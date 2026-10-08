#ifndef CU_MASTER_H
#define CU_MASTER_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CU_MASTER_MAX_LISTINGS 32U
#define CU_MASTER_STR_LEN      64U

void cu_master_init(void);
void cu_master_reset(void);
void cu_master_quit(void);
void cu_master_update(auint frame);
typedef struct{
	boole used;
	auint last_seen_frame;
	uint32 instance_id;
	uint32 session_id;
	uint32 source_ip;
	uint16 source_port;
	auint cur_players;
	auint max_players;
	boole is_public;
	boole in_progress;
	char game[CU_MASTER_STR_LEN];
	char name[CU_MASTER_STR_LEN];
	char room[CU_MASTER_STR_LEN];
	char join_host[CU_MASTER_STR_LEN];
	char proto[CU_MASTER_STR_LEN];
	uint16 join_port;
} cu_master_listing_info_t;

void cu_master_request_query(void);
void cu_master_print_browser(void);
void cu_master_set_join_port(auint join_port);
void cu_master_set_player_counts(auint cur_players, auint max_players);
void cu_master_set_in_progress(boole in_progress);
auint cu_master_get_listing_count(void);
boole cu_master_get_listing(auint slot, cu_master_listing_info_t* out);
boole cu_master_is_enabled(void);
boole cu_master_is_query_enabled(void);

#ifdef __cplusplus
}
#endif

#endif
