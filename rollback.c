#include "rollback.h"

#include <string.h>

typedef struct{
	boole	valid;
	boole	predicted;
	auint	buttons;
} rollback_input_t;

typedef struct{
	auint	frame;
	rollback_input_t	player[ROLLBACK_MAX_PLAYERS];
	auint	dev_event_count;
	rollback_device_event_t	dev_events[ROLLBACK_DEVICE_EVENTS_PER_FRAME];
} rollback_frame_t;

typedef struct{
	boole	used;
	auint	frame;
	auint	player;
	auint	buttons;
} rollback_remote_q_t;

typedef struct{
	boole	used;
	auint	frame;
	rollback_device_event_t ev;
} rollback_remote_dev_q_t;

typedef struct{
	rollback_frame_t	history[ROLLBACK_HISTORY_FRAMES];
	rollback_remote_q_t	remote_q[ROLLBACK_REMOTE_QUEUE_MAX];
	rollback_remote_dev_q_t	remote_dev_q[ROLLBACK_REMOTE_QUEUE_MAX];
	auint	remote_q_r;
	auint	remote_q_w;
	auint	remote_dev_q_r;
	auint	remote_dev_q_w;
	auint	window;
	boole	pending_correction;
	auint	pending_correction_frame;
	boole	have_seed[ROLLBACK_MAX_PLAYERS];
	auint	seed_buttons[ROLLBACK_MAX_PLAYERS];
	boole	have_confirmed_frame[ROLLBACK_MAX_PLAYERS];
	auint	confirmed_frame[ROLLBACK_MAX_PLAYERS];
	boole	have_prepared_frame;
	auint	last_prepared_frame;
} rollback_state_t;

static rollback_state_t rollback_state;

static rollback_frame_t* rollback_get_frame(auint frame)
{
	rollback_frame_t *ent = &(rollback_state.history[frame % ROLLBACK_HISTORY_FRAMES]);
	if (ent->frame != frame){
		memset(ent, 0, sizeof(*ent));
		ent->frame = frame;
	}
	return ent;
}

static boole rollback_dev_event_equal(rollback_device_event_t const *a, rollback_device_event_t const *b)
{
	auint i;
	if ((a == NULL) || (b == NULL)){ return FALSE; }
	if ((a->seq != b->seq) || (a->type != b->type) || (a->port != b->port) || (a->slot != b->slot) || (a->len != b->len)){ return FALSE; }
	for (i = 0U; i < a->len && i < ROLLBACK_DEVICE_EVENT_DATA_MAX; ++i){
		if (a->data[i] != b->data[i]){ return FALSE; }
	}
	return TRUE;
}

static void rollback_frame_sort_device_events(rollback_frame_t *ent)
{
	auint i;
	auint j;
	if ((ent == NULL) || (ent->dev_event_count < 2U)){
		return;
	}
	for (i = 0U; i + 1U < ent->dev_event_count; ++i){
		for (j = i + 1U; j < ent->dev_event_count; ++j){
			uint16 ai = ent->dev_events[i].seq;
			uint16 aj = ent->dev_events[j].seq;
			if ((aj != 0U) && ((ai == 0U) || (aj < ai))){
				rollback_device_event_t tmp = ent->dev_events[i];
				ent->dev_events[i] = ent->dev_events[j];
				ent->dev_events[j] = tmp;
			}
		}
	}
}

static boole rollback_frame_add_device_event(rollback_frame_t *ent, rollback_device_event_t const *ev)
{
	auint i;
	if ((ent == NULL) || (ev == NULL)){ return FALSE; }
	for (i = 0U; i < ent->dev_event_count; ++i){
		if (rollback_dev_event_equal(&(ent->dev_events[i]), ev)){
			return TRUE;
		}
	}
	if (ent->dev_event_count >= ROLLBACK_DEVICE_EVENTS_PER_FRAME){
		return FALSE;
	}
	ent->dev_events[ent->dev_event_count++] = *ev;
	rollback_frame_sort_device_events(ent);
	return TRUE;
}

void rollback_init(void)
{
	memset(&rollback_state, 0, sizeof(rollback_state));
	rollback_state.window = ROLLBACK_DEFAULT_WINDOW;
}

void rollback_reset(void)
{
	auint keep_window = rollback_state.window;
	rollback_init();
	if (keep_window != 0U){
		rollback_state.window = keep_window;
	}
}

void rollback_set_window(auint frames)
{
	if (frames == 0U){
		frames = 1U;
	}
	rollback_state.window = frames;
}

auint rollback_get_window(void)
{
	return rollback_state.window;
}

void rollback_submit_local_input(auint frame, auint player, auint buttons)
{
	rollback_frame_t *ent;
	if (player >= ROLLBACK_MAX_PLAYERS){
		return;
	}
	ent = rollback_get_frame(frame);
	ent->player[player].valid = TRUE;
	ent->player[player].predicted = FALSE;
	ent->player[player].buttons = buttons;
	rollback_state.have_confirmed_frame[player] = TRUE;
	rollback_state.confirmed_frame[player] = frame;
}

void rollback_seed_remote_input(auint player, auint buttons)
{
	if (player >= ROLLBACK_MAX_PLAYERS){
		return;
	}
	rollback_state.have_seed[player] = TRUE;
	rollback_state.seed_buttons[player] = buttons;
	if (!rollback_state.have_confirmed_frame[player]){
		rollback_state.have_confirmed_frame[player] = TRUE;
		rollback_state.confirmed_frame[player] = 0U;
	}
}

boole rollback_submit_local_device_event(auint frame, rollback_device_event_t const *ev)
{
	rollback_frame_t *ent = rollback_get_frame(frame);
	return rollback_frame_add_device_event(ent, ev);
}

boole rollback_submit_remote_device_event(auint frame, rollback_device_event_t const *ev,
		boole *correction, auint *correction_frame)
{
	rollback_frame_t *ent;
	boole added;
	boole corr = FALSE;
	ent = rollback_get_frame(frame);
	added = rollback_frame_add_device_event(ent, ev);
	if (added && rollback_state.have_prepared_frame && (frame <= rollback_state.last_prepared_frame)) {
		corr = TRUE;
		if ((!rollback_state.pending_correction) || (frame < rollback_state.pending_correction_frame)){
			rollback_state.pending_correction = TRUE;
			rollback_state.pending_correction_frame = frame;
		}
	}
	if (correction != NULL){ *correction = corr; }
	if (correction_frame != NULL){ *correction_frame = corr ? frame : 0U; }
	return added;
}

boole rollback_queue_remote_device_event(auint frame, rollback_device_event_t const *ev)
{
	rollback_remote_dev_q_t *slot;
	auint next_w = (rollback_state.remote_dev_q_w + 1U) % ROLLBACK_REMOTE_QUEUE_MAX;
	if ((ev == NULL) || (next_w == rollback_state.remote_dev_q_r)){
		return FALSE;
	}
	slot = &(rollback_state.remote_dev_q[rollback_state.remote_dev_q_w]);
	slot->used = TRUE;
	slot->frame = frame;
	slot->ev = *ev;
	rollback_state.remote_dev_q_w = next_w;
	return TRUE;
}

boole rollback_submit_remote_input(auint frame, auint player, auint buttons,
		boole *correction, auint *correction_frame)
{
	rollback_frame_t *ent;
	boole corr = FALSE;

	if (player >= ROLLBACK_MAX_PLAYERS){
		return FALSE;
	}

	ent = rollback_get_frame(frame);
	if (ent->player[player].valid){
		if (ent->player[player].predicted && (ent->player[player].buttons != buttons)){
			corr = TRUE;
			if ((!rollback_state.pending_correction) || (frame < rollback_state.pending_correction_frame)){
				rollback_state.pending_correction = TRUE;
				rollback_state.pending_correction_frame = frame;
			}
		}
	}

	ent->player[player].valid = TRUE;
	ent->player[player].predicted = FALSE;
	ent->player[player].buttons = buttons;
	rollback_state.have_seed[player] = TRUE;
	rollback_state.seed_buttons[player] = buttons;
	if ((!rollback_state.have_confirmed_frame[player]) || (frame > rollback_state.confirmed_frame[player])){
		rollback_state.confirmed_frame[player] = frame;
	}
	rollback_state.have_confirmed_frame[player] = TRUE;

	if (correction != NULL){
		*correction = corr;
	}
	if (correction_frame != NULL){
		*correction_frame = frame;
	}
	return TRUE;
}

boole rollback_queue_remote_input(auint frame, auint player, auint buttons)
{
	rollback_remote_q_t *slot;
	auint next_w;

	next_w = (rollback_state.remote_q_w + 1U) % ROLLBACK_REMOTE_QUEUE_MAX;
	if (next_w == rollback_state.remote_q_r){
		return FALSE;
	}
	slot = &(rollback_state.remote_q[rollback_state.remote_q_w]);
	slot->used = TRUE;
	slot->frame = frame;
	slot->player = player;
	slot->buttons = buttons;
	rollback_state.remote_q_w = next_w;
	return TRUE;
}

void rollback_process_remote_queue(void)
{
	while (rollback_state.remote_q_r != rollback_state.remote_q_w){
		rollback_remote_q_t *slot = &(rollback_state.remote_q[rollback_state.remote_q_r]);
		if (slot->used){
			(void)rollback_submit_remote_input(slot->frame, slot->player, slot->buttons, NULL, NULL);
			slot->used = FALSE;
		}
		rollback_state.remote_q_r = (rollback_state.remote_q_r + 1U) % ROLLBACK_REMOTE_QUEUE_MAX;
	}
	while (rollback_state.remote_dev_q_r != rollback_state.remote_dev_q_w){
		rollback_remote_dev_q_t *slot = &(rollback_state.remote_dev_q[rollback_state.remote_dev_q_r]);
		if (slot->used){
			(void)rollback_submit_remote_device_event(slot->frame, &(slot->ev), NULL, NULL);
			slot->used = FALSE;
		}
		rollback_state.remote_dev_q_r = (rollback_state.remote_dev_q_r + 1U) % ROLLBACK_REMOTE_QUEUE_MAX;
	}
}

void rollback_prepare_frame(auint frame, rollback_prepare_t *prep)
{
	rollback_frame_t *ent;
	auint player;

	if (prep != NULL){
		memset(prep, 0, sizeof(*prep));
	}

	ent = rollback_get_frame(frame);
	rollback_state.have_prepared_frame = TRUE;
	rollback_state.last_prepared_frame = frame;
	for (player = 0U; player < ROLLBACK_MAX_PLAYERS; player++){
		if (ent->player[player].valid){
			if (prep != NULL && ent->player[player].predicted){
				prep->predicted_any = TRUE;
			}
			continue;
		}

		if (rollback_state.have_seed[player]){
			boole allow_predict = FALSE;
			if (rollback_state.have_confirmed_frame[player]){
				auint max_predict_frame = rollback_state.confirmed_frame[player] + rollback_state.window;
				if (frame <= max_predict_frame){
					allow_predict = TRUE;
				}
			}
			if (allow_predict){
				ent->player[player].valid = TRUE;
				ent->player[player].predicted = TRUE;
				ent->player[player].buttons = rollback_state.seed_buttons[player];
				if (prep != NULL){
					prep->predicted_any = TRUE;
				}
			}else if (prep != NULL){
				prep->stall = TRUE;
				prep->missing_mask |= ((auint)1U << player);
			}
		}else{
			if (prep != NULL){
				prep->stall = TRUE;
				prep->missing_mask |= ((auint)1U << player);
			}
		}
	}

	if (prep != NULL){
		prep->have_all_inputs = !prep->stall;
	}
}

boole rollback_get_buttons(auint frame, auint player, auint *buttons, boole *predicted)
{
	rollback_frame_t *ent;
	if (player >= ROLLBACK_MAX_PLAYERS){
		return FALSE;
	}
	ent = rollback_get_frame(frame);
	if (!ent->player[player].valid){
		return FALSE;
	}
	if (buttons != NULL){
		*buttons = ent->player[player].buttons;
	}
	if (predicted != NULL){
		*predicted = ent->player[player].predicted;
	}
	return TRUE;
}


boole rollback_get_device_events(auint frame, rollback_device_event_t *out_events, auint max_events, auint *out_count)
{
	rollback_frame_t *ent = rollback_get_frame(frame);
	auint i, count = ent->dev_event_count;
	if (out_count != NULL){ *out_count = count; }
	if ((out_events == NULL) || (max_events == 0U)){
		return (count != 0U);
	}
	if (count > max_events){ count = max_events; }
	for (i = 0U; i < count; ++i){ out_events[i] = ent->dev_events[i]; }
	return (count != 0U);
}

boole rollback_has_pending_correction(void)
{
	return rollback_state.pending_correction;
}

auint rollback_get_pending_correction_frame(void)
{
	return rollback_state.pending_correction_frame;
}

void rollback_clear_pending_correction(void)
{
	rollback_state.pending_correction = FALSE;
	rollback_state.pending_correction_frame = 0U;
}
