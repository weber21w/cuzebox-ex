/*
 *  CUzeBox UART module (AVR-facing UART boundary)
 */

#ifndef CU_UART_H
#define CU_UART_H

#include "types.h"

boole	cu_uart_route_is_host_serial(void);
boole	cu_uart_route_is_tcp_serial(void);
boole	cu_uart_route_is_midi(void);
boole	cu_uart_route_is_loopback(void);
boole	cu_uart_runtime_is_inert(void);
auint	cu_uart_get_bit_cycles(void);
auint	cu_uart_get_frame_bits(void);
auint	cu_uart_get_frame_cycles(void);
void	cu_uart_debug_logic_capture_set(boole tx_enable, boole rx_enable);
void	cu_uart_debug_logic_capture_get(boole* tx_enable, boole* rx_enable);

#endif
