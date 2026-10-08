#ifndef SAVESTATE_H
#define SAVESTATE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAVESTATE_SLOT_COUNT		10U
#define SAVESTATE_DEFAULT_CHECKPOINTS	32U

typedef struct{
	uint8	*data;
	auint	size;
} savestate_blob_t;

void	savestate_init(void);
void	savestate_shutdown(void);

void	savestate_set_slot(auint slot);
auint	savestate_get_slot(void);

boole	savestate_capture_to_mem(savestate_blob_t *blob);
void	savestate_free_blob(savestate_blob_t *blob);
boole	savestate_restore_from_mem(savestate_blob_t const *blob);

boole	savestate_save_slot(auint slot);
boole	savestate_load_slot(auint slot);
boole	savestate_slot_exists(auint slot);

void	savestate_checkpoint_clear(void);
boole	savestate_checkpoint_capture(auint frame);
boole	savestate_checkpoint_restore_at_or_before(auint frame, auint *loaded_frame);

#ifdef __cplusplus
}
#endif

#endif
