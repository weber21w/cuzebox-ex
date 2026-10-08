#include "savestate.h"
#include "textgui.h"

#include "cu_avr.h"
#include "cu_spir.h"
#include "cu_spisd.h"
#include "cu_vfat.h"
#include "cu_esp.h"
#include "cu_vdev.h"
#include "cu_gun.h"
#include "cu_kbd.h"
#include "cu_ctr.h"
#include "cu_multitap.h"
#include "mainui.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
#include <direct.h>
#define SAVESTATE_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define SAVESTATE_MKDIR(path) mkdir(path, 0777)
#endif

typedef struct{
	uint32	magic;
	uint32	version;
	uint32	cpu_size;
	uint32	spir_meta_size;
	uint32	spir_ram_size;
	uint32	spisd_size;
	uint32	vfat_size;
	uint32	esp_size;
} savestate_hdr_t;

typedef struct{
	boole	valid;
	auint	frame;
	savestate_blob_t	blob;
} savestate_checkpoint_t;

typedef struct{
	boole	initialized;
	auint	active_slot;
	auint	next_checkpoint;
	savestate_checkpoint_t	checkpoints[SAVESTATE_DEFAULT_CHECKPOINTS];
} savestate_state_t;

typedef struct{
	boole	ena;
	auint	mode;
	auint	state;
	auint	addr;
	auint	data;
	auint	size;
	uint8	addr_bytes;
	auint	addr_mask;
	auint	page_size;
	auint	page_mask;
} savestate_spir_meta_t;

typedef struct{
	uint32	magic;
	uint32	size;
	uint32	multitap_size;
	uint32	ctr_size;
	uint32	kbd_size;
	uint32	gun_size;
	uint32	vdev_size;
} savestate_input_hdr_t;

#define SAVESTATE_INPUT_MAGIC 0x435a4958UL /* CZIX */

#define SAVESTATE_MAGIC	0x435a5356UL	/* CZSV */
#define SAVESTATE_VERSION	1UL

static savestate_state_t savestate_state;

static void savestate_system_message(char const* fmt, ...)
{
	va_list ap;
	char    buf[96];

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	textgui_log_add(&(buf[0]));
}

static void savestate_blob_reset(savestate_blob_t *blob)
{
	if (blob == NULL){
		return;
	}
	blob->data = NULL;
	blob->size = 0U;
}

void savestate_free_blob(savestate_blob_t *blob)
{
	if (blob == NULL){
		return;
	}
	if (blob->data != NULL){
		free(blob->data);
	}
	savestate_blob_reset(blob);
}

static void savestate_copy_str(char* dst, auint cap, char const* src)
{
	if ((dst == NULL) || (cap == 0U)){ return; }
	if (src == NULL){ src = ""; }
	strncpy(dst, src, cap - 1U);
	dst[cap - 1U] = 0;
}

static void savestate_sanitize_name(char* dst, auint cap, char const* src)
{
	auint di = 0U;
	auint si = 0U;
	boole prev_us = FALSE;
	if ((dst == NULL) || (cap == 0U)){ return; }
	if (src == NULL){ src = ""; }
	while ((src[si] != 0) && (di + 1U < cap)){
		unsigned char c = (unsigned char)src[si++];
		if (isalnum(c)){
			dst[di++] = (char)c;
			prev_us = FALSE;
		}else if ((c == '_') || (c == '-') || (c == '.')){
			dst[di++] = (char)c;
			prev_us = FALSE;
		}else{
			if (!prev_us){
				dst[di++] = '_';
				prev_us = TRUE;
			}
		}
	}
	while ((di > 0U) && (dst[di - 1U] == '_')){ di--; }
	dst[di] = 0;
	if (dst[0] == 0){ savestate_copy_str(dst, cap, "default"); }
}

static void savestate_join_path(char* dst, auint cap, char const* a, char const* b)
{
	if ((a == NULL) || (a[0] == 0)){
		savestate_copy_str(dst, cap, (b == NULL) ? "" : b);
		return;
	}
	if ((b == NULL) || (b[0] == 0)){
		savestate_copy_str(dst, cap, a);
		return;
	}
	if ((a[strlen(a) - 1U] == '/') || (a[strlen(a) - 1U] == '\\')){
		snprintf(dst, cap, "%s%s", a, b);
	}else{
		snprintf(dst, cap, "%s/%s", a, b);
	}
	dst[cap - 1U] = 0;
}

static boole savestate_mkdirs(char const* path)
{
	char tmp[1024];
	auint i;
	auint len;
	int rc;
	if ((path == NULL) || (path[0] == 0)){ return FALSE; }
	savestate_copy_str(tmp, sizeof(tmp), path);
	len = (auint)strlen(tmp);
	if ((len > 0U) && ((tmp[len - 1U] == '/') || (tmp[len - 1U] == '\\'))){
		tmp[len - 1U] = 0;
	}
	for (i = 1U; tmp[i] != 0; i++){
		if ((tmp[i] == '/') || (tmp[i] == '\\')){
			char hold = tmp[i];
			tmp[i] = 0;
			rc = SAVESTATE_MKDIR(tmp);
			if ((rc != 0) && (errno != EEXIST)){
				tmp[i] = hold;
				return FALSE;
			}
			tmp[i] = hold;
		}
	}
	rc = SAVESTATE_MKDIR(tmp);
	if ((rc != 0) && (errno != EEXIST)){ return FALSE; }
	return TRUE;
}

static char const* savestate_slot_name(auint slot)
{
	static char path[1024];
	char romdir[256];
	char base[512];
	char leaf[64];
	savestate_sanitize_name(romdir, sizeof(romdir), mainui_get_current_rom_name());
	savestate_join_path(base, sizeof(base), mainui_get_save_path(), "savestates");
	savestate_join_path(base, sizeof(base), base, romdir);
	(void)savestate_mkdirs(base);
	snprintf(leaf, sizeof(leaf), "slot_%u.czs", (unsigned)(slot % SAVESTATE_SLOT_COUNT));
	savestate_join_path(path, sizeof(path), base, leaf);
	return path;
}

static void savestate_sanitize_esp(cu_state_esp_t *esp)
{
	auint i;

	if (esp == NULL){
		return;
	}

	for (i = 0U; i < ESP_LINK_COUNT; i++){
		esp->socks[i] = ESP_INVALID_SOCKET;
		esp->proto[i] = 0U;
		esp->protocol[i] = 0U;
		memset(&(esp->sock_info[i]), 0, sizeof(esp->sock_info[i]));
	}

	for (i = 0U; i < 3U; i++){
		esp->ping_sock[i] = ESP_INVALID_SOCKET;
	}

	esp->listen_socket = ESP_INVALID_SOCKET;
	esp->uart_logging_file = NULL;
	esp->uart_playback_file = NULL;
	esp->uart_logging_started = 0U;
	esp->uart_playback_started = 0U;
	esp->host_serial_enabled = 0U;
	esp->host_midi_enabled = 0U;
	esp->virtual_midi_enabled = 0U;
	esp->tcp_serial_enabled = 0U;
	esp->tcp_serial_connect_pending = 0U;
	esp->tcp_serial_state = CU_ESP_TCP_SERIAL_STATE_DISCONNECTED;
	esp->tcp_serial_last_error = 0;
	esp->tcp_serial_tx_head = 0U;
	esp->tcp_serial_tx_tail = 0U;
	esp->tcp_serial_tx_count = 0U;
	esp->tcp_serial_rx_head = 0U;
	esp->tcp_serial_rx_tail = 0U;
	esp->tcp_serial_rx_count = 0U;
	esp->host_serial_bypass = 0U;
	esp->host_midi_bypass = 0U;
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	esp->host_serial_port = INVALID_HANDLE_VALUE;
#else
	esp->host_serial_port = ESP_SERIAL_OPEN_ERROR;
#endif
	esp->tcp_serial_sock = ESP_INVALID_SOCKET;
	esp->tcp_serial_listen_sock = ESP_INVALID_SOCKET;
	esp->loopback_head = 0U;
	esp->loopback_tail = 0U;
	esp->loopback_count = 0U;
	esp->loopback_drops = 0U;
	esp->uart_rx_error_flags = 0U;
	esp->uart_txc_pending = 0U;
	esp->uart_tx_udr_valid = 0U;
	esp->uart_tx_udr_byte = 0U;
	esp->uart_tx_shift_valid = 0U;
	esp->uart_tx_shift_byte = 0U;
	esp->uart_tx_complete_cycle = 0U;

#ifndef __EMSCRIPTEN__
	esp->async_run = 0U;
	esp->async_sync_init = 0U;
	esp->async_job_r = 0U;
	esp->async_job_w = 0U;
	esp->async_evt_r = 0U;
	esp->async_evt_w = 0U;
	memset(&(esp->async_jobs[0]), 0, sizeof(esp->async_jobs));
	memset(&(esp->async_evts[0]), 0, sizeof(esp->async_evts));
#endif

#if defined(ENABLE_OPENSSL) && !defined(__EMSCRIPTEN__)
	for (i = 0U; i < ESP_MAX_LINKS; i++){
		esp->tls_ctx[i] = NULL;
		esp->tls_ssl[i] = NULL;
	}
#endif
}

static boole savestate_build_blob(savestate_blob_t *blob)
{
	savestate_hdr_t hdr;
	savestate_spir_meta_t spir_meta;
	cu_state_cpu_t *cpu;
	cu_state_spir_t *spir;
	cu_state_spisd_t *spisd;
	cu_state_esp_t *esp;
	cu_state_multitap_t mtap;
	cu_state_ctr_t ctr;
	cu_state_kbd_t kbd;
	cu_state_gun_t gun;
	cu_state_vdev_t vdev;
	savestate_input_hdr_t input_hdr;
	uint8 *dst;
	auint total_size;

	if (blob == NULL){
		return FALSE;
	}

	cpu = cu_avr_get_state();
	spir = cu_spir_get_state();
	spisd = cu_spisd_get_state();
	esp = cu_esp_get_state();	
	cu_multitap_get_state(&mtap);
	cu_ctr_get_state(&ctr);
	cu_kbd_get_state(&kbd);
	cu_gun_get_state(&gun);
	cu_vdev_get_state(&vdev);

	spir_meta.ena = spir->ena;
	spir_meta.mode = spir->mode;
	spir_meta.state = spir->state;
	spir_meta.addr = spir->addr;
	spir_meta.data = spir->data;
	spir_meta.size = spir->size;
	spir_meta.addr_bytes = spir->addr_bytes;
	spir_meta.addr_mask = spir->addr_mask;
	spir_meta.page_size = spir->page_size;
	spir_meta.page_mask = spir->page_mask;

	hdr.magic = SAVESTATE_MAGIC;
	hdr.version = SAVESTATE_VERSION;
	hdr.cpu_size = (uint32)sizeof(*cpu);
	hdr.spir_meta_size = (uint32)sizeof(spir_meta);
	hdr.spir_ram_size = (uint32)spir->size;
	hdr.spisd_size = (uint32)sizeof(*spisd);
	hdr.vfat_size = (uint32)cu_vfat_state_size();
	if (hdr.vfat_size == 0U){
		print_error("savestate: vfat serialization size invalid\n");
		return FALSE;
	}
	hdr.esp_size = (uint32)sizeof(*esp);
	input_hdr.magic = SAVESTATE_INPUT_MAGIC;
	input_hdr.size = (uint32)(sizeof(input_hdr) + sizeof(mtap) + sizeof(ctr) + sizeof(kbd) + sizeof(gun) + sizeof(vdev));
	input_hdr.multitap_size = (uint32)sizeof(mtap);
	input_hdr.ctr_size = (uint32)sizeof(ctr);
	input_hdr.kbd_size = (uint32)sizeof(kbd);
	input_hdr.gun_size = (uint32)sizeof(gun);
	input_hdr.vdev_size = (uint32)sizeof(vdev);

	total_size = (auint)sizeof(hdr) +
			 (auint)hdr.cpu_size +
			 (auint)hdr.spir_meta_size +
			 (auint)hdr.spir_ram_size +
			 (auint)hdr.spisd_size +
			 (auint)hdr.vfat_size +
			 (auint)hdr.esp_size +
			 (auint)input_hdr.size;

	savestate_free_blob(blob);
	blob->data = (uint8*)malloc(total_size);
	if (blob->data == NULL){
		print_error("savestate: allocation failed (%u bytes)\n", (unsigned)total_size);
		blob->size = 0U;
		return FALSE;
	}
	blob->size = total_size;

	dst = blob->data;
	memcpy(dst, &hdr, sizeof(hdr));
	dst += sizeof(hdr);
	memcpy(dst, cpu, sizeof(*cpu));
	dst += sizeof(*cpu);
	memcpy(dst, &spir_meta, sizeof(spir_meta));
	dst += sizeof(spir_meta);
	if ((spir->size != 0U) && (spir->ram != NULL)){
		memcpy(dst, spir->ram, spir->size);
	}
	dst += spir->size;
	memcpy(dst, spisd, sizeof(*spisd));
	dst += sizeof(*spisd);
	if (!cu_vfat_state_save(dst, (auint)hdr.vfat_size)){
		print_error("savestate: vfat serialization failed\n");
		savestate_free_blob(blob);
		return FALSE;
	}
	dst += hdr.vfat_size;
	memcpy(dst, esp, sizeof(*esp));
	dst += sizeof(*esp);
	memcpy(dst, &input_hdr, sizeof(input_hdr));
	dst += sizeof(input_hdr);
	memcpy(dst, &mtap, sizeof(mtap));
	dst += sizeof(mtap);
	memcpy(dst, &ctr, sizeof(ctr));
	dst += sizeof(ctr);
	memcpy(dst, &kbd, sizeof(kbd));
	dst += sizeof(kbd);
	memcpy(dst, &gun, sizeof(gun));
	dst += sizeof(gun);
	memcpy(dst, &vdev, sizeof(vdev));

	return TRUE;
}

boole savestate_capture_to_mem(savestate_blob_t *blob)
{
	return savestate_build_blob(blob);
}

boole savestate_restore_from_mem(savestate_blob_t const *blob)
{
	savestate_hdr_t hdr;
	savestate_spir_meta_t spir_meta;
	savestate_input_hdr_t input_hdr;
	uint8 const *src;
	cu_state_cpu_t *cpu;
	uint8 const *ext_src;
	auint remaining;
	cu_state_spir_t *spir;
	cu_state_spisd_t *spisd;
	cu_state_esp_t *esp;
	auint banks;

	if ((blob == NULL) || (blob->data == NULL)){
		return FALSE;
	}
	if (blob->size < sizeof(hdr)){
		return FALSE;
	}

	src = blob->data;
	memcpy(&hdr, src, sizeof(hdr));
	src += sizeof(hdr);

	if ((hdr.magic != SAVESTATE_MAGIC) || (hdr.version != SAVESTATE_VERSION)){
		print_error("savestate: invalid image\n");
		return FALSE;
	}
	if (blob->size < ((auint)sizeof(hdr) + hdr.cpu_size + hdr.spir_meta_size + hdr.spir_ram_size + hdr.spisd_size + hdr.vfat_size + hdr.esp_size)){
		print_error("savestate: truncated image\n");
		return FALSE;
	}
	if (hdr.cpu_size != sizeof(*cu_avr_get_state())){
		print_error("savestate: cpu size mismatch\n");
		return FALSE;
	}
	if (hdr.spir_meta_size != sizeof(spir_meta)){
		print_error("savestate: spir meta size mismatch\n");
		return FALSE;
	}
	if (hdr.spisd_size != sizeof(*cu_spisd_get_state())){
		print_error("savestate: sd size mismatch\n");
		return FALSE;
	}
	if (hdr.vfat_size < sizeof(cu_state_vfat_t)){
		print_error("savestate: vfat size invalid\n");
		return FALSE;
	}
	/* ESP state grew from the legacy four-link layout.  Old packaged CUzeBox
	 * states remain loadable; their raw ESP block cannot be safely field-mapped,
	 * so only that peripheral is reset while CPU/SD/SPI/input state is restored. */
	if (hdr.esp_size != sizeof(*cu_esp_get_state()) && hdr.esp_size != 132512u){
		print_error("savestate: unsupported esp state size %u\n",(unsigned)hdr.esp_size);
		return FALSE;
	}

	cpu = cu_avr_get_state();
	memcpy(cpu, src, sizeof(*cpu));
	src += sizeof(*cpu);
	cu_avr_crom_update(0U, 65536U);
	cu_avr_io_update();

	memcpy(&spir_meta, src, sizeof(spir_meta));
	src += sizeof(spir_meta);
	banks = (spir_meta.size + 65535U) >> 16;
	if (banks == 0U){
		banks = 1U;
	}
	cu_spir_set_size(banks);
	spir = cu_spir_get_state();
	spir->ena = spir_meta.ena;
	spir->mode = spir_meta.mode;
	spir->state = spir_meta.state;
	spir->addr = spir_meta.addr;
	spir->data = spir_meta.data;
	spir->size = spir_meta.size;
	spir->addr_bytes = spir_meta.addr_bytes;
	spir->addr_mask = spir_meta.addr_mask;
	spir->page_size = spir_meta.page_size;
	spir->page_mask = spir_meta.page_mask;
	if ((spir->size != 0U) && (spir->ram != NULL)){
		memcpy(spir->ram, src, spir->size);
	}
	src += hdr.spir_ram_size;
	cu_spir_update();

	spisd = cu_spisd_get_state();
	memcpy(spisd, src, sizeof(*spisd));
	src += sizeof(*spisd);
	cu_spisd_update();

	if (!cu_vfat_state_load(src, (auint)hdr.vfat_size)){
		print_error("savestate: vfat restore failed\n");
		return FALSE;
	}
	src += hdr.vfat_size;

	esp = cu_esp_get_state();
	if(hdr.esp_size == sizeof(*esp)){
		memcpy(esp,src,sizeof(*esp));savestate_sanitize_esp(esp);
	}else{
		/* Preserve the user's current ESP configuration and reset volatile
		 * networking/UART state instead of interpreting the incompatible block. */
		savestate_sanitize_esp(esp);cu_esp_reset_network();
	}
	src += hdr.esp_size;

	remaining = (auint)(blob->data + blob->size - src);
	if (remaining >= sizeof(input_hdr)) {
		memcpy(&input_hdr, src, sizeof(input_hdr));
		if ((input_hdr.magic == SAVESTATE_INPUT_MAGIC) &&
		    (input_hdr.size >= (sizeof(input_hdr) + sizeof(cu_state_multitap_t) + sizeof(cu_state_ctr_t) + sizeof(cu_state_kbd_t) + sizeof(cu_state_gun_t) + sizeof(cu_state_vdev_t))) &&
		    (input_hdr.size <= remaining) &&
		    (input_hdr.multitap_size == sizeof(cu_state_multitap_t)) &&
		    (input_hdr.ctr_size == sizeof(cu_state_ctr_t)) &&
		    (input_hdr.kbd_size == sizeof(cu_state_kbd_t)) &&
		    (input_hdr.gun_size == sizeof(cu_state_gun_t)) &&
		    (input_hdr.vdev_size == sizeof(cu_state_vdev_t))) {
			cu_state_multitap_t mtap;
			cu_state_ctr_t ctr;
			cu_state_kbd_t kbd;
			cu_state_gun_t gun;
			cu_state_vdev_t vdev;
			ext_src = src + sizeof(input_hdr);
			memcpy(&mtap, ext_src, sizeof(mtap));
			ext_src += sizeof(mtap);
			memcpy(&ctr, ext_src, sizeof(ctr));
			ext_src += sizeof(ctr);
			memcpy(&kbd, ext_src, sizeof(kbd));
			ext_src += sizeof(kbd);
			memcpy(&gun, ext_src, sizeof(gun));
			ext_src += sizeof(gun);
			memcpy(&vdev, ext_src, sizeof(vdev));
			cu_vdev_set_state(&vdev);
			cu_multitap_set_state(&mtap);
			cu_ctr_set_state(&ctr);
			cu_kbd_set_state(&kbd);
			cu_gun_set_state(&gun);
		}
	}

	return TRUE;
}

void savestate_init(void)
{
	memset(&savestate_state, 0, sizeof(savestate_state));
	savestate_state.initialized = TRUE;
}

void savestate_shutdown(void)
{
	auint i;

	for (i = 0U; i < SAVESTATE_DEFAULT_CHECKPOINTS; i++){
		savestate_free_blob(&(savestate_state.checkpoints[i].blob));
		savestate_state.checkpoints[i].valid = FALSE;
	}
	savestate_state.initialized = FALSE;
}

void savestate_set_slot(auint slot)
{
	auint next = slot % SAVESTATE_SLOT_COUNT;

	if (savestate_state.active_slot == next){
		return;
	}
	savestate_state.active_slot = next;
	savestate_system_message("SAVE SLOT %u SELECTED", (unsigned)next);
}

auint savestate_get_slot(void)
{
	return savestate_state.active_slot;
}

boole savestate_save_slot(auint slot)
{
	savestate_blob_t blob;
	FILE *fp;
	boole ok = FALSE;

	slot %= SAVESTATE_SLOT_COUNT;
	savestate_blob_reset(&blob);
	if (!savestate_capture_to_mem(&blob)){
		savestate_system_message("SAVE STATE %u FAILED", (unsigned)slot);
		return FALSE;
	}

	fp = fopen(savestate_slot_name(slot), "wb");
	if (fp == NULL){
		print_error("savestate: unable to open slot file for write\n");
	}else if (fwrite(blob.data, 1U, blob.size, fp) != blob.size){
		print_error("savestate: short write\n");
	}else{
		ok = TRUE;
	}

	if (fp != NULL){
		fclose(fp);
	}
	savestate_free_blob(&blob);
	if (ok){
		savestate_system_message("SAVE STATE %u SAVED", (unsigned)slot);
	}else{
		savestate_system_message("SAVE STATE %u FAILED", (unsigned)slot);
	}
	return ok;
}

boole savestate_load_slot(auint slot)
{
	savestate_blob_t blob;
	FILE *fp;
	long fsz;
	boole ok = FALSE;

	slot %= SAVESTATE_SLOT_COUNT;
	savestate_blob_reset(&blob);
	fp = fopen(savestate_slot_name(slot), "rb");
	if (fp == NULL){
		savestate_system_message("SAVE STATE %u LOAD FAILED", (unsigned)slot);
		return FALSE;
	}
	if (fseek(fp, 0L, SEEK_END) != 0){
		fclose(fp);
		savestate_system_message("SAVE STATE %u LOAD FAILED", (unsigned)slot);
		return FALSE;
	}
	fsz = ftell(fp);
	if (fsz <= 0L){
		fclose(fp);
		savestate_system_message("SAVE STATE %u LOAD FAILED", (unsigned)slot);
		return FALSE;
	}
	if (fseek(fp, 0L, SEEK_SET) != 0){
		fclose(fp);
		savestate_system_message("SAVE STATE %u LOAD FAILED", (unsigned)slot);
		return FALSE;
	}
	blob.size = (auint)fsz;
	blob.data = (uint8*)malloc(blob.size);
	if (blob.data == NULL){
		fclose(fp);
		savestate_system_message("SAVE STATE %u LOAD FAILED", (unsigned)slot);
		return FALSE;
	}
	if (fread(blob.data, 1U, blob.size, fp) == blob.size){
		ok = savestate_restore_from_mem(&blob);
	}
	fclose(fp);
	savestate_free_blob(&blob);
	if (ok){
		savestate_system_message("SAVE STATE %u LOADED", (unsigned)slot);
	}else{
		savestate_system_message("SAVE STATE %u LOAD FAILED", (unsigned)slot);
	}
	return ok;
}

boole savestate_slot_exists(auint slot)
{
	FILE* fp;
	fp = fopen(savestate_slot_name(slot), "rb");
	if (fp == NULL){ return FALSE; }
	fclose(fp);
	return TRUE;
}

void savestate_checkpoint_clear(void)
{
	auint i;
	for (i = 0U; i < SAVESTATE_DEFAULT_CHECKPOINTS; i++){
		savestate_free_blob(&(savestate_state.checkpoints[i].blob));
		savestate_state.checkpoints[i].valid = FALSE;
		savestate_state.checkpoints[i].frame = 0U;
	}
	savestate_state.next_checkpoint = 0U;
}

boole savestate_checkpoint_capture(auint frame)
{
	savestate_checkpoint_t *cp;

	cp = &(savestate_state.checkpoints[savestate_state.next_checkpoint % SAVESTATE_DEFAULT_CHECKPOINTS]);
	savestate_free_blob(&(cp->blob));
	if (!savestate_capture_to_mem(&(cp->blob))){
		cp->valid = FALSE;
		return FALSE;
	}
	cp->valid = TRUE;
	cp->frame = frame;
	savestate_state.next_checkpoint = (savestate_state.next_checkpoint + 1U) % SAVESTATE_DEFAULT_CHECKPOINTS;
	return TRUE;
}

boole savestate_checkpoint_restore_at_or_before(auint frame, auint *loaded_frame)
{
	auint i;
	boole found = FALSE;
	auint best_index = 0U;
	auint best_frame = 0U;

	for (i = 0U; i < SAVESTATE_DEFAULT_CHECKPOINTS; i++){
		savestate_checkpoint_t const *cp = &(savestate_state.checkpoints[i]);
		if (!cp->valid){
			continue;
		}
		if (cp->frame > frame){
			continue;
		}
		if ((!found) || (cp->frame >= best_frame)){
			found = TRUE;
			best_index = i;
			best_frame = cp->frame;
		}
	}

	if (!found){
		return FALSE;
	}
	if (!savestate_restore_from_mem(&(savestate_state.checkpoints[best_index].blob))){
		return FALSE;
	}
	if (loaded_frame != NULL){
		*loaded_frame = best_frame;
	}
	return TRUE;
}
