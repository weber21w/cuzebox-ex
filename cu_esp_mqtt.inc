/* Native MQTT 3.1.1 transport for ESP-AT 2.3 compatibility.
 * Stock ESP8266 WROOM-02 firmware exposes scheme 1 (TCP) and scheme 6
 * (WebSocket); MQTT-over-TLS is excluded by Espressif's ESP8266 build. */
#define CU_MQTT_SUBS 8u
#define CU_MQTT_RX 8192u
#define CU_MQTT_RECONNECT_MS 1000u

#define CU_MQTT_WS_KEY "Y3V6ZWJveC1lc3AtYXQ="
#define CU_MQTT_WS_ACCEPT "AnW6Bcu1weOqCgyK7MOZnLJ+hSk="

typedef struct {
    ESP_SOCKET sock;
    uint8 connected;
    uint8 websocket;
    uint8 reconnect;
    uint8 ever_connected;
    uint8 evt_kind;
    uint8 sub_count;
    uint8 ping_outstanding;
    uint16 pkt_id;
    uint32 last_io_ms;
    uint32 ping_sent_ms;
    uint32 reconnect_at_ms;
    char host[CU_ESP_MQTT_HOST_MAX+1u];
    uint16 port;
    char path[CU_ESP_MQTT_PATH_MAX+1u];
    char sub_topic[CU_MQTT_SUBS][CU_ESP_MQTT_TOPIC_MAX+1u];
    uint8 sub_qos[CU_MQTT_SUBS];
    char evt_topic[CU_ESP_MQTT_TOPIC_MAX+1u];
    uint8 evt_data[CU_ESP_MQTT_EVENT_DATA_MAX];
    uint32 evt_len;
    uint8 rx[CU_MQTT_RX];
    uint32 rx_len;
    uint8 ws_rx[CU_MQTT_RX+32u];
    uint32 ws_rx_len;
} cu_mqtt_rt_t;

static cu_mqtt_rt_t cu_mqtt={.sock=ESP_INVALID_SOCKET};

static uint16 cu_mqtt_effective_keepalive(void)
{
    return esp_state.mqtt_keepalive ? esp_state.mqtt_keepalive : 120u;
}

static void cu_mqtt_close(uint8 event)
{
    if(cu_mqtt.sock!=ESP_INVALID_SOCKET && cu_mqtt.sock!=-1){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
        closesocket(cu_mqtt.sock);
#else
        close(cu_mqtt.sock);
#endif
    }
    cu_mqtt.sock=ESP_INVALID_SOCKET;
    cu_mqtt.connected=0u;
    cu_mqtt.rx_len=0u;
    cu_mqtt.ws_rx_len=0u;
    cu_mqtt.ping_outstanding=0u;
    if(event){
        cu_mqtt.evt_kind=CU_ESP_MQTT_EVT_DISCONNECTED;
        if(cu_mqtt.reconnect && cu_mqtt.ever_connected)
            cu_mqtt.reconnect_at_ms=cu_esp_now_ms()+CU_MQTT_RECONNECT_MS;
    }
}

static int cu_mqtt_send_all(const uint8 *p,uint32 n)
{
    while(n){
#if defined(MSG_NOSIGNAL)
        int r=(int)send(cu_mqtt.sock,(const char*)p,(int)n,MSG_NOSIGNAL);
#else
        int r=(int)send(cu_mqtt.sock,(const char*)p,(int)n,0);
#endif
        if(r<=0)return -1;
        p+=(uint32)r;
        n-=(uint32)r;
    }
    return 0;
}

static uint32 cu_mqtt_remenc(uint8 *out,uint32 v)
{
    uint32 n=0u;
    do{
        uint8 b=(uint8)(v%128u);
        v/=128u;
        if(v)b|=0x80u;
        out[n++]=b;
    }while(v&&n<4u);
    return n;
}

static uint32 cu_mqtt_putstr(uint8 *out,const char *s)
{
    uint32 n=(uint32)strlen(s?s:"");
    out[0]=(uint8)(n>>8);
    out[1]=(uint8)n;
    if(n)memcpy(out+2,s,n);
    return n+2u;
}

static int cu_mqtt_ws_send_frame(uint8 opcode,const uint8 *payload,uint32 len)
{
    uint8 h[14],mask[4],tmp[1024];
    uint32 hn=0u,off=0u;
    h[hn++]=(uint8)(0x80u|(opcode&0x0fu));
    if(len<126u)h[hn++]=(uint8)(0x80u|len);
    else if(len<=65535u){h[hn++]=0xFEu;h[hn++]=(uint8)(len>>8);h[hn++]=(uint8)len;}
    else{h[hn++]=0xFFu;for(int i=7;i>=0;i--)h[hn++]=(uint8)(((uint64_t)len)>>(i*8));}
    {uint32 rv=(uint32)rand();memcpy(mask,&rv,4);}
    memcpy(h+hn,mask,4);hn+=4u;
    if(cu_mqtt_send_all(h,hn))return -1;
    while(off<len){
        uint32 n=len-off;if(n>sizeof(tmp))n=sizeof(tmp);
        for(uint32 i=0u;i<n;i++)tmp[i]=(uint8)(payload[off+i]^mask[(off+i)&3u]);
        if(cu_mqtt_send_all(tmp,n))return -1;
        off+=n;
    }
    return 0;
}

static int cu_mqtt_ws_send(const uint8 *payload,uint32 len)
{
    return cu_mqtt_ws_send_frame(0x2u,payload,len);
}

static int cu_mqtt_sendpkt(uint8 type,const uint8 *body,uint32 blen)
{
    uint8 h[5];
    int rc;
    h[0]=type;
    uint32 rn=cu_mqtt_remenc(h+1,blen);
    if(cu_mqtt.websocket){
        uint8 *p=(uint8*)malloc(1u+rn+blen);
        if(!p)return -1;
        memcpy(p,h,1u+rn);
        if(blen)memcpy(p+1u+rn,body,blen);
        rc=cu_mqtt_ws_send(p,1u+rn+blen);
        free(p);
    }else{
        rc=cu_mqtt_send_all(h,1u+rn);
        if(!rc&&blen)rc=cu_mqtt_send_all(body,blen);
    }
    if(!rc)cu_mqtt.last_io_ms=cu_esp_now_ms();
    return rc;
}

static int cu_mqtt_wait_readable(uint32 ms)
{
    fd_set r;
    struct timeval tv;
    if(cu_mqtt.sock==ESP_INVALID_SOCKET||cu_mqtt.sock==-1)return 0;
    FD_ZERO(&r);FD_SET(cu_mqtt.sock,&r);
    tv.tv_sec=(long)(ms/1000u);tv.tv_usec=(long)((ms%1000u)*1000u);
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
    return select(0,&r,NULL,NULL,&tv)>0;
#else
    return select((int)cu_mqtt.sock+1,&r,NULL,NULL,&tv)>0;
#endif
}

static int cu_mqtt_recv_exact_timeout(uint8 *out,uint32 n,uint32 timeout)
{
    uint32 off=0u;
    while(off<n){
        if(!cu_mqtt_wait_readable(timeout))return -1;
        int r=(int)recv(cu_mqtt.sock,(char*)out+off,(int)(n-off),0);
        if(r<=0)return -1;
        off+=(uint32)r;
    }
    return 0;
}

static int cu_mqtt_ws_handshake(const char *host,uint16 port,const char *path)
{
    char req[1024],resp[2048];
    const char *p=(path&&*path)?path:"/mqtt";
    snprintf(req,sizeof(req),
        "GET %s HTTP/1.1\r\nHost: %s:%u\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Key: " CU_MQTT_WS_KEY "\r\nSec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Protocol: mqtt\r\n\r\n",p,host,(unsigned)port);
    if(cu_mqtt_send_all((uint8*)req,(uint32)strlen(req)))return -1;
    uint32 n=0u;
    while(n+1u<sizeof(resp)&&cu_mqtt_wait_readable(2000u)){
        int r=(int)recv(cu_mqtt.sock,resp+n,(int)(sizeof(resp)-1u-n),0);
        if(r<=0)break;
        n+=(uint32)r;resp[n]=0;
        if(strstr(resp,"\r\n\r\n"))break;
    }
    if(!n||!strstr(resp," 101 ")||!strstr(resp,CU_MQTT_WS_ACCEPT))return -1;
    return 0;
}

static int cu_mqtt_socket_connect(const char *host,uint16 port)
{
    char ps[16];
    struct addrinfo hints,*ai=NULL,*it;
    snprintf(ps,sizeof(ps),"%u",(unsigned)port);
    memset(&hints,0,sizeof(hints));hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;
    if(getaddrinfo(host,ps,&hints,&ai))return -1;
    for(it=ai;it;it=it->ai_next){
        ESP_SOCKET s=socket(it->ai_family,it->ai_socktype,it->ai_protocol);
        if(s==ESP_INVALID_SOCKET||s==-1)continue;
        if(cu_sock_connect_timeout(s,it->ai_addr,(socklen_t)it->ai_addrlen,5000)==0){cu_mqtt.sock=s;freeaddrinfo(ai);return 0;}
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
        closesocket(s);
#else
        close(s);
#endif
    }
    freeaddrinfo(ai);
    return -1;
}

static int cu_mqtt_send_connect(void)
{
    uint32 cap=32u+(uint32)strlen((char*)esp_state.mqtt_client_id)+(uint32)strlen((char*)esp_state.mqtt_username)+
        (uint32)strlen((char*)esp_state.mqtt_password)+(uint32)strlen((char*)esp_state.mqtt_lwt_topic)+
        (uint32)strlen((char*)esp_state.mqtt_lwt_msg);
    uint8 *b=(uint8*)malloc(cap);
    uint32 n=0u;
    if(!b)return -1;
    n+=cu_mqtt_putstr(b+n,"MQTT");b[n++]=4u;
    uint8 fl=2u;
    if(esp_state.mqtt_username[0])fl|=0x80u;
    if(esp_state.mqtt_password[0])fl|=0x40u;
    if(esp_state.mqtt_lwt_topic[0]){fl|=0x04u|(uint8)((esp_state.mqtt_lwt_qos&3u)<<3);if(esp_state.mqtt_lwt_retain)fl|=0x20u;}
    if(esp_state.mqtt_disable_clean_session)fl&=(uint8)~2u;
    b[n++]=fl;
    {uint16 keep=cu_mqtt_effective_keepalive();b[n++]=(uint8)(keep>>8);b[n++]=(uint8)keep;}
    n+=cu_mqtt_putstr(b+n,(char*)esp_state.mqtt_client_id);
    if(esp_state.mqtt_lwt_topic[0]){n+=cu_mqtt_putstr(b+n,(char*)esp_state.mqtt_lwt_topic);n+=cu_mqtt_putstr(b+n,(char*)esp_state.mqtt_lwt_msg);}
    if(esp_state.mqtt_username[0])n+=cu_mqtt_putstr(b+n,(char*)esp_state.mqtt_username);
    if(esp_state.mqtt_password[0])n+=cu_mqtt_putstr(b+n,(char*)esp_state.mqtt_password);
    int rc=cu_mqtt_sendpkt(0x10u,b,n);
    free(b);
    return rc;
}

static int cu_mqtt_recv_raw(uint8 *out,uint32 cap,uint32 timeout)
{
    if(!cu_mqtt_wait_readable(timeout))return -1;
    return (int)recv(cu_mqtt.sock,(char*)out,(int)cap,0);
}

static int cu_mqtt_recv_ws(uint8 *out,uint32 cap,uint32 timeout)
{
    for(;;){
        if(cu_mqtt.ws_rx_len>=2u){
            const uint8 *p=cu_mqtt.ws_rx;
            uint64_t len=(uint64_t)(p[1]&0x7fu);
            uint32 hlen=2u;
            uint8 masked=(uint8)(p[1]&0x80u);
            uint8 opcode=(uint8)(p[0]&0x0fu);
            if(len==126u){
                if(cu_mqtt.ws_rx_len>=4u){len=((uint32)p[2]<<8)|p[3];hlen=4u;}else goto need_more;
            }else if(len==127u){
                if(cu_mqtt.ws_rx_len>=10u){len=0u;for(uint32 i=2u;i<10u;i++)len=(len<<8)|p[i];hlen=10u;}else goto need_more;
            }
            if(masked)hlen+=4u;
            if(len>(uint64_t)CU_MQTT_RX)return 0;
            if(cu_mqtt.ws_rx_len<hlen+(uint32)len)goto need_more;
            const uint8 *payload=p+hlen;
            uint8 control[125];
            if(opcode==0x8u)return 0;
            if(opcode==0x9u){
                uint32 cn=(uint32)len;if(cn>sizeof(control))return 0;
                if(masked){const uint8 *mask=p+hlen-4u;for(uint32 i=0u;i<cn;i++)control[i]=(uint8)(payload[i]^mask[i&3u]);}
                else if(cn)memcpy(control,payload,cn);
                if(cu_mqtt_ws_send_frame(0xAu,control,cn))return 0;
            }else if(opcode==0x2u||opcode==0x0u){
                if(len>(uint64_t)cap)return 0;
                if(masked){const uint8 *mask=p+hlen-4u;for(uint32 i=0u;i<(uint32)len;i++)out[i]=(uint8)(payload[i]^mask[i&3u]);}
                else if(len)memcpy(out,payload,(size_t)len);
                uint32 used=hlen+(uint32)len;
                memmove(cu_mqtt.ws_rx,cu_mqtt.ws_rx+used,cu_mqtt.ws_rx_len-used);cu_mqtt.ws_rx_len-=used;
                return (int)len;
            }
            {uint32 used=hlen+(uint32)len;memmove(cu_mqtt.ws_rx,cu_mqtt.ws_rx+used,cu_mqtt.ws_rx_len-used);cu_mqtt.ws_rx_len-=used;}
            continue;
        }
need_more:
        if(cu_mqtt.ws_rx_len>=sizeof(cu_mqtt.ws_rx))return 0;
        if(!cu_mqtt_wait_readable(timeout))return -1;
        int r=(int)recv(cu_mqtt.sock,(char*)cu_mqtt.ws_rx+cu_mqtt.ws_rx_len,(int)(sizeof(cu_mqtt.ws_rx)-cu_mqtt.ws_rx_len),0);
        if(r==0)return 0;
        if(r<0)return -1;
        cu_mqtt.ws_rx_len+=(uint32)r;
    }
}

static int cu_mqtt_get_frame(uint8 *out,uint32 cap,uint32 timeout)
{
    return cu_mqtt.websocket?cu_mqtt_recv_ws(out,cap,timeout):cu_mqtt_recv_raw(out,cap,timeout);
}

static int cu_mqtt_recv_connack(uint32 timeout)
{
    uint8 b[8];
    if(cu_mqtt.websocket){
        int r=cu_mqtt_recv_ws(b,sizeof(b),timeout);
        return (r==4&&b[0]==0x20u&&b[1]==0x02u&&b[3]==0u)?0:-1;
    }
    if(cu_mqtt_recv_exact_timeout(b,4u,timeout))return -1;
    return (b[0]==0x20u&&b[1]==0x02u&&b[3]==0u)?0:-1;
}

static int cu_mqtt_send_subscribe_wire(const char *topic,uint8 qos)
{
    uint8 b[2u+2u+CU_ESP_MQTT_TOPIC_MAX+1u];
    uint16 id=++cu_mqtt.pkt_id;
    b[0]=(uint8)(id>>8);b[1]=(uint8)id;
    uint32 n=2u+cu_mqtt_putstr(b+2,topic);b[n++]=qos;
    return cu_mqtt_sendpkt(0x82u,b,n);
}

static int cu_mqtt_open_saved(uint8 announce)
{
    cu_mqtt.rx_len=0u;cu_mqtt.ws_rx_len=0u;cu_mqtt.ping_outstanding=0u;
    if(cu_mqtt_socket_connect(cu_mqtt.host,cu_mqtt.port))return -1;
    if(cu_mqtt.websocket&&cu_mqtt_ws_handshake(cu_mqtt.host,cu_mqtt.port,cu_mqtt.path)){cu_mqtt_close(0);return -1;}
    if(cu_mqtt_send_connect()||cu_mqtt_recv_connack(5000u)){cu_mqtt_close(0);return -1;}
    cu_sock_set_nonblocking(cu_mqtt.sock);
    cu_mqtt.connected=1u;cu_mqtt.ever_connected=1u;cu_mqtt.last_io_ms=cu_esp_now_ms();cu_mqtt.reconnect_at_ms=0u;
    if(announce)cu_mqtt.evt_kind=CU_ESP_MQTT_EVT_CONNECTED;
    return 0;
}

sint32 cu_esp_mqtt_connect(const char *host,uint16 port,uint8 scheme,const char *path,uint8 reconnect)
{
    cu_mqtt_close(0);
    memset(&cu_mqtt,0,sizeof(cu_mqtt));cu_mqtt.sock=ESP_INVALID_SOCKET;cu_mqtt.pkt_id=1u;
    if(scheme!=1u&&scheme!=6u)return -1;
    cu_mqtt.websocket=(uint8)(scheme==6u);cu_mqtt.reconnect=reconnect;
    snprintf(cu_mqtt.host,sizeof(cu_mqtt.host),"%s",host?host:"");cu_mqtt.port=port;snprintf(cu_mqtt.path,sizeof(cu_mqtt.path),"%s",path?path:"");
    if(cu_mqtt_open_saved(1u)){cu_mqtt.reconnect=0u;return -1;}
    return 0;
}

void cu_esp_mqtt_disconnect(void)
{
    cu_mqtt.reconnect=0u;
    if(cu_mqtt.connected)(void)cu_mqtt_sendpkt(0xE0u,NULL,0u);
    cu_mqtt_close(cu_mqtt.connected?1u:0u);
    cu_mqtt.sub_count=0u;
}

uint8 cu_esp_mqtt_state(void){return cu_mqtt.connected;}

sint32 cu_esp_mqtt_publish(const char *topic,const uint8 *data,uint32 len,uint8 qos,uint8 retain)
{
    if(!cu_mqtt.connected||!topic||qos>2u)return -1;
    uint32 tl=(uint32)strlen(topic),n=2u+tl+len+(qos?2u:0u),o=2u+tl;
    uint8 *b=(uint8*)malloc(n?n:1u);if(!b)return -1;
    b[0]=(uint8)(tl>>8);b[1]=(uint8)tl;memcpy(b+2,topic,tl);
    if(qos){uint16 id=++cu_mqtt.pkt_id;b[o++]=(uint8)(id>>8);b[o++]=(uint8)id;}
    if(len)memcpy(b+o,data,len);
    int rc=cu_mqtt_sendpkt((uint8)(0x30u|(qos<<1)|(retain?1u:0u)),b,n);free(b);
    if(rc)cu_mqtt_close(1u);
    return rc;
}

sint32 cu_esp_mqtt_subscribe(const char *topic,uint8 qos)
{
    if(!cu_mqtt.connected||!topic||qos>2u||cu_mqtt.sub_count>=CU_MQTT_SUBS)return -1;
    if(cu_mqtt_send_subscribe_wire(topic,qos)){cu_mqtt_close(1u);return -1;}
    snprintf(cu_mqtt.sub_topic[cu_mqtt.sub_count],sizeof(cu_mqtt.sub_topic[0]),"%s",topic);
    cu_mqtt.sub_qos[cu_mqtt.sub_count++]=qos;
    return 0;
}

sint32 cu_esp_mqtt_unsubscribe(const char *topic)
{
    if(!cu_mqtt.connected||!topic)return -1;
    uint8 found=0u;
    for(uint8 i=0u;i<cu_mqtt.sub_count;i++)if(!strcmp(cu_mqtt.sub_topic[i],topic)){found=1u;break;}
    if(!found)return 1;
    uint8 b[2u+2u+CU_ESP_MQTT_TOPIC_MAX+1u];uint16 id=++cu_mqtt.pkt_id;b[0]=(uint8)(id>>8);b[1]=(uint8)id;
    uint32 n=2u+cu_mqtt_putstr(b+2,topic);
    if(cu_mqtt_sendpkt(0xA2u,b,n)){cu_mqtt_close(1u);return -1;}
    for(uint8 i=0u;i<cu_mqtt.sub_count;i++)if(!strcmp(cu_mqtt.sub_topic[i],topic)){
        for(uint8 j=i+1u;j<cu_mqtt.sub_count;j++){strcpy(cu_mqtt.sub_topic[j-1u],cu_mqtt.sub_topic[j]);cu_mqtt.sub_qos[j-1u]=cu_mqtt.sub_qos[j];}
        cu_mqtt.sub_count--;break;
    }
    return 0;
}

uint8 cu_esp_mqtt_subscription_count(void){return cu_mqtt.sub_count;}

sint32 cu_esp_mqtt_subscription_get(uint8 idx,char *topic,auint topic_sz,uint8 *qos)
{
    if(idx>=cu_mqtt.sub_count)return -1;
    if(topic&&topic_sz)snprintf(topic,(size_t)topic_sz,"%s",cu_mqtt.sub_topic[idx]);
    if(qos)*qos=cu_mqtt.sub_qos[idx];
    return 0;
}

sint32 cu_esp_mqtt_take_event(uint8 *kind,char *topic,auint topic_sz,uint8 *data,auint data_sz,uint32 *data_len)
{
    if(!cu_mqtt.evt_kind)return 0;
    if(kind)*kind=cu_mqtt.evt_kind;
    if(topic&&topic_sz)snprintf(topic,(size_t)topic_sz,"%s",cu_mqtt.evt_topic);
    if(data_len)*data_len=cu_mqtt.evt_len;
    if(data&&data_sz){uint32 n=cu_mqtt.evt_len;if(n>data_sz)n=(uint32)data_sz;memcpy(data,cu_mqtt.evt_data,n);}
    cu_mqtt.evt_kind=0u;
    return 1;
}

static int cu_mqtt_decode(const uint8 *p,uint32 n,uint32 *hdr,uint32 *rem)
{
    if(n<2u)return 0;
    uint32 m=1u,v=0u,i=1u;
    for(;i<n&&i<5u;i++){
        uint8 b=p[i];v+=(b&127u)*m;m*=128u;
        if(!(b&128u)){*hdr=i+1u;*rem=v;return n>=*hdr+v?1:0;}
    }
    return 0;
}

static uint8 cu_mqtt_runtime_active(void)
{
    if(cu_mqtt.connected)
        return 1u;
    if(cu_mqtt.reconnect && cu_mqtt.ever_connected && cu_mqtt.host[0])
        return 1u;
    return 0u;
}

void cu_esp_mqtt_tick(void)
{
    uint32 now=cu_esp_now_ms();
    if(!cu_mqtt.connected){
        if(cu_mqtt.reconnect&&cu_mqtt.ever_connected&&cu_mqtt.evt_kind==0u&&cu_mqtt.host[0]&&
           (sint32)(now-cu_mqtt.reconnect_at_ms)>=0){
            if(cu_mqtt_open_saved(1u)==0){
                for(uint8 i=0u;i<cu_mqtt.sub_count;i++){
                    if(cu_mqtt_send_subscribe_wire(cu_mqtt.sub_topic[i],cu_mqtt.sub_qos[i])){cu_mqtt_close(1u);break;}
                }
            }else cu_mqtt.reconnect_at_ms=now+CU_MQTT_RECONNECT_MS;
        }
        return;
    }

    uint8 tmp[4096];
    int r=cu_mqtt_get_frame(tmp,sizeof(tmp),0u);
    if(r>0){
        if((uint32)r>sizeof(cu_mqtt.rx)-cu_mqtt.rx_len){cu_mqtt_close(1u);return;}
        memcpy(cu_mqtt.rx+cu_mqtt.rx_len,tmp,(uint32)r);cu_mqtt.rx_len+=(uint32)r;cu_mqtt.last_io_ms=now;
    }else if(r==0){cu_mqtt_close(1u);return;}

    while(cu_mqtt.rx_len){
        uint32 h,rem;if(!cu_mqtt_decode(cu_mqtt.rx,cu_mqtt.rx_len,&h,&rem))break;
        uint8 type=cu_mqtt.rx[0],*b=cu_mqtt.rx+h;
        if((type&0xF0u)==0x30u&&rem>=2u){
            uint32 tl=((uint32)b[0]<<8)|b[1];
            if(2u+tl<=rem){
                uint32 o=2u+tl,q=(type>>1)&3u;uint16 id=0u;
                if(q&&o+2u<=rem){id=(uint16)(((uint16)b[o]<<8)|b[o+1u]);o+=2u;}
                uint32 dl=rem-o;if(dl>sizeof(cu_mqtt.evt_data))dl=sizeof(cu_mqtt.evt_data);
                uint32 tn=tl;if(tn>CU_ESP_MQTT_TOPIC_MAX)tn=CU_ESP_MQTT_TOPIC_MAX;
                memcpy(cu_mqtt.evt_topic,b+2,tn);cu_mqtt.evt_topic[tn]=0;memcpy(cu_mqtt.evt_data,b+o,dl);cu_mqtt.evt_len=dl;cu_mqtt.evt_kind=CU_ESP_MQTT_EVT_MESSAGE;
                if(q==1u){uint8 a[2]={(uint8)(id>>8),(uint8)id};if(cu_mqtt_sendpkt(0x40u,a,2u)){cu_mqtt_close(1u);return;}}
                else if(q==2u){uint8 a[2]={(uint8)(id>>8),(uint8)id};if(cu_mqtt_sendpkt(0x50u,a,2u)){cu_mqtt_close(1u);return;}}
            }
        }else if((type&0xF0u)==0x60u&&rem==2u){
            if(cu_mqtt_sendpkt(0x70u,b,2u)){cu_mqtt_close(1u);return;}
        }else if((type&0xF0u)==0xD0u){
            cu_mqtt.ping_outstanding=0u;
        }
        uint32 used=h+rem;memmove(cu_mqtt.rx,cu_mqtt.rx+used,cu_mqtt.rx_len-used);cu_mqtt.rx_len-=used;
    }

    now=cu_esp_now_ms();
    uint16 keep=cu_mqtt_effective_keepalive();
    if(cu_mqtt.ping_outstanding){
        if(now-cu_mqtt.ping_sent_ms>(uint32)keep*1000u){cu_mqtt_close(1u);return;}
    }else if(now-cu_mqtt.last_io_ms>(uint32)keep*500u){
        if(cu_mqtt_sendpkt(0xC0u,NULL,0u)){cu_mqtt_close(1u);return;}
        cu_mqtt.ping_outstanding=1u;cu_mqtt.ping_sent_ms=now;
    }
}
