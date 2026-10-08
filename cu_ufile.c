/*
 *  UzeRom file parser
 *
 *  Copyright (C) 2016 - 2017
 *    Sandor Zsuga (Jubatian)
 *  Uzem (the base of CUzeBox) is copyright (C)
 *    David Etherton,
 *    Eric Anderton,
 *    Alec Bourque (Uze),
 *    Filipe Rinaldi,
 *    Sandor Zsuga (Jubatian),
 *    Matt Pandina (Artcfox)
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "cu_ufile.h"
#include "filesys.h"

/* UzeRom header magic value */
#define CU_MAGIC_LEN 6U
static const uint8 cu_magic[] = {'U', 'Z', 'E', 'B', 'O', 'X'};

/* String constants */
#ifndef HEADLESS
static const char cu_id[]  = "UzeRom parser: ";
static const char cu_err[] = "Error: ";
static const char cu_war[] = "Warning: ";
#endif

static boole cu_ufile_parse_image(uint8 const* src, auint src_len, uint8* cmem, cu_ufile_header_t* head, char const* src_name)
{
	auint i;
	auint len;
	sint8 psupport_str[256];
	sint8 pdefault_str[256];
	sint8 pdisplay_str[256 * 2];
	sint8 jamma_str[256];
	uint32 prog_crc;
	char const* name = src_name;

	if (name == NULL){
		name = "memory image";
	}

	if (src_len < 512U){
		print_error("%s%sNot enough data for header in %s.\n", cu_id, cu_err, name);
		return FALSE;
	}

	for (i = 0U; i < CU_MAGIC_LEN; i++){
		if (src[i] != cu_magic[i]){
			print_error("%s%s%s is not a UzeRom file.\n", cu_id, cu_err, name);
			return FALSE;
		}
	}

	if (src[6] != 1U){
		print_error("%s%s%s has unknown version (%d).\n", cu_id, cu_war, name, src[6]);
	}
	head->version = src[6];

	if (src[7] != 0U){
		print_error("%s%s%s has unknown target UC (%d).\n", cu_id, cu_err, name, src[7]);
		return FALSE;
	}
	head->target = src[7];

	len = ((auint)(src[ 8])      ) +
	      ((auint)(src[ 9]) <<  8) +
	      ((auint)(src[10]) << 16) +
	      ((auint)(src[11]) << 24);
	if (len > 65535U){
		print_error("%s%s%s has too large program size (%d).\n", cu_id, cu_err, name, len);
		return FALSE;
	}
	if (src_len < (512U + len)){
		print_error("%s%sNot enough data for program in %s.\n", cu_id, cu_err, name);
		return FALSE;
	}
	head->pmemsize = len;

	head->year = ((auint)(src[12])      ) +
	             ((auint)(src[13]) <<  8);

	for (i = 0U; i < 31U; i++){ head->name[i] = src[14U + i]; }
	head->name[31] = 0U;

	for (i = 0U; i < 31U; i++){ head->author[i] = src[46U + i]; }
	head->author[31] = 0U;

	for (i = 0U; i < 256U; i++){
		head->icon[i] = src[78U + i];
	}

	head->crc32 = ((auint)(src[334])      ) +
	              ((auint)(src[335]) <<  8) +
	              ((auint)(src[336]) << 16) +
	              ((auint)(src[337]) << 24);

	head->psupport = src[0x152];
	psupport_str[0] = '\0';
	if (head->psupport & PERIPHERAL_MOUSE){ sprintf((char*)psupport_str + strlen((char*)psupport_str), "Mouse,"); }
	if (head->psupport & PERIPHERAL_KEYBOARD){ sprintf((char*)psupport_str + strlen((char*)psupport_str), "Keyboard,"); }
	if (head->psupport & PERIPHERAL_MULTITAP){ sprintf((char*)psupport_str + strlen((char*)psupport_str), "Multitap,"); }
	if (head->psupport & PERIPHERAL_ESP8266){ sprintf((char*)psupport_str + strlen((char*)psupport_str), "ESP8266,"); }
	if (head->psupport & PERIPHERAL_ESP8266_AP){ sprintf((char*)psupport_str + strlen((char*)psupport_str), "ESP8266-AP,"); }
	if (strlen((char*)psupport_str) && (psupport_str[strlen((char*)psupport_str) - 1] == ',')){ psupport_str[strlen((char*)psupport_str) - 1] = '\0'; }
	if (!strlen((char*)psupport_str)){ sprintf((char*)psupport_str + strlen((char*)psupport_str), "None"); }

	for (i = 0U; i < 63U; i++){ head->desc[i] = src[339U + i]; }
	head->desc[63] = 0U;

	head->pdefault = src[0x193];
	pdefault_str[0] = '\0';
	if (head->pdefault & PERIPHERAL_MOUSE){ sprintf((char*)pdefault_str + strlen((char*)pdefault_str), "Mouse,"); }
	if (head->pdefault & PERIPHERAL_KEYBOARD){ sprintf((char*)pdefault_str + strlen((char*)pdefault_str), "Keyboard,"); }
	if (head->pdefault & PERIPHERAL_MULTITAP){ sprintf((char*)pdefault_str + strlen((char*)pdefault_str), "Multitap,"); }
	if (head->pdefault & PERIPHERAL_ESP8266){ sprintf((char*)pdefault_str + strlen((char*)pdefault_str), "ESP8266,"); }
	if (head->pdefault & PERIPHERAL_ESP8266_AP){ sprintf((char*)pdefault_str + strlen((char*)pdefault_str), "ESP8266-AP,"); }
	if (strlen((char*)pdefault_str) && (pdefault_str[strlen((char*)pdefault_str) - 1] == ',')){ pdefault_str[strlen((char*)pdefault_str) - 1] = '\0'; }
	if (!strlen((char*)pdefault_str)){ sprintf((char*)pdefault_str + strlen((char*)pdefault_str), "None"); }
	sprintf((char*)pdisplay_str, "%s, Default: %s", psupport_str, pdefault_str);

	head->jamma = src[0x194];
	jamma_str[0] = '\0';
	if (head->jamma){
		sprintf((char*)jamma_str + strlen((char*)jamma_str), "Enabled:");
		if ((head->jamma & (JAMMA_ROTATE_90 | JAMMA_ROTATE_180 | JAMMA_ROTATE_270)) > JAMMA_ROTATE_270){
			sprintf((char*)jamma_str, "\tError: multiple rotation bits in header");
			head->jamma = 0U;
		}else if (head->jamma & JAMMA_ROTATE_90){
			sprintf((char*)jamma_str + strlen((char*)jamma_str), "Rotate 90,");
		}else if (head->jamma & JAMMA_ROTATE_180){
			sprintf((char*)jamma_str + strlen((char*)jamma_str), "Rotate 180,");
		}else if (head->jamma & JAMMA_ROTATE_270){
			sprintf((char*)jamma_str + strlen((char*)jamma_str), "Rotate 270,");
		}
		if (head->jamma & JAMMA_FLIP_H){ sprintf((char*)jamma_str + strlen((char*)jamma_str), "Flip Horizontal,"); }
		if (head->jamma & JAMMA_FLIP_V){ sprintf((char*)jamma_str + strlen((char*)jamma_str), "Flip Vertical,"); }
		if (strlen((char*)jamma_str) && (jamma_str[strlen((char*)jamma_str) - 1] == ',')){ jamma_str[strlen((char*)jamma_str) - 1] = '\0'; }
	}else{
		sprintf((char*)jamma_str + strlen((char*)jamma_str), "Disabled");
	}

	head->spiram_banks = src[0x199];

	memcpy(cmem, src + 512U, len);
	prog_crc = cu_ufile_crc32(cmem, len);
	if ((head->crc32 != 0U) && (prog_crc != head->crc32)){
		print_error("%s%s%s program CRC mismatch (header %08X, actual %08X).\n", cu_id, cu_err, name, (unsigned)head->crc32, (unsigned)prog_crc);
		return FALSE;
	}

	print_message("%sSuccesfully loaded program from %s:\n", cu_id, name);
	print_message(
	 "\tName ........: %s\n"
	 "\tAuthor ......: %s\n"
	 "\tYear ........: %u\n"
	 "\tDescription .: %s\n"
	 "\tSupported ...: %s\n"
	 "\tJAMMA .......: %s\n"
	 "\tSPI RAM Banks: %u\n",
	 (char*)(&(head->name[0])),
	 (char*)(&(head->author[0])),
	 (unsigned)(head->year),
	 (char*)(&(head->desc[0])),
	 (char*)pdisplay_str,
	 (char*)jamma_str,
	 (unsigned)(head->spiram_banks)
	);

	return TRUE;
}

uint32 cu_ufile_crc32(uint8 const* data, auint len)
{
	uint32 crc = 0xFFFFFFFFUL;
	auint  pos;
	auint  bit;
	if (data == NULL){
		return 0U;
	}
	for (pos = 0U; pos < len; pos++){
		crc ^= (uint32)data[pos];
		for (bit = 0U; bit < 8U; bit++){
			if ((crc & 1U) != 0U){ crc = (crc >> 1) ^ 0xEDB88320UL; }
			else{                  crc = (crc >> 1); }
		}
	}
	return crc ^ 0xFFFFFFFFUL;
}

boole cu_ufile_load_buf(uint8 const* src, auint src_len, uint8* cmem, cu_ufile_header_t* head)
{
	if ((src == NULL) || (cmem == NULL) || (head == NULL)){
		return FALSE;
	}
	return cu_ufile_parse_image(src, src_len, cmem, head, "memory image");
}

auint cu_ufile_build_image(uint8* dst, auint dst_cap, uint8 const* cmem, cu_ufile_header_t const* head)
{
	auint len;
	if ((dst == NULL) || (cmem == NULL) || (head == NULL)){
		return 0U;
	}
	len = head->pmemsize;
	if ((len > 65535U) || (dst_cap < (512U + len))){
		return 0U;
	}
	memset(dst, 0, 512U + len);
	memcpy(dst, cu_magic, CU_MAGIC_LEN);
	dst[6] = (uint8)head->version;
	dst[7] = (uint8)head->target;
	dst[8]  = (uint8)(len & 0xFFU);
	dst[9]  = (uint8)((len >> 8) & 0xFFU);
	dst[10] = (uint8)((len >> 16) & 0xFFU);
	dst[11] = (uint8)((len >> 24) & 0xFFU);
	dst[12] = (uint8)(head->year & 0xFFU);
	dst[13] = (uint8)((head->year >> 8) & 0xFFU);
	memcpy(dst + 14U, head->name, 32U);
	memcpy(dst + 46U, head->author, 32U);
	memcpy(dst + 78U, head->icon, 256U);
	dst[334] = (uint8)(head->crc32 & 0xFFU);
	dst[335] = (uint8)((head->crc32 >> 8) & 0xFFU);
	dst[336] = (uint8)((head->crc32 >> 16) & 0xFFU);
	dst[337] = (uint8)((head->crc32 >> 24) & 0xFFU);
	memcpy(dst + 339U, head->desc, 64U);
	dst[0x152] = head->psupport;
	dst[0x193] = head->pdefault;
	dst[0x194] = head->jamma;
	dst[0x199] = head->spiram_banks;
	memcpy(dst + 512U, cmem, len);
	return (512U + len);
}

/*
** Attempts to load the passed file into code memory. The code memory is not
** cleared, so bootloader image or other contents may be added before this if
** there are any.
**
** The code memory must be 64 KBytes.
**
** Returns TRUE if the loading was successful.
*/
boole cu_ufile_load(char const* fname, uint8* cmem, cu_ufile_header_t* head)
{
	asint rv;
	uint8 buf[512];
	auint len;
	uint8* img;
	boole  ok;

	if (!filesys_open(FILESYS_CH_EMU, fname)){
		print_error("%s%sCouldn't open %s.\n", cu_id, cu_err, fname);
		return FALSE;
	}

	rv = filesys_read(FILESYS_CH_EMU, buf, 512U);
	if (rv != 512){
		print_error("%s%sNot enough data for header in %s.\n", cu_id, cu_err, fname);
		filesys_flush(FILESYS_CH_EMU);
		return FALSE;
	}

	len = ((auint)(buf[ 8])      ) +
	      ((auint)(buf[ 9]) <<  8) +
	      ((auint)(buf[10]) << 16) +
	      ((auint)(buf[11]) << 24);
	if ((len > 65535U) || ((512U + len) < 512U)){
		print_error("%s%s%s has invalid program size (%d).\n", cu_id, cu_err, fname, len);
		filesys_flush(FILESYS_CH_EMU);
		return FALSE;
	}

	img = (uint8*)malloc(512U + len);
	if (img == NULL){
		print_error("%s%sCouldn't allocate buffer for %s.\n", cu_id, cu_err, fname);
		filesys_flush(FILESYS_CH_EMU);
		return FALSE;
	}
	memcpy(img, buf, 512U);
	rv = filesys_read(FILESYS_CH_EMU, img + 512U, len);
	filesys_flush(FILESYS_CH_EMU);
	if (rv != (asint)len){
		print_error("%s%sNot enough data for program in %s.\n", cu_id, cu_err, fname);
		free(img);
		return FALSE;
	}

	ok = cu_ufile_parse_image(img, 512U + len, cmem, head, fname);
	free(img);
	return ok;
}
