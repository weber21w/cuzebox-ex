/* Exercise the production adapter with a deterministic host/UART boundary. */
#include "../../cu_esp_uzenet.c"
#include <assert.h>

static cu_state_esp_t es;
static cu_state_cpu_t cpu;
static uint8_t output[8192], control[256];
static unsigned output_n, control_n;
cu_state_esp_t *cu_esp_get_state(void){return &es;}
cu_state_cpu_t *cu_avr_get_state(void){return &cpu;}
void cu_esp_txl(const char *p,auint n)
{
 if(cu_uzenet_stock_write(p,n))return;
 assert(output_n+n<=sizeof(output));
 memcpy(output+output_n,p,n);output_n+=n;
 es.read_buf_pos_in+=n;
}
sint32 cu_esp_process_ipd(void){return 0;}
sint32 cu_esp_net_send(uint32 sock,sint8 *p,sint32 n,sint32 flags)
{
 (void)sock;(void)flags;
 assert(control_n+(unsigned)n<=sizeof(control));
 memcpy(control+control_n,p,n);control_n+=n;return n;
}
static void step(unsigned milliseconds)
{
 for(unsigned i=0;i<milliseconds;i++){
  cpu.cycle+=28637;cu_uzenet_tick(cpu.cycle);
 }
}
static void drain(void){es.read_buf_pos_out=es.read_buf_pos_in;}
static void pump(unsigned ms){while(ms--){step(1);drain();}}
static unsigned decode(UnRx *rx)
{
 unsigned got=0;memset(rx,0,sizeof(*rx));
 for(unsigned i=0;i<output_n;i++){unsigned n=un_receive(rx,output[i]);if(n)got=n;}
 output_n=0;return got;
}
static void request(uint8_t type,const uint8_t *p,unsigned n)
{
 uint8_t bytes[UN_BODY_MAX],crc=0;bytes[0]=type;
 memcpy(bytes+1,p,n);
 for(unsigned i=0;i<=n;i++)crc=un_crc8(crc,bytes[i]);
 bytes[n+1]=crc;
 assert(cu_uzenet_uart_write(UN_DELIM,cpu.cycle));
 for(unsigned i=0;i<n+2;i++){
  if(bytes[i]==UN_DELIM || bytes[i]==UN_ESCAPE){
   assert(cu_uzenet_uart_write(UN_ESCAPE,cpu.cycle));
   assert(cu_uzenet_uart_write(bytes[i]^0x20,cpu.cycle));
  }else assert(cu_uzenet_uart_write(bytes[i],cpu.cycle));
 }
 assert(cu_uzenet_uart_write(UN_DELIM,cpu.cycle));
 pump(100);
}
static void entry(void)
{
 cpu.iors[CU_IO_DDRA]=0;step(120);
 cpu.iors[CU_IO_DDRA]=0x80;cpu.iors[CU_IO_PORTA]=0;step(30);
 cpu.iors[CU_IO_DDRA]=0;step(5);
 assert(un_session.active && un_ack_low && !(cu_uzenet_pina(0,cpu.cycle)&0x80));
 step(35);assert(un_ack_low && !output_n);
 step(10);assert(!un_ack_low && !output_n);
 assert(cu_uzenet_pina(0,cpu.cycle)&0x80);
 cpu.iors[CU_IO_DDRA]=0x80; /* READY */
 pump(100);
 UnRx rx;assert(decode(&rx)==11 && rx.data[0]==UN_ENTERED);
 assert(rx.data[3]==4 && rx.data[8]==un_session.tx_batch+1 && rx.data[10]==40);
}
static void network(void)
{
 uint8_t cfg[19]={'T','E','S','T','G','A','M','E'};
 cfg[8]=1;cfg[16]=1;cfg[17]=2;cfg[18]=1;
 es.user_input_mode=ESP_USER_MODE_UNVARNISHED;es.link_state[0]=CU_ESP_LS_OPEN;
 step(1);
#ifndef ENABLE_OPENSSL
 assert(ue_start(cfg,un_ms)==UE_BACKEND && !un_io_failed);
#else
 int relay=socket(AF_INET,SOCK_DGRAM,0);assert(relay>=0);
 struct sockaddr_in address={0},client_address;
 address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 assert(bind(relay,(struct sockaddr*)&address,sizeof(address))==0);
 socklen_t size=sizeof(address);
 assert(getsockname(relay,(struct sockaddr*)&address,&size)==0);
 assert(fcntl(relay,F_SETFL,O_NONBLOCK)==0);
 assert(ue_start(cfg,un_ms)==0);step(1);
 assert(control_n==25 && control[0]==UR_OPEN);
 uint8_t ticket[68]={0};memcpy(ticket,"UNCT",4);ticket[4]=6;
 memcpy(ticket+6,control+21,4);memcpy(ticket+10,"MATCH001",8);
 ticket[23]=1;ticket[24]=3;ticket[25]=1;
 memcpy(ticket+26,&address.sin_addr.s_addr,4);
 unsigned port=ntohs(address.sin_port);ticket[30]=port;ticket[31]=port>>8;
 memset(ticket+32,0x5a,32);ur_put32(ticket+64,60);
 /* Tickets can span reads; secret bytes must never reach the AVR FIFO. */
 cu_esp_txl((const char*)ticket,13);
 cu_esp_txl((const char*)ticket+13,55);
 assert(!output_n && !un_session.count);
 step(1);assert(un_client.state==UC_BINDING);
 uint8_t packet[UR_PACKET_MAX];int got=-1;
 for(unsigned i=0;i<100 && got<0;i++){
  size=sizeof(client_address);
  got=recvfrom(relay,packet,sizeof(packet),0,(struct sockaddr*)&client_address,&size);
  if(got<0){usleep(1000);step(1);}
 }
 assert(got==48 && packet[4]==UR_HELLO);
 UrReplay replay={0};
 assert(ur_open(packet,got,ticket+32,0,ticket+10,&replay));
 assert(ur_seal(packet,ticket+32,1,ticket+10,0,UR_READY,1,0,3,1,NULL));
 assert(sendto(relay,packet,48,0,(struct sockaddr*)&client_address,size)==48);
 for(unsigned i=0;i<100 && un_client.state!=UC_READY;i++){usleep(1000);step(1);}
 assert(un_client.state==UC_READY);
 /* Reject unauthenticated packets and duplicate authenticated sequences. */
 packet[24]^=1;
 assert(!uc_receive(&un_client,packet,48,un_ms));
 packet[24]^=1;assert(!uc_receive(&un_client,packet,48,un_ms));
 un_net_close();step(1);
 assert(control[control_n-1]==UR_CLOSE);
 cu_esp_txl("UNCX",4);assert(!un_capture && !output_n);
 close(relay);
#endif
 es.user_input_mode=ESP_USER_MODE_AT;es.link_state[0]=0;
 step(1);
}
int main(void)
{
 es.serial_route=CU_ESP_SERIAL_ESP_MODULE;es.emulation_model=1;
 es.baud_rate=115200;cu_uzenet_reset();
 step(150);assert(!cu_uzenet_enabled() && !un_session.active);
 es.at_firmware_profile=CU_ESP_AT_FW_UZENET;
 step(150);assert(!cu_uzenet_enabled() && !un_session.active);
 es.reset_pin=1;
 /* A valid entry pulse must survive a stock beacon still in the FIFO. */
 cpu.iors[CU_IO_DDRA]=0;step(120);
 cu_esp_txl("!M\0\0",4);
 cpu.iors[CU_IO_DDRA]=0x80;cpu.iors[CU_IO_PORTA]=0;step(30);
 cpu.iors[CU_IO_DDRA]=0;step(10);
 assert(un_entry_pending && !un_session.active && output_n==4);
 step(90);assert(un_entry_pending && !un_session.active);
 drain();output_n=0;step(5);assert(un_session.active && un_ack_low);
 cu_uzenet_reset();drain();output_n=0;
 entry();
 /* UART completions may be delivered after the periodic firmware tick. */
 uint32_t before=un_ms;
 cu_uzenet_tick(cpu.cycle-1000);
 assert(un_ms==before);
 /* A real 32-bit cycle wrap must still advance time normally. */
 un_cycle=cpu.cycle=0xfffff000U;
 before=un_ms;step(2);assert(un_ms-before==2);
 UnRx rx;
 request(UN_CAPS,NULL,0);assert(decode(&rx)==5 && rx.data[0]==0x81 && rx.data[1]==4);
 uint8_t info[]={1,UE_SERVICE_INFO};
 request(UE_REQUEST,info,sizeof(info));assert(decode(&rx)>=3 && rx.data[2]==UE_OK);
 /* Duplicate transaction has an identical cached response. */
 uint8_t previous[UN_BODY_MAX];memcpy(previous,rx.data,sizeof(previous));
 request(UE_REQUEST,info,sizeof(info));assert(decode(&rx)>=3 && !memcmp(previous,rx.data,7));
 network();
 /* Stock bytes remain deferred until explicitly pulled or exiting. */
 cu_esp_txl("AT\r\n",4);assert(!output_n && un_session.count==4);
 cpu.iors[CU_IO_DDRA]=0; /* PAUSE */
 request(UN_CAPS,NULL,0);assert(!output_n && un_tx_used);
 pump(20);assert(!output_n);
 cpu.iors[CU_IO_DDRA]=0x80;pump(100);assert(decode(&rx)==5);
 uint8_t name[]={0,'L','E','E'};
 assert(us_profile_write(0,name+1,3));
 uint8_t loaded[24];assert(us_profile_read(0,loaded)==3 && !memcmp(loaded,"LEE",3));
 assert(us_profile_read(1,loaded)==-1);
 cu_uzenet_set_config_path("player-a.cfg");
 assert(us_profile_write(0,(const uint8_t *)"ALICE",5));
 cu_uzenet_set_config_path("player-b.cfg");
 assert(us_profile_write(0,(const uint8_t *)"BOB",3));
 cu_uzenet_reset();
 assert(us_profile_read(0,loaded)==3 && !memcmp(loaded,"BOB",3));
 cu_uzenet_set_config_path("player-a.cfg");
 assert(us_profile_read(0,loaded)==5 && !memcmp(loaded,"ALICE",5));
 /* Resume the transport exercise after the deliberate reset above. */
 entry();cu_esp_txl("AT\r\n",4);
 request(UN_EXIT,NULL,0);
 assert(!un_session.active && output_n>=4 && !memcmp(output+output_n-4,"AT\r\n",4));
 output_n=0;
 /* New entry/reset does not carry transaction or FIFO state across sessions. */
 entry();cu_uzenet_reset();assert(!un_session.active && !un_tx_used && !un_rx_count);
 es.at_firmware_profile=CU_ESP_AT_FW_ESPAT_230;
 assert(!cu_uzenet_uart_write(0x7e,cpu.cycle));
 assert(cu_uzenet_pina(0x5a,cpu.cycle)==0x5a);
 puts("Uzenet adapter GPIO/ACK/flow/framing/retry/profiles/EXIT: PASS");
 return 0;
}
