/* Host adaptation of custom-firmware/firmware/un_net.inc (2026-10-20).
 * Room tickets, UDP scheduling and exit fencing follow that adapter.
 * Host sockets, OpenSSL and a local profile file replace ESP platform APIs.
 */
static UrClient un_client;
static UrGame un_game;
static UnEndpoint un_endpoint;
/* Preserve the historical path unless a process-specific config is selected. */
static char un_profiles_path[512] = "uzenet-profiles.bin";
void cu_uzenet_set_config_path(const char *path)
{
 snprintf(un_profiles_path,sizeof(un_profiles_path),"%s.uzenet-profiles.bin",path);
}
int us_profile_read(uint8_t local,uint8_t text[US_NAME_MAX])
{
 uint8_t slots[4][US_NAME_MAX+1];
 FILE *f=fopen(un_profiles_path,"rb");
 if(!f)return -1;
 size_t n=fread(slots,1,sizeof(slots),f);fclose(f);
 if(n!=sizeof(slots) || local>=4)return -2;
 if(slots[local][0]==0xFF)return -1;
 if(slots[local][0]>US_NAME_MAX)return -2;
 memcpy(text,slots[local]+1,slots[local][0]);return slots[local][0];
}
int us_profile_write(uint8_t local,const uint8_t *text,uint8_t n)
{
 uint8_t slots[4][US_NAME_MAX+1];
 if(local>=4 || n>US_NAME_MAX)return 0;
 memset(slots,0,sizeof(slots));for(unsigned i=0;i<4;i++)slots[i][0]=0xFF;
 FILE *f=fopen(un_profiles_path,"rb");
 if(f){size_t got=fread(slots,1,sizeof(slots),f);fclose(f);if(got!=sizeof(slots))return 0;}
 slots[local][0]=n;memset(slots[local]+1,0,US_NAME_MAX);memcpy(slots[local]+1,text,n);
 char temporary[sizeof(un_profiles_path)+4];
 snprintf(temporary,sizeof(temporary),"%s.tmp",un_profiles_path);
 f=fopen(temporary,"wb");if(!f)return 0;
 bool ok=fwrite(slots,1,sizeof(slots),f)==sizeof(slots);if(fclose(f)!=0)ok=false;
 if(!ok)return 0;
 return configfile_replace(temporary,un_profiles_path)==0;
}
static bool un_power_begin(void){return true;}
static bool un_power_end(void){return true;}
static ESP_SOCKET un_socket=ESP_INVALID_SOCKET;
static volatile bool un_transparent;
static uint8_t un_inject[25],un_inject_pos,un_inject_count;
static uint8_t un_control[68],un_control_used;
static bool un_capture,un_ticket_pending;
static uint8_t un_peer_query;
static uint32_t un_peer_query_at,un_direct_at;

static void un_net_send(const uint8_t *packet,const uint8_t address[6]) {
    struct sockaddr_in endpoint={0};endpoint.sin_family=AF_INET;
    memcpy(&endpoint.sin_addr.s_addr,address,4);endpoint.sin_port=htons((uint16_t)address[4]|((uint16_t)address[5]<<8));
    if(sendto(un_socket,(const char *)packet,UR_SIZE(packet),0,(struct sockaddr*)&endpoint,sizeof(endpoint))!=(int)UR_SIZE(packet) &&
       un_client.tx_errors<65535)un_client.tx_errors++;
}
int ur_mac(const uint8_t key[32],uint8_t domain,const uint8_t *body,size_t bytes,uint8_t tag[16])
{
#ifdef ENABLE_OPENSSL
 uint8_t input[UR_PACKET_MAX-15],output[32];unsigned used=0;
 if(bytes+1>sizeof(input))return 0;
 input[0]=domain;memcpy(input+1,body,bytes);
 if(!HMAC(EVP_sha256(),key,32,input,bytes+1,output,&used) || used!=32)return 0;
 memcpy(tag,output,16);return 1;
#else
 (void)key;(void)domain;(void)body;(void)bytes;(void)tag;return 0;
#endif
}
static bool un_random(uint32_t *value)
{
#ifdef ENABLE_OPENSSL
 return RAND_bytes((unsigned char *)value,sizeof(*value))==1;
#else
 (void)value;return false;
#endif
}
static void un_socket_close(void)
{
 if(un_socket==ESP_INVALID_SOCKET)return;
#ifdef _WIN32
 closesocket(un_socket);
#else
 close(un_socket);
#endif
 un_socket=ESP_INVALID_SOCKET;
}
static void un_net_disconnect(void) {
    un_socket_close();
    (void)un_power_end();
}
static void un_net_close(void) {
    /* The TLS CLOSE acknowledgement fences all ticket bytes, even if EXIT
     * happens halfway through a ticket. Keep filtering until that fence. */
    if(un_capture && un_transparent){un_inject[0]=UR_CLOSE;un_inject_pos=0;un_inject_count=1;}
    if(un_socket!=ESP_INVALID_SOCKET && un_client.state>=UC_BINDING && un_client.state<=UC_READY && un_client.seq<UINT32_MAX){
        uint8_t packet[48];
        if(ur_seal(packet,un_client.key,0,un_client.match,un_client.peer,UR_LEAVE,++un_client.seq,0,0,un_client.history.width,0))
            un_net_send(packet,un_client.endpoint);
    }
    un_net_disconnect();uc_reset(&un_client);ug_reset(&un_game);ue_reset(&un_endpoint);
}
uint8_t un_session_can_exit(void) {return un_inject_count==0 && un_power_end();}
static bool un_net_emit(uint8_t byte) {
    if(un_session.active)return un_defer(&un_session,&byte,1)==1;
    un_raw_write(&byte,1);return true;
}
static bool un_net_tap(uint8_t byte) {
    static const uint8_t prefix[3]={'U','N','C'};
    if(!un_capture)return false;
    /* Caller reserves four ring bytes before each call, so a mismatched
     * partial prefix can be restored without losing or reordering data. */
    if(un_control_used<3){
        if(byte==prefix[un_control_used]){un_control[un_control_used++]=byte;return true;}
        for(unsigned i=0;i<un_control_used;i++)if(!un_net_emit(un_control[i]))un_io_failed=true;
        un_control_used=0;
        if(byte=='U'){un_control[un_control_used++]=byte;return true;}
        if(!un_net_emit(byte))un_io_failed=true;
        return true;
    }
    if(un_control_used==3 && byte=='X'){
        memset(un_control,0,sizeof(un_control));un_control_used=0;un_capture=un_ticket_pending=false;return true;
    }
    if(un_control_used==3 && byte!='T' && byte!='P'){
        for(unsigned i=0;i<3;i++)if(!un_net_emit(un_control[i]))un_io_failed=true;
        un_control_used=0;
        if(byte=='U')un_control[un_control_used++]=byte;
        else if(!un_net_emit(byte))un_io_failed=true;
        return true;
    }
    un_control[un_control_used++]=byte;
    if(un_control_used==sizeof(un_control)){
        if(un_control[3]=='P')(void)ud_offer(&un_client,un_control,un_now());
        else (void)uc_ticket(&un_client,un_control,un_now());
        un_ticket_pending=false;
        /* A stale nonce's ticket is also secret; never forward it to AVR. */
        memset(un_control,0,sizeof(un_control));un_control_used=0;
    }
    return true;
}
uint8_t ue_start(const uint8_t p[19],uint32_t now) {
    if(!un_transparent || un_capture || un_inject_count || un_client.state!=UC_IDLE)return UE_NOT_READY;
    uint32_t nonce;
    if(!un_random(&nonce))return UE_BACKEND;
    if(!uc_begin(&un_client,p,(uint8_t*)&nonce,now,un_inject))return UE_BAD_REQUEST;
    if(!un_power_begin()){uc_reset(&un_client);return UE_BACKEND;}
    ug_reset(&un_game);un_inject_pos=0;un_inject_count=25;
    un_capture=un_ticket_pending=true;un_peer_query_at=now;un_peer_query=0;return 0;
}
uint8_t un_session_extension(uint8_t type,const uint8_t *p,uint8_t n) {
    if(type!=UE_REQUEST)return 0;
    ue_command(&un_endpoint,&un_game,&un_client,p,n,un_now());return 1;
}
static void un_net_poll(void) {
    uint8_t packet[UR_PACKET_MAX+1],direct[UR_PACKET_MAX],address[6];uint32_t now=un_now();
    cu_state_esp_t *es=cu_esp_get_state();
    un_transparent=es->user_input_mode==ESP_USER_MODE_UNVARNISHED && es->link_state[0]==CU_ESP_LS_OPEN;
    if(un_inject_count && un_transparent){
        sint32 sent=cu_esp_net_send(0,(sint8 *)un_inject+un_inject_pos,un_inject_count,0);
        if(sent>0 && sent<=un_inject_count){un_inject_pos+=(uint8_t)sent;un_inject_count-=(uint8_t)sent;}
    }
    /* TCP tickets and UDP recovery keep progressing while AVR RX is paused. */
    if(un_capture && cu_uzenet_stock_capacity())(void)cu_esp_process_ipd();
    if(!un_transparent){
        if(un_client.state==UC_TICKET)uc_fail(&un_client,12);
        un_inject_count=un_inject_pos=0;
        /* An established UDP lease survives control-TCP loss. Only a ticket
         * still in flight must remain filtered until its close fence/reset. */
        if(!un_ticket_pending && !un_control_used)un_capture=false;
    }
    if(un_client.state>=UC_BINDING && un_client.state<=UC_READY && un_socket==ESP_INVALID_SOCKET){
        un_socket=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if(un_socket==ESP_INVALID_SOCKET){uc_fail(&un_client,13);return;}
        struct sockaddr_in local={0};local.sin_family=AF_INET;
#ifdef _WIN32
        u_long nonblock=1;
        int result=ioctlsocket(un_socket,FIONBIO,&nonblock);
#else
        int flags=fcntl(un_socket,F_GETFL,0);
        int result=flags<0?-1:fcntl(un_socket,F_SETFL,flags|O_NONBLOCK);
#endif
        if(result<0 || bind(un_socket,(struct sockaddr*)&local,sizeof(local))<0){
            uc_fail(&un_client,13);un_net_disconnect();return;
        }
    }
    if(un_socket!=ESP_INVALID_SOCKET){
        for(unsigned budget=0;budget<8;budget++){
            struct sockaddr_in source;socklen_t length=sizeof(source);
            int n=recvfrom(un_socket,(char *)packet,sizeof(packet),0,(struct sockaddr*)&source,&length);if(n<=0)break;
            memcpy(address,&source.sin_addr.s_addr,4);uint16_t port=ntohs(source.sin_port);
            address[4]=(uint8_t)port;address[5]=(uint8_t)(port>>8);
            if(!memcmp(address,un_client.endpoint,6))(void)uc_receive(&un_client,packet,(size_t)n,now);
            else (void)ud_receive(&un_client,packet,(size_t)n,address,now);
        }
    }
    ue_tick(&un_endpoint,&un_client,now);
    ug_recover(&un_game,&un_client);
    if(uc_next(&un_client,now,packet) && un_socket!=ESP_INVALID_SOCKET){
        for(unsigned i=0;i<4;i++)if(ud_input(&un_client,i,packet,now,direct,address))un_net_send(direct,address);
        un_net_send(packet,un_client.endpoint);
    }
    if(un_socket!=ESP_INVALID_SOCKET && now-un_direct_at>=5){
        un_direct_at=now;
        for(unsigned i=0;i<4;i++)if(ud_next(&un_client,i,now,direct,address))un_net_send(direct,address);
    }
    if(un_socket!=ESP_INVALID_SOCKET && up_next(&un_client,now,packet,address))un_net_send(packet,address);
    /* OOB gets idle scheduling slots only, after inputs/recovery and control.
     * It continues buffering while PA7 is paused, never pushes UART text. */
    else if(un_socket!=ESP_INVALID_SOCKET && now-un_client.last_tx>=2 && now-un_direct_at>=2 &&
        uo_next(&un_client,now,packet,address))un_net_send(packet,address);
    if(un_session.active && un_transparent && un_client.protocol>=UR_PROFILE && un_client.state>=UC_WAITING && un_client.state<=UC_READY &&
       !un_inject_count && !un_ticket_pending && now-un_peer_query_at>=(now-un_client.born<5000?250u:2000u)){
        un_peer_query_at=now;un_peer_query=(un_peer_query+1)&3;
        if(un_peer_query==un_client.peer)un_peer_query=(un_peer_query+1)&3;
        un_inject[0]=UR_PEER_INFO;un_inject[1]=un_peer_query;un_inject_pos=0;un_inject_count=2;un_capture=un_ticket_pending=true;
    }
    /* Optional transport diagnosis; never log tickets or traffic keys. */
    if(getenv("CUZEBOX_UZENET_TRACE")){
        static unsigned previous=255;
        if(previous!=un_client.state){
            fprintf(stderr,"Uzenet state=%u error=%u pending=%u transparent=%u\n",
                un_client.state,un_client.error,un_inject_count,un_transparent);
            fflush(stderr);previous=un_client.state;
        }
    }
    if(un_client.state==UC_FAILED)un_net_disconnect();
}
