#ifndef ROLLBACK_H
#define ROLLBACK_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROLLBACK_MAX_PLAYERS	8U
#define ROLLBACK_DEFAULT_WINDOW	12U
#define ROLLBACK_HISTORY_FRAMES	256U
#define ROLLBACK_REMOTE_QUEUE_MAX 128U

#define ROLLBACK_DEVICE_EVENT_DATA_MAX 8U
#define ROLLBACK_DEVICE_EVENTS_PER_FRAME 16U

typedef enum{
	ROLLBACK_DEV_EVT_NONE = 0,
	ROLLBACK_DEV_EVT_TAP_SELECT = 1,
	ROLLBACK_DEV_EVT_TAP_PROBE = 2,
	ROLLBACK_DEV_EVT_KBD_BYTES = 3
} rollback_device_event_type_t;

typedef struct{
	uint8	type;
	uint8	port;
	uint8	slot;
	uint8	len;
	uint16	seq; /* deterministic ordering / dedupe for decoded device events */
	uint8	data[ROLLBACK_DEVICE_EVENT_DATA_MAX];
} rollback_device_event_t;

typedef struct{
	boole	stall;
	boole	predicted_any;
	boole	have_all_inputs;
	auint	missing_mask;
} rollback_prepare_t;

void	rollback_init(void);
void	rollback_reset(void);
void	rollback_set_window(auint frames);
auint	rollback_get_window(void);

void	rollback_submit_local_input(auint frame, auint player, auint buttons);
void	rollback_seed_remote_input(auint player, auint buttons);
boole	rollback_submit_remote_input(auint frame, auint player, auint buttons,
		boole *correction, auint *correction_frame);

boole	rollback_queue_remote_input(auint frame, auint player, auint buttons);
boole	rollback_submit_local_device_event(auint frame, rollback_device_event_t const *ev);
boole	rollback_submit_remote_device_event(auint frame, rollback_device_event_t const *ev,
		boole *correction, auint *correction_frame);
boole	rollback_queue_remote_device_event(auint frame, rollback_device_event_t const *ev);
void	rollback_process_remote_queue(void);

void	rollback_prepare_frame(auint frame, rollback_prepare_t *prep);
boole	rollback_get_buttons(auint frame, auint player, auint *buttons, boole *predicted);
boole	rollback_get_device_events(auint frame, rollback_device_event_t *out_events, auint max_events, auint *out_count);

boole	rollback_has_pending_correction(void);
auint	rollback_get_pending_correction_frame(void);
void	rollback_clear_pending_correction(void);

#ifdef __cplusplus
}
#endif

#endif
