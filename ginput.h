/*
 *  Game input processing
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



#ifndef GINPUT_H
#define GINPUT_H



#include "types.h"



/*
** Initializes input component. Always succeeds (it may just fail to find any
** input device, the emulator however might still run a demo not needing any).
*/
void  ginput_init(void);


/*
** Frees / destroys input component.
*/
void  ginput_quit(void);


/*
** Forwards SDL events to emulated game controllers.
*/
void  ginput_sendevent(SDL_Event const* ev);


/*
** Sets SNES / UZEM style keymapping (TRUE: UZEM)
*/
void  ginput_setkbuzem(boole kmap);


/*
** Retrieves SNES / UZEM style keymapping select (TRUE: UZEM)
*/
boole ginput_iskbuzem(void);


/*
** Sets 1 player / 2 players controller allocation (TRUE: 2 players)
*/
void  ginput_set2palloc(boole al2p);


/*
** Retrieves 1 player / 2 players controller allocation (TRUE: 2 players)
*/
boole ginput_is2palloc(void);

/* Reloads SDL controller mappings from a configured file or directory.
** Returns number of loaded entries, or a negative value on failure / unsupported. */
asint ginput_reload_controllerdb(char const* path);
asint ginput_reload_controllerdb_quiet(char const* path);

/* Host controller enumeration for UI / virtual-device binding */
auint ginput_get_controller_slots(void);
boole ginput_get_controller_present(auint index);
char const* ginput_get_controller_name(auint index);

/* Raw host input snapshots for virtual-device binding */
boole ginput_host_get_controller_buttons(auint index, auint* buttons);
boole ginput_host_get_controller_axis(auint index, auint axis, sint32* value);
auint ginput_host_get_controller_slot_from_instance(SDL_JoystickID jid);
void  ginput_host_set_gui_capture(boole enable, auint index);
boole ginput_host_get_gui_capture(auint* index);
auint ginput_host_get_keyboard_buttons(void);
void  ginput_host_get_mouse_buttons(auint* buttons);
void  ginput_host_consume_mouse_delta(sint32* dx, sint32* dy);

#endif
