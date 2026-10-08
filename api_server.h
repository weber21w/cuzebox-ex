#ifndef API_SERVER_H
#define API_SERVER_H

#include "types.h"

#ifdef ENABLE_API_SERVER

#define API_SERVER_DEFAULT_PORT 24680U

void  api_server_configure(boole enable, auint port);
boole api_server_is_enabled(void);
auint api_server_get_port(void);
void  api_server_tick(void);
void  api_server_frame_begin(void);
void  api_server_frame_end(void);
void  api_server_notify_rom_loaded(void);
void  api_server_shutdown(void);

#else

#define API_SERVER_DEFAULT_PORT 24680U

static inline void  api_server_configure(boole enable, auint port){ (void)enable; (void)port; }
static inline boole api_server_is_enabled(void){ return FALSE; }
static inline auint api_server_get_port(void){ return API_SERVER_DEFAULT_PORT; }
static inline void  api_server_tick(void){ }
static inline void  api_server_frame_begin(void){ }
static inline void  api_server_frame_end(void){ }
static inline void  api_server_notify_rom_loaded(void){ }
static inline void  api_server_shutdown(void){ }

#endif

#endif
