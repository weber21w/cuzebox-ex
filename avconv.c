/*
 *  AVConv video output generator
 *
 *  Copyright (C) 2016
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

#include "avconv.h"
#include "guicore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
#include <direct.h>
#include <io.h>
#define AVCONV_MKDIR(path) _mkdir(path)
#define AVCONV_POPEN(cmd, mode) _popen((cmd), (mode))
#define AVCONV_PCLOSE(fp) _pclose((fp))
#define AVCONV_UNLINK(path) _unlink(path)
#define AVCONV_PIPE_WRITE_MODE "wb"
#else
#include <sys/types.h>
#define AVCONV_MKDIR(path) mkdir(path, 0777)
#define AVCONV_POPEN(cmd, mode) popen((cmd), (mode))
#define AVCONV_PCLOSE(fp) pclose((fp))
#define AVCONV_UNLINK(path) unlink(path)
#define AVCONV_PIPE_WRITE_MODE "w"
#endif

#define AVCONV_NAME_CAP 256U

/* Identifies whether any capturing was performed */
static boole avconv_isinit = FALSE;

/* Identifies whether the recording is available (that opening the pipes did not fail) */
static boole avconv_ok     = FALSE;

/* The pipe used for the audio stream */
static FILE* avconv_pipe_a = NULL;

/* The pipe used for the video stream */
static FILE* avconv_pipe_v = NULL;

/* Audio fraction (to get 800.8008 samples / frame) */
static auint avconv_afrac = 0U;
static boole avconv_sigpipe_ignored = FALSE;
static boole avconv_autoinc = TRUE;
static char  avconv_name[AVCONV_NAME_CAP] = "video/CAPTURE001.MP4";
static char  avconv_active_name[AVCONV_NAME_CAP] = "";
static char  avconv_status[AVCONV_NAME_CAP] = "READY";

static void avconv_copy_name(char* dst, auint cap, char const* src)
{
	if ((dst == NULL) || (cap == 0U)){ return; }
	if (src == NULL){ src = ""; }
	strncpy(dst, src, cap - 1U);
	dst[cap - 1U] = 0;
}

static void avconv_set_status(char const* status)
{
	avconv_copy_name(avconv_status, sizeof(avconv_status), status);
}

static boole avconv_mkdirs_for_file(char const* path)
{
	char tmp[AVCONV_NAME_CAP];
	char* slash1;
	char* slash2;
	char* slash;
	auint i;
	int rc;
	if ((path == NULL) || (path[0] == 0)){ return TRUE; }
	avconv_copy_name(tmp, sizeof(tmp), path);
	slash1 = strrchr(tmp, '/');
	slash2 = strrchr(tmp, '\\');
	slash = slash1;
	if ((slash2 != NULL) && ((slash == NULL) || (slash2 > slash))){ slash = slash2; }
	if (slash == NULL){ return TRUE; }
	*slash = 0;
	if (tmp[0] == 0){ return TRUE; }
	for (i = 1U; tmp[i] != 0; i++){
		if ((tmp[i] == '/') || (tmp[i] == '\\')){
			char hold = tmp[i];
			tmp[i] = 0;
			rc = AVCONV_MKDIR(tmp);
			if ((rc != 0) && (errno != EEXIST)){
				tmp[i] = hold;
				return FALSE;
			}
			tmp[i] = hold;
		}
	}
	rc = AVCONV_MKDIR(tmp);
	if ((rc != 0) && (errno != EEXIST)){ return FALSE; }
	return TRUE;
}

static boole avconv_shell_quote(char* dst, auint cap, char const* src)
{
	auint si;
	auint di = 0U;
	if ((dst == NULL) || (cap < 3U) || (src == NULL)){ return FALSE; }
	dst[0] = 0;

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	/* system() and _popen() use cmd.exe, where single quotes are ordinary
	** filename characters. Double quotes are required for spaces. Reject the
	** few characters which cannot be represented safely by this small helper. */
	dst[di++] = '"';
	for (si = 0U; src[si] != 0; si++){
		if ((src[si] == '"') || (src[si] == '%') || (src[si] == '!') ||
		    (src[si] == '\r') || (src[si] == '\n')){
			dst[0] = 0;
			return FALSE;
		}
		if ((di + 1U) >= cap){
			dst[0] = 0;
			return FALSE;
		}
		dst[di++] = src[si];
	}
	if ((di + 1U) >= cap){
		dst[0] = 0;
		return FALSE;
	}
	dst[di++] = '"';
#else
	dst[di++] = '\'';
	for (si = 0U; (src[si] != 0) && (di + 5U < cap); si++){
		if (src[si] == '\''){
			dst[di++] = '\'';
			dst[di++] = '\\';
			dst[di++] = '\'';
			dst[di++] = '\'';
		}else{
			dst[di++] = src[si];
		}
	}
	if (src[si] != 0){
		dst[0] = 0;
		return FALSE;
	}
	dst[di++] = '\'';
#endif
	dst[di] = 0;
	return TRUE;
}

static boole avconv_file_nonempty(char const* path)
{
	struct stat st;
	if ((path == NULL) || (path[0] == 0)){ return FALSE; }
	if (stat(path, &st) != 0){ return FALSE; }
	return (st.st_size > 0) ? TRUE : FALSE;
}

static void avconv_force_mp4_extension(char* path, auint cap)
{
	char* slash1;
	char* slash2;
	char* slash;
	char* dot;
	auint base_len;
	if ((path == NULL) || (cap < 5U)){ return; }
	slash1 = strrchr(path, '/');
	slash2 = strrchr(path, '\\');
	slash = slash1;
	if ((slash2 != NULL) && ((slash == NULL) || (slash2 > slash))){ slash = slash2; }
	dot = strrchr(path, '.');
	if ((dot != NULL) && ((slash == NULL) || (dot > (slash + 1)))){
		base_len = (auint)(dot - path);
	}else{
		base_len = (auint)strlen(path);
	}
	if ((base_len + 5U) > cap){ base_len = cap - 5U; }
	memcpy(path + base_len, ".MP4", 5U);
}

static void avconv_remove_temporary_files(void)
{
	AVCONV_UNLINK("~tempvid.mp4");
	AVCONV_UNLINK("~tempaud.wav");
	AVCONV_UNLINK("~tempvid.log");
	AVCONV_UNLINK("~tempaud.log");
	AVCONV_UNLINK("~tempvid.err");
	AVCONV_UNLINK("~tempaud.err");
	AVCONV_UNLINK("~capturemux.log");
	AVCONV_UNLINK("~capturemux.err");
}

static void avconv_prepare_active_name(void)
{
	char dir[AVCONV_NAME_CAP];
	char base[AVCONV_NAME_CAP];
	char stem[AVCONV_NAME_CAP];
	char ext[64];
	char cand[AVCONV_NAME_CAP];
	char const* src;
	char const* slash1;
	char const* slash2;
	char const* slash;
	char const* dot;
	auint len;
	auint i;
	unsigned idx;

	src = avconv_name;
	if (src[0] == 0){ src = "CAPTURE001.MP4"; }
	if (!avconv_autoinc){
		avconv_copy_name(avconv_active_name, sizeof(avconv_active_name), src);
		(void)avconv_mkdirs_for_file(avconv_active_name);
		return;
	}

	slash1 = strrchr(src, '/');
	slash2 = strrchr(src, '\\');
	slash = slash1;
	if ((slash2 != NULL) && ((slash == NULL) || (slash2 > slash))){ slash = slash2; }
	if (slash != NULL){
		len = (auint)((slash - src) + 1);
		if (len >= sizeof(dir)){ len = sizeof(dir) - 1U; }
		memcpy(dir, src, len);
		dir[len] = 0;
		src = slash + 1;
	}else{
		dir[0] = 0;
	}

	avconv_copy_name(base, sizeof(base), src);
	dot = strrchr(base, '.');
	if ((dot != NULL) && (dot != base)){
		avconv_copy_name(ext, sizeof(ext), dot);
		len = (auint)(dot - base);
		if (len >= sizeof(stem)){ len = sizeof(stem) - 1U; }
		memcpy(stem, base, len);
		stem[len] = 0;
	}else{
		avconv_copy_name(ext, sizeof(ext), ".MP4");
		avconv_copy_name(stem, sizeof(stem), base);
	}

	len = (auint)strlen(stem);
	while ((len > 0U) && (stem[len - 1U] >= '0') && (stem[len - 1U] <= '9')){
		len--;
	}
	stem[len] = 0;
	if (stem[0] == 0){
		avconv_copy_name(stem, sizeof(stem), "CAPTURE");
	}

	for (idx = 1U; idx < 10000U; idx++){
		snprintf(cand, sizeof(cand), "%s%s%03u%s", dir, stem, idx, ext);
		if (access(cand, F_OK) != 0){
			avconv_copy_name(avconv_active_name, sizeof(avconv_active_name), cand);
			(void)avconv_mkdirs_for_file(avconv_active_name);
			return;
		}
	}

	for (i = 0U; stem[i] != 0; i++){
		if ((stem[i] < 'A') || (stem[i] > 'Z')){ break; }
	}
	snprintf(cand, sizeof(cand), "%s%s9999%s", dir, stem, ext);
	avconv_copy_name(avconv_active_name, sizeof(avconv_active_name), cand);
	(void)avconv_mkdirs_for_file(avconv_active_name);
}

void avconv_push(uint8 const* samples, auint len)
{
	uint32* pixbuf;
	auint   areq;
	uint8   samp48k[801];
	auint   i;
	auint   j;
	auint   frac;
	char    tstr[1024];
	char    pixs[5] = {'0', '0', '0', '0', 0};
	guicore_pixfmt_t pixfmt;

	if (!avconv_sigpipe_ignored){
		#ifdef SIGPIPE
		signal(SIGPIPE, SIG_IGN);
		#endif
		avconv_sigpipe_ignored = TRUE;
	}

	if (!avconv_isinit){
		guicore_getpixfmt(&pixfmt);
		pixs[pixfmt.rsh / 8U] = 'r';
		pixs[pixfmt.gsh / 8U] = 'g';
		pixs[pixfmt.bsh / 8U] = 'b';
		avconv_prepare_active_name();

		snprintf(&(tstr[0]),
				sizeof(tstr),
				"ffmpeg"
				" -y"
				" -f rawvideo"
				" -s 640x228"
				" -pix_fmt %s"
				" -r 59.94"
				" -i -"
				" -vf crop=620:228:10:0"
				" -an"
				" -preset ultrafast"
				" -qp 0"
				" -tune animation"
				" ~tempvid.mp4"
				" 1> ~tempvid.log"
				" 2> ~tempvid.err",
				&pixs[0]);
		avconv_pipe_v = AVCONV_POPEN(&(tstr[0]), AVCONV_PIPE_WRITE_MODE);

		avconv_pipe_a = AVCONV_POPEN(
				"ffmpeg"
				" -y"
				" -f u8"
				" -ar 48000"
				" -ac 1"
				" -i -"
				" -c:a pcm_u8"
				" ~tempaud.wav"
				" 1> ~tempaud.log"
				" 2> ~tempaud.err",
				AVCONV_PIPE_WRITE_MODE);

		if ((avconv_pipe_v == NULL) || (avconv_pipe_a == NULL)){
			if (avconv_pipe_v != NULL){ AVCONV_PCLOSE(avconv_pipe_v); }
			if (avconv_pipe_a != NULL){ AVCONV_PCLOSE(avconv_pipe_a); }
			avconv_pipe_v = NULL;
			avconv_pipe_a = NULL;
			avconv_ok = FALSE;
			avconv_set_status("FAILED TO OPEN VIDEO DUMP PIPES");
		}else{
			avconv_ok = TRUE;
			snprintf(avconv_status, sizeof(avconv_status), "RECORDING %s", avconv_active_name);
		}
		avconv_isinit = TRUE;
	}

	if ((samples == NULL) || (len == 0U)){
		return;
	}
	if (!avconv_ok){
		return;
	}

	areq = 800U;
	avconv_afrac += 8008U;
	if (avconv_afrac >= 10000U){
		areq++;
		avconv_afrac -= 10000U;
	}

	frac = 0U;
	j = 0U;
	for (i = 0U; i < areq; i++){
		samp48k[i] = samples[j];
		frac += len;
		if (frac >= areq){
			frac -= areq;
			j++;
		}
	}

	pixbuf = guicore_getpixbuf() + (640U * 20U);
	if (fwrite(pixbuf, (640U * 228U) * sizeof(uint32), 1U, avconv_pipe_v) != 1U){
		AVCONV_PCLOSE(avconv_pipe_v);
		AVCONV_PCLOSE(avconv_pipe_a);
		avconv_pipe_v = NULL;
		avconv_pipe_a = NULL;
		avconv_ok = FALSE;
		avconv_set_status("VIDEO DUMP VIDEO PIPE WRITE FAILED");
		return;
	}
	if (fwrite(&(samp48k[0]), areq, 1U, avconv_pipe_a) != 1U){
		AVCONV_PCLOSE(avconv_pipe_v);
		AVCONV_PCLOSE(avconv_pipe_a);
		avconv_pipe_v = NULL;
		avconv_pipe_a = NULL;
		avconv_ok = FALSE;
		avconv_set_status("VIDEO DUMP AUDIO PIPE WRITE FAILED");
		return;
	}
}

boole avconv_is_initialized(void)
{
	return avconv_isinit;
}

void avconv_finalize(void)
{
	int  rc;
	int  c0 = 0;
	int  c1 = 0;
	char cmd[1536];
	char qout[AVCONV_NAME_CAP * 2U];
	boole quoted = FALSE;

	if (avconv_isinit && avconv_ok){
		if (avconv_pipe_v != NULL){ c0 = AVCONV_PCLOSE(avconv_pipe_v); avconv_pipe_v = NULL; }
		if (avconv_pipe_a != NULL){ c1 = AVCONV_PCLOSE(avconv_pipe_a); avconv_pipe_a = NULL; }
		quoted = avconv_shell_quote(qout, sizeof(qout), avconv_active_name);
		rc = -1;
		if ((c0 == 0) && (c1 == 0) && quoted){
			snprintf(cmd, sizeof(cmd),
					"ffmpeg"
					" -y"
					" -i ~tempvid.mp4"
					" -i ~tempaud.wav"
					" -map 0:v:0"
					" -map 1:a:0"
					" -vf scale=940:228:flags=bilinear,scale=940:684:flags=neighbor,pad=960:720:16:18"
					" -c:v libx264"
					" -preset medium"
					" -crf 22"
					" -tune animation"
					" -pix_fmt yuv420p"
					" -profile:v high"
					" -level:v 3.2"
					" -tag:v avc1"
					" -c:a aac"
					" -b:a 192k"
					" -ar 48000"
					" -movflags +faststart"
					" -shortest"
					" -f mp4 %s"
					" 1> ~capturemux.log"
					" 2> ~capturemux.err",
					qout);
			rc = system(cmd);
		}
		if ((c0 == 0) && (c1 == 0) && quoted && (rc == 0) &&
		    avconv_file_nonempty(avconv_active_name)){
			avconv_remove_temporary_files();
			snprintf(avconv_status, sizeof(avconv_status), "WROTE %s", avconv_active_name);
		}else if (c0 != 0){
			avconv_set_status("VIDEO ENCODER FAILED; SEE ~tempvid.err");
		}else if (c1 != 0){
			avconv_set_status("AUDIO ENCODER FAILED; SEE ~tempaud.err");
		}else if (!quoted){
			avconv_set_status("VIDEO OUTPUT PATH HAS UNSUPPORTED CHARACTERS");
		}else{
			avconv_set_status("VIDEO MUX FAILED; SEE ~capturemux.err");
		}
	}else if (avconv_isinit){
		if (avconv_pipe_v != NULL){ AVCONV_PCLOSE(avconv_pipe_v); avconv_pipe_v = NULL; }
		if (avconv_pipe_a != NULL){ AVCONV_PCLOSE(avconv_pipe_a); avconv_pipe_a = NULL; }
		if (avconv_active_name[0] != 0){
			snprintf(avconv_status, sizeof(avconv_status), "VIDEO DUMP FAILED FOR %s", avconv_active_name);
		}else{
			avconv_set_status("VIDEO DUMP FAILED");
		}
	}else{
		avconv_set_status("NO VIDEO DUMP TO FINALIZE");
	}

	avconv_pipe_v = NULL;
	avconv_pipe_a = NULL;
	avconv_isinit = FALSE;
	avconv_ok = FALSE;
	avconv_afrac = 0U;
}

char const* avconv_get_filename(void)
{
	return avconv_name;
}

void avconv_set_filename(char const* name)
{
	avconv_copy_name(avconv_name, sizeof(avconv_name), name);
	if (avconv_name[0] == 0){
		avconv_copy_name(avconv_name, sizeof(avconv_name), "video/CAPTURE001.MP4");
	}
	avconv_force_mp4_extension(avconv_name, sizeof(avconv_name));
}

char const* avconv_get_active_filename(void)
{
	return avconv_active_name;
}

char const* avconv_get_status(void)
{
	return avconv_status;
}

boole avconv_get_autoinc(void)
{
	return avconv_autoinc;
}

void avconv_set_autoinc(boole enable)
{
	avconv_autoinc = enable ? TRUE : FALSE;
}
