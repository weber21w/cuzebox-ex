/*
 *  Game input processing
 *
 *  Copyright (C) 2016 - 2018
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



#include "ginput.h"
#include <string.h>
#include "cu_ctr.h"
#include "cu_kbd.h"
#include "cu_mouse.h"

extern auint guicore_getflags(void);
extern SDL_Window*   guicore_window;

/* Request Uzem style keymapping */
static boole ginput_kbuzem = FALSE;

/* Directing keyboard input to Player 2 (for two redirect keys) */
static boole ginput_kbp2_0 = FALSE;
static boole ginput_kbp2_1 = FALSE;

/* Request 2 players controller allocation */
static boole ginput_2palloc = FALSE;

/* Raw host-side state used by virtual devices */
static auint ginput_host_kb_buttons = 0U;
static auint ginput_host_ctr_buttons[2] = {0U, 0U};
static sint32 ginput_host_mouse_dx = 0;
static sint32 ginput_host_mouse_dy = 0;
static auint ginput_host_mouse_buttons = 0U;
static boole ginput_gui_capture_enabled = FALSE;
static auint ginput_gui_capture_slot = 0U;

static auint ginput_host_button_bit_from_keysym(int key)
{
 switch (key){
  case SDLK_LEFT:   return CU_CTR_SNES_M_LEFT;
  case SDLK_RIGHT:  return CU_CTR_SNES_M_RIGHT;
  case SDLK_UP:     return CU_CTR_SNES_M_UP;
  case SDLK_DOWN:   return CU_CTR_SNES_M_DOWN;
  case SDLK_q:      return ginput_kbuzem ? 0U : CU_CTR_SNES_M_Y;
  case SDLK_w:      return ginput_kbuzem ? 0U : CU_CTR_SNES_M_X;
  case SDLK_a:      return ginput_kbuzem ? CU_CTR_SNES_M_A : CU_CTR_SNES_M_B;
  case SDLK_s:      return ginput_kbuzem ? CU_CTR_SNES_M_B : CU_CTR_SNES_M_A;
  case SDLK_y:      return ginput_kbuzem ? CU_CTR_SNES_M_Y : 0U;
  case SDLK_z:      return ginput_kbuzem ? CU_CTR_SNES_M_Y : 0U;
  case SDLK_x:      return ginput_kbuzem ? CU_CTR_SNES_M_X : 0U;
  case SDLK_SPACE:  return CU_CTR_SNES_M_SELECT;
  case SDLK_TAB:    return CU_CTR_SNES_M_SELECT;
  case SDLK_RETURN: return CU_CTR_SNES_M_START;
  case SDLK_LSHIFT: return CU_CTR_SNES_M_LSH;
  case SDLK_RSHIFT: return CU_CTR_SNES_M_RSH;
  default:          return 0U;
 }
}

static void ginput_host_set_keyboard_key(int key, boole press)
{
 auint bit = ginput_host_button_bit_from_keysym(key);
 if (bit == 0U){ return; }
 if (press){ ginput_host_kb_buttons |= bit; }
 else{ ginput_host_kb_buttons &= ~bit; }
}


#ifndef USE_SDL1
/* Game controller objects */
static SDL_GameController* ginput_gamectr[2] = {NULL, NULL};

/* Event ID for each game controller */
static auint ginput_gamectr_id[2] = {0U, 0U};

/* Directional moves collected from digital inputs */
static boole ginput_gamectr_ddig[2][4];

/* Directional moves collected from analog inputs */
static boole ginput_gamectr_dana[2][4];

#ifndef HEADLESS
/* Controller name when no name string is available */
static const char ginput_ctr_noname[] = "<no name>";
#endif
/* Game controller DB filename */
static const char ginput_gctr_filename[] = "gamecontrollerdb.txt";
static asint ginput_gctr_loadmappings_path(char const* path, boole report);
#endif



#ifndef USE_SDL1

/*
** Returns controller name string for the given controller. Returns no-name
** string if it is nonexistent.
*/
#ifndef HEADLESS
static const char* ginput_gctr_name(auint i)
{
 const char* tstr;
 if (i >= 2U){ return &(ginput_ctr_noname[0]); }
 if (ginput_gamectr[i] == NULL){ return &(ginput_ctr_noname[0]); }
 tstr = SDL_GameControllerName(ginput_gamectr[i]);
 if (tstr == NULL){ return &(ginput_ctr_noname[0]); }
 return tstr;
}
#endif

static auint ginput_gctr_getslot_from_id(SDL_JoystickID jid)
{
 if (jid == ginput_gamectr_id[0]){ return 0U; }
 if (jid == ginput_gamectr_id[1]){ return 1U; }
 return 2U;
}

static auint ginput_gctr_player_from_slot(auint slot)
{
 if (!ginput_2palloc){ return 0U; }
 if (ginput_gamectr[1] == NULL){ return 1U; }
 if (slot == 1U){ return 1U; }
 return 0U;
}

static void ginput_clear_player_controller_state(auint player)
{
 auint i;
 if (player >= 2U){ return; }
 for (i = 0U; i < 4U; i++){
  ginput_gamectr_ddig[player][i] = FALSE;
  ginput_gamectr_dana[player][i] = FALSE;
 }
 if ((player == 0U) && cu_mouse_get_enabled()){ return; }
 if ((player == 1U) && cu_kbd_get_enabled()){ return; }
 cu_ctr_setsnes_single(player, CU_CTR_SNES_LEFT,  FALSE);
 cu_ctr_setsnes_single(player, CU_CTR_SNES_RIGHT, FALSE);
 cu_ctr_setsnes_single(player, CU_CTR_SNES_UP,    FALSE);
 cu_ctr_setsnes_single(player, CU_CTR_SNES_DOWN,  FALSE);
 cu_ctr_setsnes_single(player, CU_CTR_SNES_A,     FALSE);
 cu_ctr_setsnes_single(player, CU_CTR_SNES_B,     FALSE);
 cu_ctr_setsnes_single(player, CU_CTR_SNES_X,     FALSE);
 cu_ctr_setsnes_single(player, CU_CTR_SNES_Y,     FALSE);
 cu_ctr_setsnes_single(player, CU_CTR_SNES_LSH,   FALSE);
 cu_ctr_setsnes_single(player, CU_CTR_SNES_RSH,   FALSE);
 cu_ctr_setsnes_single(player, CU_CTR_SNES_START, FALSE);
 cu_ctr_setsnes_single(player, CU_CTR_SNES_SELECT,FALSE);
}

static void ginput_host_set_controller_button(auint slot, auint bit, boole press)
{
 if (slot >= 2U){ return; }
 if (press){ ginput_host_ctr_buttons[slot] |= bit; }
 else{ ginput_host_ctr_buttons[slot] &= ~bit; }
}

static void ginput_host_set_controller_dir(auint slot, auint bit, boole press)
{
 ginput_host_set_controller_button(slot, bit, press);
}

/*
** Returns player number (0 or 1) for which the game controller event should
** apply. If there is one controller, it is for Player 2 when 2 player mode is
** enabled.
*/
static auint ginput_gctr_getplayer(SDL_Event const* ev)
{
 SDL_JoystickID jid;
 if (!ginput_2palloc){ return 0U; }
 if (ginput_gamectr[1] == NULL){ return 1U; }
 if (ev->type == SDL_JOYAXISMOTION){ jid = ev->jaxis.which; }
 else                              { jid = ev->cbutton.which; }
 if ( jid != (ginput_gamectr_id[0]) &&
      jid == (ginput_gamectr_id[1]) ){ return 1U; }
 return 0U;
}


/*
** Prints out controller identification for players
*/
static void ginput_gctr_printid(auint player, auint cid)
{
 if (ginput_gamectr[cid] != NULL){
  print_message("Player %u controller id: %u name: %s\n",
      player,
      ginput_gamectr_id[cid],
      ginput_gctr_name(cid));
 }
}


/*
** Prints out player - controller allocation
*/
static void ginput_gctr_printalloc(void)
{
 if (ginput_2palloc){
  if (ginput_gamectr[1] == NULL){
   ginput_gctr_printid(2U, 0U); /* Only controller is for Player 2 */
  }else{
   ginput_gctr_printid(1U, 0U); /* Controller 0 is for Player 1 */
   ginput_gctr_printid(2U, 1U); /* Controller 1 is for Player 2 */
  }
 }else{
  ginput_gctr_printid(1U, 0U); /* Controller 0 is for Player 1 */
  ginput_gctr_printid(1U, 1U); /* Controller 1 is for Player 1 */
 }
}


/*
** Loads game controller config. from path along with optional reporting.
*/
static asint ginput_gctr_loadmappings_path(char const* path, boole report)
{
 asint n;
 if ((path == NULL) || (path[0] == 0)){ return -1; }
 n = SDL_GameControllerAddMappingsFromFile(path);
 if (report){
  print_message("Load game controller mappings:\n    %s\n    ", path);
  if (n < 0){ print_unf("could not open\n"); }
  else{       print_message("contained %i entries\n", n); }
 }
 return n;
}

#endif

static asint ginput_reload_controllerdb_core(char const* path, boole report)
{
#ifndef HEADLESS
#ifndef USE_SDL1
 char  sbuf[1024];
 auint i = 0U;
 auint j = 0U;
 asint n;
 if ((path == NULL) || (path[0] == 0)){ return -1; }
 n = ginput_gctr_loadmappings_path(path, report);
 if (n >= 0){ return n; }
 while ((path[i] != 0) && (i < (sizeof(sbuf) - 1U))){
  sbuf[i] = path[i];
  i++;
 }
 if ((i != 0U) && (sbuf[i - 1U] != '/') && (sbuf[i - 1U] != '\\')){
  if (i >= (sizeof(sbuf) - 1U)){ return n; }
  sbuf[i++] = '/';
 }
 while ((ginput_gctr_filename[j] != 0) && (i < (sizeof(sbuf) - 1U))){
  sbuf[i++] = ginput_gctr_filename[j++];
 }
 sbuf[i] = 0;
 if (strcmp(sbuf, path) == 0){ return n; }
 return ginput_gctr_loadmappings_path(sbuf, report);
#else
 (void)path;
 (void)report;
 return -2;
#endif
#else
 (void)path;
 (void)report;
 return -2;
#endif
}

asint ginput_reload_controllerdb(char const* path)
{
 return ginput_reload_controllerdb_core(path, TRUE);
}

asint ginput_reload_controllerdb_quiet(char const* path)
{
 return ginput_reload_controllerdb_core(path, FALSE);
}

/*
** Initializes input component. Always succeeds (it may just fail to find any
** input device, the emulator however might still run a demo not needing any).
*/
void  ginput_init(void)
{
#ifdef HEADLESS
 return;
#endif
#ifndef USE_SDL1
 auint         i;
 auint         j;
 SDL_Joystick* jtmp;
 char          sbuf[1024];
 char*         bpat;
#endif

#ifndef USE_SDL1

 for (j = 0U; j < 2U; j++){
  ginput_host_ctr_buttons[j] = 0U;
  for (i = 0U; i < 4U; i++){
   ginput_gamectr_ddig[j][i] = FALSE;
   ginput_gamectr_dana[j][i] = FALSE;
  }
 }
 ginput_host_kb_buttons = 0U;
 ginput_host_mouse_dx = 0;
 ginput_host_mouse_dy = 0;
 ginput_host_mouse_buttons = 0U;

 /* Load game controller mappings (if such a file is present) */

 bpat = SDL_GetBasePath();
 i = 0U;
 if (bpat != NULL){
  while (bpat[i] != 0){
   sbuf[i] = bpat[i];
   i++;
   if (i == 1023U){ break; }
  }
  SDL_free(bpat);
 }
 j = 0U;
 while (ginput_gctr_filename[j] != 0){
  if (i == 1023U){ break; }
  sbuf[i] = ginput_gctr_filename[j];
  i++;
  j++;
 }
 sbuf[i] = 0;
 ginput_gctr_loadmappings_path(&(sbuf[0]), TRUE);
#ifdef PATH_GAMECONTROLLERDB
 ginput_gctr_loadmappings_path(PATH_GAMECONTROLLERDB, TRUE);
#endif

 /* Open game controller or controllers */

 j = 0U;
 for (i = 0U; i < SDL_NumJoysticks(); i++){
  if (SDL_IsGameController(i)){
   ginput_gamectr[j] = SDL_GameControllerOpen(i);
   if (ginput_gamectr[j] != NULL){
    jtmp = SDL_GameControllerGetJoystick(ginput_gamectr[j]);
    if (jtmp != NULL){
     ginput_gamectr_id[j] = SDL_JoystickInstanceID(jtmp);
     print_message("Game controller id: %u found: %s\n",
         ginput_gamectr_id[j],
         ginput_gctr_name(j));
     j++;
     if (j >= 2U){ break; }
    }else{
     SDL_GameControllerClose(ginput_gamectr[j]);
     ginput_gamectr[j] = NULL;
    }
   }
  }
 }

 /* If there are 2 game controllers, start in 2 player mode, otherwise assume
 ** 1 player initially. */

 ginput_2palloc = (j >= 2U);
 ginput_gctr_printalloc();

#endif
}



/*
** Frees / destroys input component.
*/
void  ginput_quit(void)
{
#ifndef USE_SDL1
 auint i;
#endif

#ifndef USE_SDL1

 /* Close game controller or controllers */

 for (i = 0U; i < 2U; i++){
  if (ginput_gamectr[i] != NULL){
   SDL_GameControllerClose(ginput_gamectr[i]);
  }
 }
#endif
}



/*
** Forwards SDL events to emulated game controllers.
*/
void  ginput_sendevent(SDL_Event const* ev)
{

 boole press = FALSE;
 auint player = 0U;

 if ((ev->type) == SDL_KEYDOWN || (ev->type) == SDL_KEYUP){
  press = ((ev->type) == SDL_KEYDOWN);
  ginput_host_set_keyboard_key(ev->key.keysym.sym, press);
  press = FALSE;
 }

 /* Keyboard input: Player 1 */
 if(cu_kbd_capture_active() && ((ev->type) == SDL_KEYDOWN || (ev->type) == SDL_KEYUP)){
  cu_kbd_handle_key(ev);
 }else if ( (((ev->type) == SDL_KEYDOWN) ||
      ((ev->type) == SDL_KEYUP))){ /* skip key interpretation if keyboard capture is enabled */
 
 if ((ev->type) == SDL_KEYDOWN){ press = TRUE; }
  if ( (ginput_kbp2_0) ||
       (ginput_kbp2_1) ){ player = 1U; }

  /* Note: For SDL2 the scancode has more sense here, but SDL1 does not
  ** support that. This solution works for now on both. For the Uzem
  ** keymapping both Y and Z triggers SNES_Y, so it remains useful on both a
  ** QWERTY and a QWERTZ keyboard. */

  switch (ev->key.keysym.sym){
   case SDLK_RALT:
    ginput_kbp2_0 = press; /* AltGr down => Keyboard input goes to P2 */
    break;
   case SDLK_LALT:
    ginput_kbp2_1 = press; /* Alt down => Keyboard input goes to P2 */
    break;
   case SDLK_LEFT:
#ifndef USE_SDL1
    ginput_gamectr_ddig[player][0] = press;
#else
    cu_ctr_setsnes_single(player, CU_CTR_SNES_LEFT, press);
#endif
    break;
   case SDLK_RIGHT:
#ifndef USE_SDL1
    ginput_gamectr_ddig[player][1] = press;
#else
    cu_ctr_setsnes_single(player, CU_CTR_SNES_RIGHT, press);
#endif
    break;
   case SDLK_UP:
#ifndef USE_SDL1
    ginput_gamectr_ddig[player][2] = press;
#else
    cu_ctr_setsnes_single(player, CU_CTR_SNES_UP, press);
#endif
    break;
   case SDLK_DOWN:
#ifndef USE_SDL1
    ginput_gamectr_ddig[player][3] = press;
#else
    cu_ctr_setsnes_single(player, CU_CTR_SNES_DOWN, press);
#endif
    break;
   case SDLK_q:
    if (!ginput_kbuzem){ cu_ctr_setsnes_single(player, CU_CTR_SNES_Y, press); }
    break;
   case SDLK_w:
    if (!ginput_kbuzem){ cu_ctr_setsnes_single(player, CU_CTR_SNES_X, press); }
    break;
   case SDLK_a:
    if (!ginput_kbuzem){ cu_ctr_setsnes_single(player, CU_CTR_SNES_B, press); }
    if ( ginput_kbuzem){ cu_ctr_setsnes_single(player, CU_CTR_SNES_A, press); }
    break;
   case SDLK_s:
    if (!ginput_kbuzem){ cu_ctr_setsnes_single(player, CU_CTR_SNES_A, press); }
    if ( ginput_kbuzem){ cu_ctr_setsnes_single(player, CU_CTR_SNES_B, press); }
    break;
   case SDLK_y:
    if ( ginput_kbuzem){ cu_ctr_setsnes_single(player, CU_CTR_SNES_Y, press); }
    break;
   case SDLK_z:
    if ( ginput_kbuzem){ cu_ctr_setsnes_single(player, CU_CTR_SNES_Y, press); }
    break;
   case SDLK_x:
    if ( ginput_kbuzem){ cu_ctr_setsnes_single(player, CU_CTR_SNES_X, press); }
    break;
   case SDLK_SPACE:
    cu_ctr_setsnes_single(player, CU_CTR_SNES_SELECT, press);
    break;
   case SDLK_TAB:
    cu_ctr_setsnes_single(player, CU_CTR_SNES_SELECT, press);
    break;
   case SDLK_RETURN:
    cu_ctr_setsnes_single(player, CU_CTR_SNES_START, press);
    break;
   case SDLK_LSHIFT:
    cu_ctr_setsnes_single(player, CU_CTR_SNES_LSH, press);
    break;
   case SDLK_RSHIFT:
    cu_ctr_setsnes_single(player, CU_CTR_SNES_RSH, press);
    break;
   default:
    break;
  }

 }

#ifndef USE_SDL1

 /* Controller input */

 if ( ((ev->type) == SDL_CONTROLLERBUTTONDOWN) ||
      ((ev->type) == SDL_CONTROLLERBUTTONUP)){

  auint host_slot = ginput_gctr_getslot_from_id(ev->cbutton.which);
  player = ginput_gctr_getplayer(ev);
  if ((ev->type) == SDL_CONTROLLERBUTTONDOWN){ press = TRUE; }

  switch (ev->cbutton.button){
   case SDL_CONTROLLER_BUTTON_DPAD_LEFT:          ginput_host_set_controller_dir(host_slot, CU_CTR_SNES_M_LEFT, press); break;
   case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:         ginput_host_set_controller_dir(host_slot, CU_CTR_SNES_M_RIGHT, press); break;
   case SDL_CONTROLLER_BUTTON_DPAD_UP:            ginput_host_set_controller_dir(host_slot, CU_CTR_SNES_M_UP, press); break;
   case SDL_CONTROLLER_BUTTON_DPAD_DOWN:          ginput_host_set_controller_dir(host_slot, CU_CTR_SNES_M_DOWN, press); break;
   case SDL_CONTROLLER_BUTTON_Y:                  ginput_host_set_controller_button(host_slot, CU_CTR_SNES_M_X, press); break;
   case SDL_CONTROLLER_BUTTON_X:                  ginput_host_set_controller_button(host_slot, CU_CTR_SNES_M_Y, press); break;
   case SDL_CONTROLLER_BUTTON_B:                  ginput_host_set_controller_button(host_slot, CU_CTR_SNES_M_A, press); break;
   case SDL_CONTROLLER_BUTTON_A:                  ginput_host_set_controller_button(host_slot, CU_CTR_SNES_M_B, press); break;
   case SDL_CONTROLLER_BUTTON_BACK:               ginput_host_set_controller_button(host_slot, CU_CTR_SNES_M_SELECT, press); break;
   case SDL_CONTROLLER_BUTTON_GUIDE:              ginput_host_set_controller_button(host_slot, CU_CTR_SNES_M_SELECT, press); break;
   case SDL_CONTROLLER_BUTTON_START:              ginput_host_set_controller_button(host_slot, CU_CTR_SNES_M_START, press); break;
   case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:       ginput_host_set_controller_button(host_slot, CU_CTR_SNES_M_LSH, press); break;
   case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:      ginput_host_set_controller_button(host_slot, CU_CTR_SNES_M_RSH, press); break;
   default: break;
  }

  if(!cu_kbd_get_enabled() || player != 1){ /* ignore P2 input if the keyboard dongle is enabled */

   switch (ev->cbutton.button){
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
     ginput_gamectr_ddig[player][0] = press;
     ginput_host_set_controller_dir(host_slot, CU_CTR_SNES_M_LEFT, press);
     break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
     ginput_gamectr_ddig[player][1] = press;
     ginput_host_set_controller_dir(host_slot, CU_CTR_SNES_M_RIGHT, press);
     break;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
     ginput_gamectr_ddig[player][2] = press;
     ginput_host_set_controller_dir(host_slot, CU_CTR_SNES_M_UP, press);
     break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
     ginput_gamectr_ddig[player][3] = press;
     ginput_host_set_controller_dir(host_slot, CU_CTR_SNES_M_DOWN, press);
     break;
    case SDL_CONTROLLER_BUTTON_Y: /* X-Y swapped due to SDL2 layout corresponding to XBox360 */
     cu_ctr_setsnes_single(player, CU_CTR_SNES_X, press);
     ginput_host_set_controller_button(host_slot, CU_CTR_SNES_M_X, press);
     break;
    case SDL_CONTROLLER_BUTTON_X: /* X-Y swapped due to SDL2 layout corresponding to XBox360 */
     cu_ctr_setsnes_single(player, CU_CTR_SNES_Y, press);
     ginput_host_set_controller_button(host_slot, CU_CTR_SNES_M_Y, press);
     break;
    case SDL_CONTROLLER_BUTTON_B: /* A-B swapped due to SDL2 layout corresponding to XBox360 */
     cu_ctr_setsnes_single(player, CU_CTR_SNES_A, press);
     ginput_host_set_controller_button(host_slot, CU_CTR_SNES_M_A, press);
     break;
    case SDL_CONTROLLER_BUTTON_A: /* A-B swapped due to SDL2 layout corresponding to XBox360 */
     cu_ctr_setsnes_single(player, CU_CTR_SNES_B, press);
     ginput_host_set_controller_button(host_slot, CU_CTR_SNES_M_B, press);
     break;
    case SDL_CONTROLLER_BUTTON_BACK:
     cu_ctr_setsnes_single(player, CU_CTR_SNES_SELECT, press);
     ginput_host_set_controller_button(host_slot, CU_CTR_SNES_M_SELECT, press);
     break;
    case SDL_CONTROLLER_BUTTON_GUIDE:
     cu_ctr_setsnes_single(player, CU_CTR_SNES_SELECT, press);
     ginput_host_set_controller_button(host_slot, CU_CTR_SNES_M_SELECT, press);
     break;
    case SDL_CONTROLLER_BUTTON_START:
     cu_ctr_setsnes_single(player, CU_CTR_SNES_START, press);
     ginput_host_set_controller_button(host_slot, CU_CTR_SNES_M_START, press);
     break;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
     cu_ctr_setsnes_single(player, CU_CTR_SNES_LSH, press);
     ginput_host_set_controller_button(host_slot, CU_CTR_SNES_M_LSH, press);
     break;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
     cu_ctr_setsnes_single(player, CU_CTR_SNES_RSH, press);
     ginput_host_set_controller_button(host_slot, CU_CTR_SNES_M_RSH, press);
     break;
    default:
     break;
   }
  }
 }

 /* Joystick axis input. This complements controller input, necessary due to
 ** the poor API not being able to handle game controllers properly. If SDL2
 ** will be fixed at some point in the future, this may be removed (along with
 ** related patch in ginput_gctr_getplayer()). */

 if ( ((ev->type) == SDL_JOYAXISMOTION) ){ /* ignore P2 input if the keyboard dongle is enabled */

  auint host_slot = ginput_gctr_getslot_from_id(ev->jaxis.which);
  if ((ev->jaxis.axis) == 0U){
   ginput_host_set_controller_dir(host_slot, CU_CTR_SNES_M_LEFT,  ((ev->jaxis.value) <= -16384));
   ginput_host_set_controller_dir(host_slot, CU_CTR_SNES_M_RIGHT, ((ev->jaxis.value) >=  16384));
  }
  if ((ev->jaxis.axis) == 1U){
   ginput_host_set_controller_dir(host_slot, CU_CTR_SNES_M_UP,    ((ev->jaxis.value) <= -16384));
   ginput_host_set_controller_dir(host_slot, CU_CTR_SNES_M_DOWN,  ((ev->jaxis.value) >=  16384));
  }
  player = ginput_gctr_getplayer(ev);
  if ((!ginput_gui_capture_enabled || (host_slot >= 2U) || (host_slot != ginput_gui_capture_slot)) && (!cu_kbd_get_enabled() || player != 1)) {
   if ((ev->jaxis.axis) == 0U){ /* X axis: Left and Right */
    press = ((ev->jaxis.value) <= -16384);
    ginput_gamectr_dana[player][0] = press;
    ginput_host_set_controller_dir(host_slot, CU_CTR_SNES_M_LEFT, press);
    press = ((ev->jaxis.value) >=  16384);
    ginput_gamectr_dana[player][1] = press;
    ginput_host_set_controller_dir(host_slot, CU_CTR_SNES_M_RIGHT, press);
   }

   if ((ev->jaxis.axis) == 1U){ /* Y axis: Up and Down */
    press = ((ev->jaxis.value) <= -16384);
    ginput_gamectr_dana[player][2] = press;
    ginput_host_set_controller_dir(host_slot, CU_CTR_SNES_M_UP, press);
    press = ((ev->jaxis.value) >=  16384);
    ginput_gamectr_dana[player][3] = press;
    ginput_host_set_controller_dir(host_slot, CU_CTR_SNES_M_DOWN, press);
   }
  }
 }

 /* Mouse axis input. Raw host motion is accumulated for virtual devices. */
 if ((ev->type) == SDL_MOUSEMOTION){
  sint32 mdx = ev->motion.xrel;
  sint32 mdy = ev->motion.yrel;
  uint8 msc = cu_mouse_get_scale();
  ginput_host_mouse_dx += (mdx >> msc);
  ginput_host_mouse_dy += (mdy >> msc);
  ginput_host_mouse_buttons = ev->motion.state;

  if (cu_mouse_get_enabled()){ /* legacy direct SNES mouse path */
   cu_mouse_set_buttons((uint8)ginput_host_mouse_buttons);
   cu_mouse_set_position(mdx >> msc, mdy >> msc);

   if (guicore_getflags() & 0x0001U){ /* GUICORE_FULLSCREEN HACK TODO */
    SDL_WarpMouseInWindow(guicore_window,400,300); /* keep mouse centered so it doesn't get stuck on the edge of the screen... */
   }
  }
 }else if ((ev->type) == SDL_MOUSEBUTTONDOWN || (ev->type) == SDL_MOUSEBUTTONUP){
  int mx, my;
  (void)my;
  ginput_host_mouse_buttons = SDL_GetMouseState(&mx, &my);
  if (cu_mouse_get_enabled()){
   cu_mouse_set_buttons((uint8)ginput_host_mouse_buttons);
  }
 }


 for (player = 0U; player < 2U; player ++){
  if (player == 0U && cu_mouse_get_enabled()){ continue; }
  if (player == 1U && cu_kbd_get_enabled()){ continue; }
  cu_ctr_setsnes_single(player, CU_CTR_SNES_LEFT,  ginput_gamectr_ddig[player][0] || ginput_gamectr_dana[player][0]);
  cu_ctr_setsnes_single(player, CU_CTR_SNES_RIGHT, ginput_gamectr_ddig[player][1] || ginput_gamectr_dana[player][1]);
  cu_ctr_setsnes_single(player, CU_CTR_SNES_UP,    ginput_gamectr_ddig[player][2] || ginput_gamectr_dana[player][2]);
  cu_ctr_setsnes_single(player, CU_CTR_SNES_DOWN,  ginput_gamectr_ddig[player][3] || ginput_gamectr_dana[player][3]);
 }

#endif

}



boole ginput_host_get_controller_buttons(auint index, auint* buttons)
{
#ifndef USE_SDL1
 if ((buttons == NULL) || (index >= 2U) || (ginput_gamectr[index] == NULL)){
  return FALSE;
 }
 *buttons = ginput_host_ctr_buttons[index];
 return TRUE;
#else
 (void)index;
 (void)buttons;
 return FALSE;
#endif
}

boole ginput_host_get_controller_axis(auint index, auint axis, sint32* value)
{
#ifndef USE_SDL1
 SDL_GameControllerAxis sax = SDL_CONTROLLER_AXIS_LEFTX;
 if ((value == NULL) || (index >= 2U) || (ginput_gamectr[index] == NULL)){
  return FALSE;
 }
 if (axis == 1U){ sax = SDL_CONTROLLER_AXIS_LEFTY; }
 else if (axis != 0U){ return FALSE; }
 *value = (sint32)SDL_GameControllerGetAxis(ginput_gamectr[index], sax);
 return TRUE;
#else
 (void)index;
 (void)axis;
 (void)value;
 return FALSE;
#endif
}

auint ginput_host_get_controller_slot_from_instance(SDL_JoystickID jid)
{
#ifndef USE_SDL1
 return ginput_gctr_getslot_from_id(jid);
#else
 (void)jid;
 return 2U;
#endif
}

void ginput_host_set_gui_capture(boole enable, auint index)
{
#ifndef USE_SDL1
 if ((!enable) || (index >= 2U) || (ginput_gamectr[index] == NULL)){
  if (ginput_gui_capture_enabled){
   ginput_clear_player_controller_state(ginput_gctr_player_from_slot(ginput_gui_capture_slot));
  }
  ginput_gui_capture_enabled = FALSE;
  ginput_gui_capture_slot = 0U;
  return;
 }
 if ((!ginput_gui_capture_enabled) || (ginput_gui_capture_slot != index)){
  if (ginput_gui_capture_enabled){
   ginput_clear_player_controller_state(ginput_gctr_player_from_slot(ginput_gui_capture_slot));
  }
  ginput_clear_player_controller_state(ginput_gctr_player_from_slot(index));
 }
 ginput_gui_capture_enabled = TRUE;
 ginput_gui_capture_slot = index;
#else
 (void)enable;
 (void)index;
#endif
}

boole ginput_host_get_gui_capture(auint* index)
{
#ifndef USE_SDL1
 if (!ginput_gui_capture_enabled){ return FALSE; }
 if (index != NULL){ *index = ginput_gui_capture_slot; }
 return TRUE;
#else
 (void)index;
 return FALSE;
#endif
}

auint ginput_host_get_keyboard_buttons(void)
{
 return ginput_host_kb_buttons;
}

void ginput_host_get_mouse_buttons(auint* buttons)
{
 auint raw = 0U;
 if (ginput_host_mouse_buttons & SDL_BUTTON_LMASK){ raw |= (1U << 0); }
 if (ginput_host_mouse_buttons & SDL_BUTTON_RMASK){ raw |= (1U << 1); }
 if (ginput_host_mouse_buttons & SDL_BUTTON_MMASK){ raw |= (1U << 2); }
#ifdef SDL_BUTTON_X1MASK
 if (ginput_host_mouse_buttons & SDL_BUTTON_X1MASK){ raw |= (1U << 3); }
#endif
#ifdef SDL_BUTTON_X2MASK
 if (ginput_host_mouse_buttons & SDL_BUTTON_X2MASK){ raw |= (1U << 4); }
#endif
 if (buttons != NULL){
  *buttons = raw;
 }
}

void ginput_host_consume_mouse_delta(sint32* dx, sint32* dy)
{
 if (dx != NULL){ *dx = ginput_host_mouse_dx; }
 if (dy != NULL){ *dy = ginput_host_mouse_dy; }
 ginput_host_mouse_dx = 0;
 ginput_host_mouse_dy = 0;
}

/*
** Sets SNES / UZEM style keymapping (TRUE: UZEM)
*/
void  ginput_setkbuzem(boole kmap)
{
 ginput_kbuzem = kmap;
}



/*
** Retrieves SNES / UZEM style keymapping select (TRUE: UZEM)
*/
boole ginput_iskbuzem(void)
{
 return ginput_kbuzem;
}



/*
** Sets 1 player / 2 players controller allocation (TRUE: 2 players)
*/
void  ginput_set2palloc(boole al2p)
{
#ifndef USE_SDL1
 if (al2p != ginput_2palloc){
  ginput_2palloc = al2p;
  ginput_gctr_printalloc();
 }
#else
 ginput_2palloc = al2p;
#endif
}



/*
** Retrieves 1 player / 2 players controller allocation (TRUE: 2 players)
*/
boole ginput_is2palloc(void)
{
 return ginput_2palloc;
}

/*
** Host controller enumeration for UI / virtual-device binding.
*/
auint ginput_get_controller_slots(void)
{
#ifndef USE_SDL1
 return 2U;
#else
 return 0U;
#endif
}

boole ginput_get_controller_present(auint index)
{
#ifndef USE_SDL1
 if (index >= 2U){ return FALSE; }
 return (ginput_gamectr[index] != NULL) ? TRUE : FALSE;
#else
 (void)index;
 return FALSE;
#endif
}

char const* ginput_get_controller_name(auint index)
{
#ifndef USE_SDL1
#ifndef HEADLESS
 return ginput_gctr_name(index);
#else
 (void)index;
 return "<controller>";
#endif
#else
 (void)index;
 return "<controller>";
#endif
}
