#ifndef CONFIGFILE_H
#define CONFIGFILE_H

#include <stdio.h>
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <windows.h>
#endif

/* Replace only after the temporary file was written successfully. Never
** delete the previous preferences before attempting the replacement. */
static inline int configfile_replace(const char* temporary, const char* path)
{
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
	return MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
#else
	return rename(temporary, path);
#endif
}

#endif
