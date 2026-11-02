/*
 * Host adapter for the portable Uzenet ESP8266 firmware.
 * The protocol, endpoint and game services live in third_party/uzenet.
 * This file replaces GPIO/UART/RTOS scheduling with emulated-cycle scheduling.
 */
#include "cu_esp_uzenet.h"
#ifdef ENABLE_UZENET
#include "cu_esp.h"
#include "cu_avr.h"
#include "cu_types.h"
#include "configfile.h"
#include "third_party/uzenet/include/un_session.h"
#include "third_party/uzenet/include/un_endpoint.h"
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#ifndef _WIN32
#include <fcntl.h>
#endif
#ifdef ENABLE_OPENSSL
#include <openssl/hmac.h>
#include <openssl/rand.h>
#endif

static UnSession un_session;
static UnTrigger un_trigger;
static uint8_t un_tx[UN_ENCODED_MAX], un_tx_used, un_tx_pos;
static uint8_t un_rx[1024];
static unsigned un_rx_head, un_rx_count;
static bool un_exiting, un_ack_low, un_io_failed, un_raw_output;
static bool un_clock_valid, un_servicing, un_entry_pending;
static uint32_t un_entry_at;
static uint32_t un_cycle, un_ms, un_remainder;
static uint32_t un_ack_at, un_last_rx, un_sample_at, un_service_at;

static uint32_t un_now(void){return un_ms;}
static bool un_line_high(void)
{
 cu_state_cpu_t *cpu=cu_avr_get_state();
 /* PA7/GPIO0 has an external pullup. Both endpoints may pull it low. */
 return !(cpu->iors[CU_IO_DDRA]&0x80U) || (cpu->iors[CU_IO_PORTA]&0x80U);
}
static bool un_output_empty(void)
{
 cu_state_esp_t *es=cu_esp_get_state();
 return es->read_buf_pos_in==es->read_buf_pos_out;
}
static void un_raw_write(const uint8_t *bytes,unsigned n)
{
 un_raw_output=true;
 cu_esp_txl((const char *)bytes,n);
 un_raw_output=false;
}
void un_put(uint8_t byte)
{
 if(un_tx_used<sizeof(un_tx))un_tx[un_tx_used++]=byte;
 else un_io_failed=true;
}

#include "cu_esp_uzenet_net.inc"

boole cu_uzenet_enabled(void)
{
 cu_state_esp_t *es=cu_esp_get_state();
 return es->at_firmware_profile==CU_ESP_AT_FW_UZENET &&
        es->serial_route==CU_ESP_SERIAL_ESP_MODULE && es->emulation_model!=0 &&
        es->reset_pin && !cu_esp_rom_blocked();
}
void cu_uzenet_reset(void)
{
 un_net_disconnect();
 memset(&un_session,0,sizeof(un_session));
 memset(&un_trigger,0,sizeof(un_trigger));
 uc_reset(&un_client);ug_reset(&un_game);ue_reset(&un_endpoint);
 un_capture=un_ticket_pending=false;un_control_used=0;
 memset(un_control,0,sizeof(un_control));
 un_inject_count=un_inject_pos=0;un_peer_query=0;
 un_peer_query_at=un_direct_at=0;
 un_tx_used=un_tx_pos=0;un_rx_head=un_rx_count=0;
 un_exiting=un_ack_low=un_io_failed=un_raw_output=false;
 un_clock_valid=un_servicing=un_entry_pending=false;un_entry_at=0;un_ms=un_remainder=0;
 un_ack_at=un_last_rx=un_sample_at=un_service_at=0;
}
static void un_flush_reply(void)
{
 if(!un_tx_used || un_ack_low || un_io_failed || un_ms-un_ack_at<2 ||
    !un_output_empty() || un_line_high())return;
 unsigned n=un_tx_used-un_tx_pos;
 if(n>un_session.tx_batch)n=un_session.tx_batch;
 un_raw_write(un_tx+un_tx_pos,n);
 un_tx_pos+=(uint8_t)n;
 if(un_tx_pos==un_tx_used)un_tx_pos=un_tx_used=0;
}
static void un_exit(void)
{
 uint8_t bytes[UN_DEFERRED_BYTES];
 unsigned count=0;
 un_net_close();
 while(un_deferred_pop(&un_session,&bytes[count]))count++;
 un_session.active=0;un_session_rx_reset(&un_session);
 un_exiting=un_ack_low=un_entry_pending=false;
 memset(&un_trigger,0,sizeof(un_trigger));
 if(count)un_raw_write(bytes,count);
}
void cu_uzenet_tick(auint cycle)
{
 if(!cu_uzenet_enabled() || un_servicing)return;
 un_servicing=true;
 if(!un_clock_valid){un_cycle=cycle;un_clock_valid=true;}
 uint32_t delta=(uint32_t)(cycle-un_cycle);
 /* A lazily serviced UART completion can carry an earlier cycle timestamp.
  * Do not mistake it for a full counter wrap and expire the receive frame. */
 if(delta>0x7fffffffU){un_servicing=false;return;}
 un_cycle=cycle;
 uint64_t scaled=(uint64_t)delta*1000U+un_remainder;
 un_ms+=(uint32_t)(scaled/ESP_UZEBOX_CORE_FREQUENCY);
 un_remainder=(uint32_t)(scaled%ESP_UZEBOX_CORE_FREQUENCY);
 if(un_ms-un_sample_at>=5){
  un_sample_at=un_ms;
  if(!un_session.active && un_trigger_sample(&un_trigger,un_line_high(),un_ms)){
   un_entry_pending=true;un_entry_at=un_ms;
  }
  if(un_entry_pending){
   cu_state_esp_t *es=cu_esp_get_state();
   /* The ESP waits up to 250 ms for existing stock UART output to drain.
    * Retain a valid GPIO request across that drain instead of losing it
    * whenever a Room beacon happens to occupy the output FIFO. */
   if(un_ms-un_entry_at>250 || !un_line_high() || es->write_buf_pos_in ||
      es->unvarnished_bytes || es->uart_tx_shift_valid || es->uart_tx_udr_valid)
    un_entry_pending=false;
   else if(un_output_empty() && es->baud_rate){
    un_entry_pending=false;
    un_session.capabilities=UN_CAPABILITIES|UN_CAP_RELAY|UN_CAP_DIRECT|UN_CAP_GAME|UN_CAP_OOB;
    un_session.tx_batch=UN_PA7_TX_BATCH_FOR_BAUD(es->baud_rate);
    un_io_failed=un_exiting=false;un_tx_used=un_tx_pos=0;
    un_session_enter(&un_session);un_ack_low=true;un_ack_at=un_ms;
   }
  }
 }
 if(un_ms-un_service_at>=UN_PA7_SERVICE_MS){
  un_service_at=un_ms;
  if(un_session.active){
   if(un_ack_low && un_ms-un_ack_at>=UN_PA7_ACK_MS+1){un_ack_low=false;un_ack_at=un_ms;}
   un_flush_reply();
   if(!un_tx_used && un_ms-un_last_rx>250)un_session_rx_reset(&un_session);
   unsigned budget=64;
   while(!un_ack_low && !un_tx_used && !un_exiting && !un_io_failed && un_rx_count && budget--){
    uint8_t byte=un_rx[un_rx_head++];un_rx_head%=sizeof(un_rx);un_rx_count--;
    un_last_rx=un_ms;un_exiting=un_session_byte(&un_session,byte)!=0;
    un_flush_reply();
   }
   if(un_exiting && !un_tx_used && !un_io_failed && un_output_empty() && !un_line_high())un_exit();
  }
  un_net_poll();
  if(un_session.active && !un_ack_low && !un_exiting && !un_io_failed &&
     !un_tx_used && !un_session.rx.count && !un_rx_count){
   if(!ue_service(&un_endpoint,&un_game,&un_client,un_ms))ug_push(&un_game,&un_client);
   un_flush_reply();
  }
 }
 un_servicing=false;
}
auint cu_uzenet_pina(auint value,auint cycle)
{
 if(!cu_uzenet_enabled())return value;
 cu_uzenet_tick(cycle);
 return (value&~0x80U)|((un_line_high()&&!un_ack_low)?0x80U:0U);
}
boole cu_uzenet_uart_write(uint8 byte,auint cycle)
{
 if(!cu_uzenet_enabled())return FALSE;
 cu_uzenet_tick(cycle);
 if(!un_session.active)return FALSE;
 if(un_rx_count==sizeof(un_rx)){un_session_rx_reset(&un_session);un_io_failed=true;}
 else{un_rx[(un_rx_head+un_rx_count)%sizeof(un_rx)]=byte;un_rx_count++;}
 return TRUE;
}
boole cu_uzenet_stock_write(const char *bytes,auint length)
{
 if(un_raw_output || !cu_uzenet_enabled() || (!un_session.active&&!un_capture))return FALSE;
 while(length--){
  if(un_capture)(void)un_net_tap((uint8_t)*bytes++);
  else if(un_defer(&un_session,(const uint8_t *)bytes++,1)!=1)un_io_failed=true;
 }
 return TRUE;
}
auint cu_uzenet_stock_capacity(void)
{
 if(!cu_uzenet_enabled() || !un_session.active)return 4096;
 /* Prefix mismatches can emit three retained bytes plus the current byte. */
 return (UN_DEFERRED_BYTES-un_session.count)/4;
}
#endif
