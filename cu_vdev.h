/*
 *  Virtual device routing / topology scaffolding
 */

#ifndef CU_VDEV_H
#define CU_VDEV_H

#include "types.h"

#define CU_VDEV_MAX             16U
#define CU_VDEV_NAME_MAX        64U
#define CU_VDEV_MAX_BINDINGS    2U
#define CU_VDEV_INVALID         ((auint)(-1))

#define CU_VDEV_BIND_BUTTONS    0x01U
#define CU_VDEV_BIND_AXES       0x02U
#define CU_VDEV_BIND_TRIGGER    0x04U
#define CU_VDEV_BIND_HAPTIC     0x08U

#define CU_VDEV_OPT_LIGHT_SENSE 0x01U
#define CU_VDEV_OPT_EXTRA_BTNS  0x02U
#define CU_VDEV_OPT_REL_AXES    0x04U

#define CU_VDEV_REMAP_SLOTS      16U

typedef enum{
	CU_VDEV_TYPE_NONE = 0,
	CU_VDEV_TYPE_PAD16,
	CU_VDEV_TYPE_SUPERMOUSE32,
	CU_VDEV_TYPE_KEYBOARD
} cu_vdev_type_t;

typedef enum{
	CU_VDEV_HOST_NONE = 0,
	CU_VDEV_HOST_KEYBOARD,
	CU_VDEV_HOST_MOUSE,
	CU_VDEV_HOST_GAMECONTROLLER,
	CU_VDEV_HOST_JOYSTICK
} cu_vdev_host_type_t;

typedef struct{
	cu_vdev_host_type_t type;
	auint               host_index;
	auint               flags;
	auint               map_low16[CU_VDEV_REMAP_SLOTS];
} cu_vdev_binding_t;

typedef struct{
	boole               used;
	char                name[CU_VDEV_NAME_MAX];
	cu_vdev_type_t      type;
	auint               options;
	boole               haptic_enabled;
	boole               haptic_accept_any;
	auint               haptic_binding;
	auint               haptic_id;
	cu_vdev_binding_t   binding[CU_VDEV_MAX_BINDINGS];

	/* Packet-oriented runtime state */
	auint               low16;   /* Active-high lower 16 bits */
	auint               high16;  /* Active-high upper 16 bits */
	auint               sm_scale_x_pct;
	auint               sm_scale_y_pct;
	auint               sm_deadzone;
	boole               sm_invert_x;
	boole               sm_invert_y;
	sint32              rel_x_accum;
	sint32              rel_y_accum;
	sint32              gun_x;
	sint32              gun_y;
	auint               gun_light;
	auint               gun_trigger;
} cu_vdev_t;

typedef struct{
	cu_vdev_t list[CU_VDEV_MAX];
} cu_state_vdev_t;

void        cu_vdev_get_state(cu_state_vdev_t* state);
void        cu_vdev_set_state(cu_state_vdev_t const* state);

void        cu_vdev_reset(void);
auint       cu_vdev_create(cu_vdev_type_t type, char const* name);
boole       cu_vdev_delete(auint id);
cu_vdev_t*  cu_vdev_get_mut(auint id);
cu_vdev_t const* cu_vdev_get(auint id);
boole       cu_vdev_set_name(auint id, char const* name);
boole       cu_vdev_set_binding(auint id, auint bind_index,
	cu_vdev_host_type_t host_type, auint host_index, auint flags);
boole       cu_vdev_set_packet_words(auint id, auint low16, auint high16);
auint       cu_vdev_default_map_for_host_type(cu_vdev_host_type_t type, auint src_bit);
boole       cu_vdev_reset_binding_map(auint id, auint bind_index);
boole       cu_vdev_reset_supermouse_tuning(auint id);
auint       cu_vdev_duplicate(auint id, char const* name);
boole       cu_vdev_restore(auint id, cu_vdev_t const* src);
boole       cu_vdev_build_low_active_packet(auint id, auint port, uint32* packet, auint* bits);
void        cu_vdev_haptic_pulse(auint id, auint motor_mask, auint duration_ms);
void        cu_vdev_haptic_set_state(auint id, auint bus_id, auint state);
void        cu_vdev_haptic_from_frame(auint id, const uint8* data, auint len);
char const* cu_vdev_type_name(cu_vdev_type_t type);

#endif
