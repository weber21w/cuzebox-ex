/*
 *  MIDI backends (host MIDI + virtual MIDI device)
 *
 *  Copyright (C) 2016
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
 */

#ifndef MIDI_H
#define MIDI_H

#include "types.h"

sint32	cu_esp_host_midi_start(void);
void	cu_esp_host_midi_end(void);
void	cu_esp_host_midi_write(uint8 c);
uint8	cu_esp_host_midi_read(void);
auint	cu_esp_host_midi_rx_bytes_ready(void);
boole	cu_esp_host_midi_supported(void);
void	cu_esp_host_midi_refresh_ports(void);
auint	cu_esp_host_midi_get_port_count(void);
char const* cu_esp_host_midi_get_port_name(auint idx);
char const* cu_esp_host_midi_get_port_label(auint idx);

sint32	cu_esp_virtual_midi_start(void);
void	cu_esp_virtual_midi_end(void);
void	cu_esp_virtual_midi_write(uint8 c);
uint8	cu_esp_virtual_midi_read(void);
auint	cu_esp_virtual_midi_rx_bytes_ready(void);
boole	cu_esp_virtual_midi_supported(void);

sint32	cu_esp_serial_midi_start(auint route);
void	cu_esp_serial_midi_end(void);
void	cu_esp_serial_midi_write(uint8 c);
uint8	cu_esp_serial_midi_read(void);
auint	cu_esp_serial_midi_rx_bytes_ready(void);
boole	cu_esp_serial_midi_supported(auint route);

#endif
