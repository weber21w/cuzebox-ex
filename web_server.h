#ifndef WEB_SERVER_H
#define WEB_SERVER_H
#include "types.h"
#ifdef ENABLE_API_SERVER
#define WEB_SERVER_DEFAULT_PORT 24681U
void web_server_configure(boole enable, auint port, auint api_port);
void web_server_tick(void);
void web_server_shutdown(void);
boole web_server_is_enabled(void);
auint web_server_get_port(void);
/* Tell the bridge which port the in-process API server actually bound. */
void web_server_set_api_port(auint api_port);
#else
#define WEB_SERVER_DEFAULT_PORT 24681U
static inline void web_server_configure(boole e,auint p,auint a){(void)e;(void)p;(void)a;}
static inline void web_server_tick(void){}
static inline void web_server_shutdown(void){}
static inline boole web_server_is_enabled(void){return FALSE;}
static inline auint web_server_get_port(void){return WEB_SERVER_DEFAULT_PORT;}
static inline void web_server_set_api_port(auint a){(void)a;}
#endif
#endif
