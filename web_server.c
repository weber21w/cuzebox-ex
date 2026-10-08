#include "web_server.h"
#include "api_server.h"
#ifdef ENABLE_API_SERVER
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <SDL2/SDL.h>
#include "web_assets.h"
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET wsock_t;
#define WSOCK_INVALID INVALID_SOCKET
#define WSOCK_CLOSE closesocket
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
typedef int wsock_t;
#define WSOCK_INVALID (-1)
#define WSOCK_CLOSE close
#endif
#define WEB_MAX_CLIENTS 16U
#define WEB_RX_CAP 65536U
#define WEB_TX_CAP 131072U
#define WEB_API_REPLY_CAP 65536U
#define WEB_UPLOAD_SLOTS 2U
#define WEB_UPLOAD_MAX_BYTES (512U * 1024U * 1024U)
#define WEB_DUMP_MAX_BYTES (64U * 1024U * 1024U)
#define WEB_DUMP_CHUNK 256U
#define WEB_SCREENSHOT_CHUNK 4096U
#define WEB_API_TIMEOUT_MS 5000U
#define WEB_PORT_TRIES 10U
#define WEB_LISTEN_RETRY_MS 1000U
#define WEB_DUMP_MEMORY 0U
#define WEB_DUMP_SCREENSHOT 1U

typedef struct {
	boole active;
	auint id;
	FILE* file;
	boole run_after;
	char path[1024];
	char name[256];
	auint bytes;
} web_upload_t;

typedef struct {
	wsock_t sock;
	wsock_t event_sock;
	wsock_t dump_sock;
	char rx[WEB_RX_CAP];
	auint rx_len;
	char tx[WEB_TX_CAP];
	auint tx_len,tx_off;
	boole keep_open;
	boole dump_active;
	auint dump_kind;
	FILE* file_stream;
	boole file_stream_delete;
	char file_stream_path[1024];
	char dump_region[16];
	auint dump_addr;
	auint dump_remaining;
	char event_rx[8192];
	auint event_rx_len;
} web_client_t;

typedef struct {
	boole enabled,initialized,wsa_started,stop_thread;
	auint port,api_port;
	auint next_upload_id;
	wsock_t listen_sock;
	web_client_t clients[WEB_MAX_CLIENTS];
	web_upload_t uploads[WEB_UPLOAD_SLOTS];
	SDL_Thread* thread;
} web_state_t;
static web_state_t ws;
static void init_once(void){auint i;if(ws.initialized)return;memset(&ws,0,sizeof(ws));ws.initialized=TRUE;ws.port=WEB_SERVER_DEFAULT_PORT;ws.api_port=API_SERVER_DEFAULT_PORT;ws.listen_sock=WSOCK_INVALID;ws.next_upload_id=1U;for(i=0;i<WEB_MAX_CLIENTS;i++){ws.clients[i].sock=WSOCK_INVALID;ws.clients[i].event_sock=WSOCK_INVALID;ws.clients[i].dump_sock=WSOCK_INVALID;}}
static int would_block(void){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
 int e=WSAGetLastError(); return e==WSAEWOULDBLOCK;
#else
 return errno==EWOULDBLOCK||errno==EAGAIN;
#endif
}
static boole nonblock(wsock_t s){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
 u_long v=1; return ioctlsocket(s,FIONBIO,&v)==0;
#else
 int f=fcntl(s,F_GETFL,0); return f>=0 && fcntl(s,F_SETFL,f|O_NONBLOCK)==0;
#endif
}
static void close_client(auint i){
	if(i>=WEB_MAX_CLIENTS)return;
	if(ws.clients[i].sock!=WSOCK_INVALID)WSOCK_CLOSE(ws.clients[i].sock);
	if(ws.clients[i].event_sock!=WSOCK_INVALID)WSOCK_CLOSE(ws.clients[i].event_sock);
	if(ws.clients[i].dump_sock!=WSOCK_INVALID)WSOCK_CLOSE(ws.clients[i].dump_sock);
	if(ws.clients[i].file_stream!=NULL){fclose(ws.clients[i].file_stream);ws.clients[i].file_stream=NULL;}
	if(ws.clients[i].file_stream_delete&&ws.clients[i].file_stream_path[0])remove(ws.clients[i].file_stream_path);
	memset(&ws.clients[i],0,sizeof(ws.clients[i]));
	ws.clients[i].sock=WSOCK_INVALID;
	ws.clients[i].event_sock=WSOCK_INVALID;
	ws.clients[i].dump_sock=WSOCK_INVALID;
}
static void queue_raw(web_client_t*c,const char*d,auint n){if(!c||!d||c->sock==WSOCK_INVALID)return;if(c->tx_len+n>WEB_TX_CAP){return;}memcpy(c->tx+c->tx_len,d,n);c->tx_len+=n;}
static void http_reply(web_client_t*c,int code,const char*ct,const unsigned char*body,auint len){char h[768];int n=snprintf(h,sizeof(h),"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; connect-src 'self'\r\nConnection: close\r\n\r\n",code,code==200?"OK":code==404?"Not Found":"Bad Request",ct,(unsigned)len);queue_raw(c,h,(auint)n);queue_raw(c,(const char*)body,len);}
static const web_asset_t* asset(const char*path){auint i;const char*p=path;if(strcmp(path,"/")==0)p="/index.html";else if(strcmp(path,"/controls")==0||strcmp(path,"/controls/")==0)p="/controls.html";else if(strcmp(path,"/debugger")==0||strcmp(path,"/debugger/")==0)p="/debugger.html";else if(strcmp(path,"/sd")==0||strcmp(path,"/sd/")==0)p="/sd.html";else if(strcmp(path,"/audio")==0||strcmp(path,"/audio/")==0)p="/audio.html";else if(strcmp(path,"/serial")==0||strcmp(path,"/serial/")==0)p="/serial.html";else if(strcmp(path,"/network")==0||strcmp(path,"/network/")==0)p="/network.html";else if(strcmp(path,"/static/common.css")==0)p="/common.css";else if(strcmp(path,"/static/common.js")==0)p="/common.js";for(i=0;i<WEB_ASSET_COUNT;i++)if(strcmp(web_assets[i].path,p)==0)return &web_assets[i];return NULL;}
/* Read exactly one '\n'-terminated reply line from a blocking API socket.
** MSG_PEEK lets us consume only through the newline, so a persistent
** connection never loses or desynchronizes bytes that follow it. Replies are
** not limited in length other than by cap; a reply that does not fit, a
** timeout, or a closed socket all return FALSE and the caller must drop the
** connection. */
static boole recv_line_blocking(wsock_t s,char*out,auint cap)
{
	char peek[4096];
	auint pos=0U;
	if(cap==0U)return FALSE;
	out[0]=0;
	for(;;){
		int r=(int)recv(s,peek,(int)sizeof(peek),MSG_PEEK);
		int i,take;
		boole done=FALSE;
		if(r==0)return FALSE;
		if(r<0){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
			if(WSAGetLastError()==WSAEINTR)continue;
#else
			if(errno==EINTR)continue;
#endif
			return FALSE; /* error or SO_RCVTIMEO timeout */
		}
		take=r;
		for(i=0;i<r;i++){if(peek[i]=='\n'){take=i+1;done=TRUE;break;}}
		r=(int)recv(s,peek,take,0);
		if(r!=take)return FALSE;
		for(i=0;i<take;i++){
			char b=peek[i];
			if(b=='\n'||b=='\r')continue;
			if(pos+1U>=cap)return FALSE;
			out[pos++]=b;
		}
		out[pos]=0;
		if(done)return TRUE;
	}
}

static void api_socket_set_timeout(wsock_t s,auint ms)
{
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	DWORD t=(DWORD)ms;
	setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(const char*)&t,sizeof(t));
	setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,(const char*)&t,sizeof(t));
#else
	struct timeval tv;
	tv.tv_sec=(long)(ms/1000U);tv.tv_usec=(long)((ms%1000U)*1000U);
	setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
	setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
#endif
}

/* ws.api_port follows the port the API server really bound (see
** web_server_set_api_port, fed from the main loop), so a second instance's
** bridge talks to its own emulator, not to the first instance. */
static auint api_target_port(void)
{
	return ws.api_port;
}

static wsock_t api_open_socket(void)
{
	wsock_t s;
	struct sockaddr_in sa;
	char hello[512];
	s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
	if(s==WSOCK_INVALID)return WSOCK_INVALID;
	memset(&sa,0,sizeof(sa));
	sa.sin_family=AF_INET;
	sa.sin_port=htons((uint16)(api_target_port()&0xffffU));
	sa.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
	if(connect(s,(struct sockaddr*)&sa,(socklen_t)sizeof(sa))!=0){WSOCK_CLOSE(s);return WSOCK_INVALID;}
	api_socket_set_timeout(s,WEB_API_TIMEOUT_MS);
	if(!recv_line_blocking(s,hello,sizeof(hello))||strstr(hello,"\"ok\":1")==NULL){WSOCK_CLOSE(s);return WSOCK_INVALID;}
	return s;
}

static boole api_socket_command(wsock_t s,const char*cmd,char*out,auint cap)
{
	static char line[WEB_API_REPLY_CAP];
	int r;
	auint sent=0U,len=(auint)strlen(cmd);
	if(s==WSOCK_INVALID)return FALSE;
	while(sent<len){r=(int)send(s,cmd+sent,(int)(len-sent),0);if(r<=0)return FALSE;sent+=(auint)r;}
	if(send(s,"\n",1,0)!=1)return FALSE;
	if(!recv_line_blocking(s,line,sizeof(line)))return FALSE;
	if(out&&cap)snprintf(out,cap,"%s",line);
	return TRUE;
}

/* The bridge keeps ONE persistent command connection to the API server.
** Opening a fresh TCP connection per HTTP request made the API server announce
** "API CLIENT CONNECTED/DISCONNECTED" several times a second and could exhaust
** its client slots while closed sockets were still being reaped, which the
** browser reported as the API flapping between connected and unavailable. */
static wsock_t api_cmd_sock=WSOCK_INVALID;
static void api_command_drop(void){if(api_cmd_sock!=WSOCK_INVALID){WSOCK_CLOSE(api_cmd_sock);api_cmd_sock=WSOCK_INVALID;}}
static boole api_command(const char*cmd,char*out,auint cap)
{
	int attempt;
	for(attempt=0;attempt<2;attempt++){
		boole fresh=FALSE;
		if(api_cmd_sock==WSOCK_INVALID){api_cmd_sock=api_open_socket();fresh=TRUE;}
		if(api_cmd_sock==WSOCK_INVALID)return FALSE;
		if(api_socket_command(api_cmd_sock,cmd,out,cap))return TRUE;
		api_command_drop();
		if(fresh)return FALSE; /* a brand new connection failed: do not retry */
	}
	return FALSE;
}
static wsock_t api_subscribe_socket(void){wsock_t s;struct sockaddr_in sa;char hello[512],ack[512];const char*cmd="SUBSCRIBE FRAME CPU SERIAL ESP BREAK WATCH\n";s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==WSOCK_INVALID)return WSOCK_INVALID;memset(&sa,0,sizeof(sa));sa.sin_family=AF_INET;sa.sin_port=htons((uint16)(api_target_port()&0xffffU));sa.sin_addr.s_addr=htonl(INADDR_LOOPBACK);if(connect(s,(struct sockaddr*)&sa,(socklen_t)sizeof(sa))!=0){WSOCK_CLOSE(s);return WSOCK_INVALID;}api_socket_set_timeout(s,WEB_API_TIMEOUT_MS);if(!recv_line_blocking(s,hello,sizeof(hello))){WSOCK_CLOSE(s);return WSOCK_INVALID;}if(send(s,cmd,(int)strlen(cmd),0)<=0||!recv_line_blocking(s,ack,sizeof(ack))){WSOCK_CLOSE(s);return WSOCK_INVALID;}if(!nonblock(s)){WSOCK_CLOSE(s);return WSOCK_INVALID;}return s;}
static void start_events(web_client_t*c){static const char h[]="HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-store\r\nConnection: keep-alive\r\nX-Accel-Buffering: no\r\n\r\nretry: 1000\n\n";c->event_sock=api_subscribe_socket();if(c->event_sock==WSOCK_INVALID){static const unsigned char e[]="{\"ok\":0,\"error\":\"event API unavailable\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}c->keep_open=TRUE;queue_raw(c,h,(auint)strlen(h));}
static void pump_events(web_client_t*c){int r;auint i,start=0;if(!c->keep_open||c->event_sock==WSOCK_INVALID)return;if(c->event_rx_len>=sizeof(c->event_rx)-1)c->event_rx_len=0;r=(int)recv(c->event_sock,c->event_rx+c->event_rx_len,(int)(sizeof(c->event_rx)-1-c->event_rx_len),0);if(r==0){c->keep_open=FALSE;return;}if(r<0){if(!would_block())c->keep_open=FALSE;return;}c->event_rx_len+=(auint)r;c->event_rx[c->event_rx_len]=0;for(i=0;i<c->event_rx_len;i++){if(c->event_rx[i]=='\n'){auint n=i-start;if(n&&c->event_rx[start]!='{'){start=i+1;continue;}if(n){queue_raw(c,"data: ",6U);queue_raw(c,c->event_rx+start,n);queue_raw(c,"\n\n",2U);}start=i+1;}}if(start){memmove(c->event_rx,c->event_rx+start,c->event_rx_len-start);c->event_rx_len-=start;}}
static const char* find_body(char*req){char*p=strstr(req,"\r\n\r\n");return p?p+4:NULL;}

static auint request_content_length(char const* req)
{
	char const* p=strstr(req,"Content-Length:");
	return (p!=NULL)?(auint)strtoul(p+15,NULL,10):0U;
}

static int web_hex_digit(char c)
{
	if(c>='0'&&c<='9')return c-'0';
	if(c>='a'&&c<='f')return c-'a'+10;
	if(c>='A'&&c<='F')return c-'A'+10;
	return -1;
}

static void url_decode(char* dst,auint cap,char const* src,auint len)
{
	auint d=0U,i=0U;
	if(cap==0U)return;
	while(i<len&&src[i]!=0&&d+1U<cap){
		if(src[i]=='%'&&i+2U<len){int a=web_hex_digit(src[i+1]),b=web_hex_digit(src[i+2]);if(a>=0&&b>=0){dst[d++]=(char)((a<<4)|b);i+=3U;continue;}}
		dst[d++]=(src[i]=='+')?' ':src[i];i++;
	}
	dst[d]=0;
}

static boole query_value(char const* query,char const* key,char* out,auint cap)
{
	char const* p=query;
	auint klen=(auint)strlen(key);
	if(out&&cap)out[0]=0;
	if(query==NULL)return FALSE;
	while(*p){
		char const* end=strchr(p,'&');
		char const* eq=strchr(p,'=');
		if(end==NULL)end=p+strlen(p);
		if(eq!=NULL&&eq<end&&(auint)(eq-p)==klen&&strncmp(p,key,klen)==0){url_decode(out,cap,eq+1,(auint)(end-(eq+1)));return TRUE;}
		p=(*end=='&')?end+1:end;
	}
	return FALSE;
}

static void sanitize_filename(char* dst,auint cap,char const* src,char const* fallback)
{
	char const* base=src;char const* p;auint d=0U;
	if(src==NULL||src[0]==0)src=fallback;
	base=src;
	for(p=src;*p;p++)if(*p=='/'||*p=='\\')base=p+1;
	for(p=base;*p&&d+1U<cap;p++){
		unsigned char c=(unsigned char)*p;
		if(isalnum(c)||c=='.'||c=='_'||c=='-')dst[d++]=(char)c;else dst[d++]='_';
	}
	if(d==0U){snprintf(dst,cap,"%s",fallback);return;}
	dst[d]=0;
}

static void json_escape_small(char* dst,auint cap,char const* src)
{
	auint d=0U;char c;
	if(cap==0U)return;
	while(src&&(c=*src++)!=0&&d+2U<cap){
		if(c=='\\'||c=='\"'){dst[d++]='\\';dst[d++]=c;}
		else if(c=='\r'||c=='\n')dst[d++]=' ';
		else dst[d++]=c;
	}
	dst[d]=0;
}

static boole path_exists(char const* path)
{
	FILE* f=fopen(path,"rb");if(f==NULL)return FALSE;fclose(f);return TRUE;
}

static void join_path(char* dst,auint cap,char const* dir,char const* leaf)
{
	auint n;
	if(dir==NULL||dir[0]==0||strcmp(dir,".")==0){snprintf(dst,cap,"%s",leaf);return;}
	n=(auint)strlen(dir);
	if(dir[n-1]=='/'||dir[n-1]=='\\')snprintf(dst,cap,"%s%s",dir,leaf);else snprintf(dst,cap,"%s/%s",dir,leaf);
}

static boole json_string_field(char const* json,char const* key,char* out,auint cap)
{
	char pat[96];char const* p;auint d=0U;if(out==NULL||cap==0U)return FALSE;out[0]=0;snprintf(pat,sizeof(pat),"\"%s\":\"",key);p=strstr(json,pat);if(p==NULL)return FALSE;p+=strlen(pat);while(*p&&*p!='\"'&&d+1U<cap){if(*p=='\\'&&p[1]){p++;if(*p=='n')out[d++]='\n';else if(*p=='r')out[d++]='\r';else if(*p=='t')out[d++]='\t';else out[d++]=*p;p++;}else out[d++]=*p++;}out[d]=0;return TRUE;
}

static web_upload_t* upload_find(auint id)
{
	auint i;for(i=0U;i<WEB_UPLOAD_SLOTS;i++)if(ws.uploads[i].active&&ws.uploads[i].id==id)return &ws.uploads[i];return NULL;
}

static void upload_clear(web_upload_t* u,boole remove_file)
{
	if(u==NULL){ return; }
	if(u->file){
		fclose(u->file);
		u->file=NULL;
	}
	if(remove_file&&u->path[0]){
		(void)remove(u->path);
	}
	memset(u,0,sizeof(*u));
}

static web_upload_t* upload_alloc(void)
{
	auint i;for(i=0U;i<WEB_UPLOAD_SLOTS;i++)if(!ws.uploads[i].active)return &ws.uploads[i];return NULL;
}

static void upload_start(web_client_t* c,char const* query)
{
	web_upload_t* u=upload_alloc();char name_in[512],name[256],dir[768],path[1024],leaf[320],jpath[2048],reply[2560];char mode[16];auint id,attempt=0U;
	if(u==NULL){static const unsigned char e[]="{\"ok\":0,\"error\":\"upload slots busy\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}
	query_value(query,"name",name_in,sizeof(name_in));query_value(query,"mode",mode,sizeof(mode));
	if(!query_value(query,"dir",dir,sizeof(dir))){char status[WEB_API_REPLY_CAP];dir[0]=0;if(api_command("EMU_STATUS",status,sizeof(status)))json_string_field(status,"rom_dir",dir,sizeof(dir));}
	if(strchr(dir,'\r')||strchr(dir,'\n')){static const unsigned char e[]="{\"ok\":0,\"error\":\"invalid upload directory\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}
	sanitize_filename(name,sizeof(name),name_in,"upload.uze");if(dir[0]==0)snprintf(dir,sizeof(dir),".");
	id=ws.next_upload_id++;if(ws.next_upload_id==0U)ws.next_upload_id=1U;
	join_path(path,sizeof(path),dir,name);
	while(path_exists(path)&&attempt<1000U){attempt++;snprintf(leaf,sizeof(leaf),"web-%08u-%03u-%s",(unsigned)id,(unsigned)attempt,name);join_path(path,sizeof(path),dir,leaf);}
	if(path_exists(path)){static const unsigned char e[]="{\"ok\":0,\"error\":\"could not choose upload filename\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}
	u->file=fopen(path,"wb");if(u->file==NULL){static const unsigned char e[]="{\"ok\":0,\"error\":\"cannot create uploaded ROM in configured ROM directory\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}
	u->active=TRUE;u->id=id;u->run_after=(strcmp(mode,"RUN")==0||strcmp(mode,"run")==0)?TRUE:FALSE;u->bytes=0U;snprintf(u->path,sizeof(u->path),"%s",path);snprintf(u->name,sizeof(u->name),"%s",name);
	json_escape_small(jpath,sizeof(jpath),u->path);snprintf(reply,sizeof(reply),"{\"ok\":1,\"id\":%u,\"path\":\"%s\",\"max_bytes\":%u}",(unsigned)id,jpath,(unsigned)WEB_UPLOAD_MAX_BYTES);http_reply(c,200,"application/json; charset=utf-8",(unsigned char*)reply,(auint)strlen(reply));
}

static void upload_chunk(web_client_t* c,char const* query)
{
	char idbuf[32];web_upload_t* u;auint id,len=request_content_length(c->rx);char const* body=find_body(c->rx);char reply[192];
	if(!query_value(query,"id",idbuf,sizeof(idbuf))){static const unsigned char e[]="{\"ok\":0,\"error\":\"missing upload id\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}id=(auint)strtoul(idbuf,NULL,0);u=upload_find(id);
	if(u==NULL||u->file==NULL){static const unsigned char e[]="{\"ok\":0,\"error\":\"upload not found\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}
	if(body==NULL||len>(WEB_UPLOAD_MAX_BYTES-u->bytes)){upload_clear(u,TRUE);static const unsigned char e[]="{\"ok\":0,\"error\":\"upload too large or invalid\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}
	if(len&&fwrite(body,1U,len,u->file)!=len){upload_clear(u,TRUE);static const unsigned char e[]="{\"ok\":0,\"error\":\"upload write failed\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}u->bytes+=len;
	snprintf(reply,sizeof(reply),"{\"ok\":1,\"id\":%u,\"received\":%u}",(unsigned)id,(unsigned)u->bytes);http_reply(c,200,"application/json; charset=utf-8",(unsigned char*)reply,(auint)strlen(reply));
}

static void upload_finish(web_client_t* c,char const* query)
{
	char idbuf[32],cmd[1400],reply[WEB_API_REPLY_CAP];web_upload_t* u;auint id;
	if(!query_value(query,"id",idbuf,sizeof(idbuf))){static const unsigned char e[]="{\"ok\":0,\"error\":\"missing upload id\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}id=(auint)strtoul(idbuf,NULL,0);u=upload_find(id);
	if(u==NULL){static const unsigned char e[]="{\"ok\":0,\"error\":\"upload not found\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}if(u->file){if(fflush(u->file)!=0){upload_clear(u,TRUE);static const unsigned char e[]="{\"ok\":0,\"error\":\"upload flush failed\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}fclose(u->file);u->file=NULL;}
	snprintf(cmd,sizeof(cmd),"LOAD_ROM %s %s",u->run_after?"RUN":"WAIT",u->path);if(!api_command(cmd,reply,sizeof(reply))){static const unsigned char e[]="{\"ok\":0,\"error\":\"CUzeBox API unavailable\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);upload_clear(u,TRUE);return;}
	http_reply(c,200,"application/json; charset=utf-8",(unsigned char*)reply,(auint)strlen(reply));upload_clear(u,(strstr(reply,"\"ok\":1")!=NULL)?FALSE:TRUE);
}

static void upload_cancel(web_client_t* c,char const* query)
{
	char idbuf[32];web_upload_t* u;if(!query_value(query,"id",idbuf,sizeof(idbuf))){static const unsigned char e[]="{\"ok\":0,\"error\":\"missing upload id\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}u=upload_find((auint)strtoul(idbuf,NULL,0));if(u)upload_clear(u,TRUE);{static const unsigned char ok[]="{\"ok\":1}";http_reply(c,200,"application/json",ok,sizeof(ok)-1);}
}

static boole json_uint(char const* json,char const* key,auint* out)
{
	char pat[64];char const* p;snprintf(pat,sizeof(pat),"\"%s\":",key);p=strstr(json,pat);if(p==NULL)return FALSE;p+=strlen(pat);*out=(auint)strtoul(p,NULL,10);return TRUE;
}

static auint parse_mem_values(char const* json,unsigned char* out,auint cap)
{
	char const* p=strstr(json,"\"values\":[");auint n=0U;if(p==NULL)return 0U;p+=10;while(*p&&*p!=']'&&n<cap){char* e;unsigned long v;while(*p==' '||*p==',')p++;if(*p==']')break;v=strtoul(p,&e,10);if(e==p)break;out[n++]=(unsigned char)(v&0xffU);p=e;}return n;
}

static void start_memory_dump(web_client_t* c,char const* query)
{
	char region[24],abuf[32],lbuf[32],name_in[256],name[256],cmd[128],reply[WEB_API_REPLY_CAP],h[1024];auint addr=0U,len=0U,size=0U;wsock_t s;
	if(!query_value(query,"region",region,sizeof(region)))snprintf(region,sizeof(region),"SRAM");
	if(strcmp(region,"SRAM")&&strcmp(region,"IO")&&strcmp(region,"FLASH")&&strcmp(region,"EEPROM")&&strcmp(region,"SPIRAM")){static const unsigned char e[]="{\"ok\":0,\"error\":\"invalid memory region\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}
	if(query_value(query,"addr",abuf,sizeof(abuf))){ addr=(auint)strtoul(abuf,NULL,0); }
	if(query_value(query,"len",lbuf,sizeof(lbuf))){ len=(auint)strtoul(lbuf,NULL,0); }
	s=api_open_socket();if(s==WSOCK_INVALID){static const unsigned char e[]="{\"ok\":0,\"error\":\"CUzeBox API unavailable\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}snprintf(cmd,sizeof(cmd),"MEM_SIZE %s",region);if(!api_socket_command(s,cmd,reply,sizeof(reply))||strstr(reply,"\"ok\":1")==NULL||!json_uint(reply,"size",&size)||addr>=size){WSOCK_CLOSE(s);static const unsigned char e[]="{\"ok\":0,\"error\":\"memory range unavailable\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}
	if(len==0U||len>(size-addr)){ len=size-addr; }
	if(len>WEB_DUMP_MAX_BYTES){WSOCK_CLOSE(s);static const unsigned char e[]="{\"ok\":0,\"error\":\"memory dump too large\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}
	if(!query_value(query,"name",name_in,sizeof(name_in))){
		snprintf(name_in,sizeof(name_in),"%s-%08X-%u.bin",region,(unsigned)addr,(unsigned)len);
	}
	sanitize_filename(name,sizeof(name),name_in,"memory.bin");
	snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %u\r\nContent-Disposition: attachment; filename=\"%s\"\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",(unsigned)len,name);queue_raw(c,h,(auint)strlen(h));c->dump_sock=s;c->dump_active=TRUE;c->dump_kind=WEB_DUMP_MEMORY;c->keep_open=TRUE;snprintf(c->dump_region,sizeof(c->dump_region),"%s",region);c->dump_addr=addr;c->dump_remaining=len;
}

static void start_file_download(web_client_t* c,char const* path,char const* name,boole delete_after) CU_UNUSED_FN;

static void start_file_download(web_client_t* c,char const* path,char const* name,boole delete_after)
{
	FILE* f=fopen(path,"rb");long z;char safe[256],h[1024];if(f==NULL){static const unsigned char e[]="{\"ok\":0,\"error\":\"download file unavailable\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}if(fseek(f,0,SEEK_END)!=0||(z=ftell(f))<0||fseek(f,0,SEEK_SET)!=0){fclose(f);static const unsigned char e[]="{\"ok\":0,\"error\":\"download file size unavailable\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}sanitize_filename(safe,sizeof(safe),name,"download.bin");snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %lu\r\nContent-Disposition: attachment; filename=\"%s\"\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",(unsigned long)z,safe);queue_raw(c,h,(auint)strlen(h));c->file_stream=f;c->file_stream_delete=delete_after;c->keep_open=TRUE;snprintf(c->file_stream_path,sizeof(c->file_stream_path),"%s",path);
}

static void pump_file_download(web_client_t* c)
{
	unsigned char data[32768];size_t n;if(c->file_stream==NULL||c->tx_len!=0U)return;n=fread(data,1U,sizeof(data),c->file_stream);if(n>0U){queue_raw(c,(char const*)data,(auint)n);if(feof(c->file_stream)){fclose(c->file_stream);c->file_stream=NULL;if(c->file_stream_delete&&c->file_stream_path[0])remove(c->file_stream_path);c->file_stream_delete=FALSE;c->file_stream_path[0]=0;c->keep_open=FALSE;}return;}fclose(c->file_stream);c->file_stream=NULL;if(c->file_stream_delete&&c->file_stream_path[0])remove(c->file_stream_path);c->file_stream_delete=FALSE;c->file_stream_path[0]=0;c->keep_open=FALSE;
}

static void start_screenshot_download(web_client_t* c,char const* query)
{
	char name_in[256],name[256],reply[WEB_API_REPLY_CAP],h[1024];
	auint size=0U,width=0U,height=0U;
	wsock_t s;
	if(!query_value(query,"name",name_in,sizeof(name_in)))snprintf(name_in,sizeof(name_in),"screenshot.bmp");
	sanitize_filename(name,sizeof(name),name_in,"screenshot.bmp");
	s=api_open_socket();
	if(s==WSOCK_INVALID){static const unsigned char e[]="{\"ok\":0,\"error\":\"CUzeBox API unavailable\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}
	if(!api_socket_command(s,"SCREENSHOT_CAPTURE",reply,sizeof(reply))||strstr(reply,"\"ok\":1")==NULL||!json_uint(reply,"size",&size)||size==0U){WSOCK_CLOSE(s);static const unsigned char e[]="{\"ok\":0,\"error\":\"screenshot capture failed\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}
	json_uint(reply,"width",&width);json_uint(reply,"height",&height);
	snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: image/bmp\r\nContent-Length: %u\r\nContent-Disposition: attachment; filename=\"%s\"\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nX-CUzeBox-Image-Size: %ux%u\r\nConnection: close\r\n\r\n",(unsigned)size,name,(unsigned)width,(unsigned)height);
	queue_raw(c,h,(auint)strlen(h));
	c->dump_sock=s;c->dump_active=TRUE;c->dump_kind=WEB_DUMP_SCREENSHOT;c->keep_open=TRUE;c->dump_addr=0U;c->dump_remaining=size;c->dump_region[0]=0;
}

static void finish_dump(web_client_t* c)
{
	char reply[256];
	if(c->dump_sock!=WSOCK_INVALID){
		if(c->dump_kind==WEB_DUMP_SCREENSHOT)api_socket_command(c->dump_sock,"SCREENSHOT_CLEAR",reply,sizeof(reply));
		WSOCK_CLOSE(c->dump_sock);c->dump_sock=WSOCK_INVALID;
	}
	c->dump_active=FALSE;c->keep_open=FALSE;c->dump_kind=WEB_DUMP_MEMORY;
}

static void pump_memory_dump(web_client_t* c)
{
	char cmd[128],reply[WEB_API_REPLY_CAP];unsigned char data[WEB_SCREENSHOT_CHUNK];auint ask,n,chunk;
	if(!c->dump_active||c->tx_len!=0U)return;
	chunk=(c->dump_kind==WEB_DUMP_SCREENSHOT)?WEB_SCREENSHOT_CHUNK:WEB_DUMP_CHUNK;
	ask=(c->dump_remaining>chunk)?chunk:c->dump_remaining;
	if(ask==0U){finish_dump(c);return;}
	if(c->dump_kind==WEB_DUMP_SCREENSHOT)snprintf(cmd,sizeof(cmd),"SCREENSHOT_READ %u %u",(unsigned)c->dump_addr,(unsigned)ask);
	else snprintf(cmd,sizeof(cmd),"READ_MEM %s %u %u",c->dump_region,(unsigned)c->dump_addr,(unsigned)ask);
	if(!api_socket_command(c->dump_sock,cmd,reply,sizeof(reply))||strstr(reply,"\"ok\":1")==NULL){finish_dump(c);return;}
	n=parse_mem_values(reply,data,ask);if(n!=ask){finish_dump(c);return;}
	queue_raw(c,(char const*)data,n);c->dump_addr+=n;c->dump_remaining-=n;if(c->dump_remaining==0U)finish_dump(c);
}

static void handle(web_client_t*c)
{
	char method[16],uri[1024];
	char* query=NULL;
	const web_asset_t*a;
	if(sscanf(c->rx,"%15s %1023s",method,uri)!=2){static const unsigned char e[]="bad request";http_reply(c,400,"text/plain",e,sizeof(e)-1);return;}
	query=strchr(uri,'?');if(query){*query++=0;}
	if(strcmp(method,"GET")==0){
		if(strcmp(uri,"/api/events")==0){start_events(c);return;}
		if(strcmp(uri,"/api/status")==0){char reply[512];if(api_command("GET_STATE",reply,sizeof(reply)))http_reply(c,200,"application/json; charset=utf-8",(unsigned char*)reply,(auint)strlen(reply));else{static const unsigned char e[]="{\"ok\":0,\"connected\":0,\"error\":\"CUzeBox API unavailable\"}";http_reply(c,200,"application/json; charset=utf-8",e,sizeof(e)-1);}return;}
		if(strcmp(uri,"/api/memory-dump")==0){start_memory_dump(c,query);return;}
		if(strcmp(uri,"/api/screenshot-download")==0){start_screenshot_download(c,query);return;}
		a=asset(uri);if(a){http_reply(c,200,a->mime,a->data,a->len);return;}
		{static const unsigned char nf[]="not found";http_reply(c,404,"text/plain",nf,sizeof(nf)-1);return;}
	}
	if(strcmp(method,"POST")==0){
		if(strcmp(uri,"/api/upload-rom/start")==0){upload_start(c,query);return;}
		if(strcmp(uri,"/api/upload-rom/chunk")==0){upload_chunk(c,query);return;}
		if(strcmp(uri,"/api/upload-rom/finish")==0){upload_finish(c,query);return;}
		if(strcmp(uri,"/api/upload-rom/cancel")==0){upload_cancel(c,query);return;}
		if(strcmp(uri,"/api/command")==0){
			const char*b=find_body(c->rx);char cmd[4096],reply[WEB_API_REPLY_CAP];const char*k;auint p=0U;
			if(!b){static const unsigned char e[]="{\"ok\":0,\"error\":\"missing body\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}
			k=strstr(b,"\"command\":");if(!k){static const unsigned char e[]="{\"ok\":0,\"error\":\"missing command\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}
			k=strchr(k,':');if(k)k++;while(k&&(*k==' '||*k=='\t'))k++;if(!k||*k!='\"'){static const unsigned char e[]="{\"ok\":0,\"error\":\"bad command\"}";http_reply(c,400,"application/json",e,sizeof(e)-1);return;}k++;
			while(*k&&*k!='\"'&&p+1U<sizeof(cmd)){if(*k=='\\'&&k[1]){k++;if(*k=='n'||*k=='r'){cmd[p++]=0;break;}cmd[p++]=*k++;}else cmd[p++]=*k++;}cmd[p]=0;
			if(api_command(cmd,reply,sizeof(reply)))http_reply(c,200,"application/json; charset=utf-8",(unsigned char*)reply,(auint)strlen(reply));else{static const unsigned char e[]="{\"ok\":0,\"error\":\"CUzeBox API unavailable\"}";http_reply(c,200,"application/json; charset=utf-8",e,sizeof(e)-1);}return;
		}
	}
	{static const unsigned char nf[]="not found";http_reply(c,404,"text/plain",nf,sizeof(nf)-1);}
}
/* Port selection: a second CUzeBox instance (e.g. a local link-cable pair)
** must not share the first instance's port. On Windows SO_REUSEADDR lets two
** processes bind the same port, after which connections are split between
** them unpredictably, so it is only used on POSIX (where it merely skips
** TIME_WAIT). When the configured port is taken, the next pair is tried:
** API 24680/web 24681, then 24682/24683, ... */
static auint web_bound_port=0U;
static auint web_listen_retry_ticks=0U;
static void open_listener(void){
	wsock_t s=WSOCK_INVALID;struct sockaddr_in sa;auint k,port=0U;
	if(!ws.enabled||ws.listen_sock!=WSOCK_INVALID)return;
	if(web_listen_retry_ticks!=0U){web_listen_retry_ticks--;return;} /* thread ticks about every 1 ms */
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	if(!ws.wsa_started){WSADATA w;if(WSAStartup(MAKEWORD(2,2),&w)!=0)return;ws.wsa_started=TRUE;}
#endif
	for(k=0U;k<WEB_PORT_TRIES;k++){
		port=ws.port+2U*k;if(port>65535U)break;
		s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==WSOCK_INVALID)break;
#if !(defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__))
		{int opt=1;setsockopt(s,SOL_SOCKET,SO_REUSEADDR,(const char*)&opt,sizeof(opt));}
#endif
		if(!nonblock(s)){WSOCK_CLOSE(s);s=WSOCK_INVALID;break;}
		memset(&sa,0,sizeof(sa));sa.sin_family=AF_INET;sa.sin_port=htons((uint16)(port&0xffffU));sa.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
		if(bind(s,(struct sockaddr*)&sa,(socklen_t)sizeof(sa))==0&&listen(s,64)==0)break;
		WSOCK_CLOSE(s);s=WSOCK_INVALID;
	}
	if(s==WSOCK_INVALID){web_listen_retry_ticks=WEB_LISTEN_RETRY_MS;return;}
	web_listen_retry_ticks=0U;ws.listen_sock=s;web_bound_port=port;
	print_info("WEB: listening on http://127.0.0.1:%u/\n",(unsigned)port);
	if(port!=ws.port)print_info("WEB: port %u was busy (another CUzeBox instance?), using %u\n",(unsigned)ws.port,(unsigned)port);
}
static int web_thread_main(void* unused){(void)unused;while(!ws.stop_thread){web_server_tick();SDL_Delay(1);}return 0;}
void web_server_configure(boole e,auint p,auint ap){init_once();if(p)ws.port=p;if(ap)ws.api_port=ap;if(!e){web_server_shutdown();return;}ws.enabled=TRUE;if(ws.thread==NULL){ws.stop_thread=FALSE;ws.thread=SDL_CreateThread(web_thread_main,"CUzeBox Web",NULL);if(ws.thread==NULL){ws.enabled=FALSE;}}}
void web_server_set_api_port(auint ap){init_once();if(ap!=0U&&ap!=ws.api_port){ws.api_port=ap;}}
boole web_server_is_enabled(void){init_once();return ws.enabled;} auint web_server_get_port(void){init_once();return (ws.listen_sock!=WSOCK_INVALID&&web_bound_port!=0U)?web_bound_port:ws.port;}
void web_server_tick(void)
{
	auint i;
	init_once();
	if (!ws.enabled){ return; }
	open_listener();
	if (ws.listen_sock != WSOCK_INVALID){
		for (;;){
			wsock_t s;
			/* Only accept when a slot is free. Excess browser connections
			** wait in the listen backlog instead of being reset, which the
			** page would report as the bridge disconnecting. Each open page
			** holds one slot for its /api/events stream. */
			for (i = 0U; i < WEB_MAX_CLIENTS; ++i){
				if (ws.clients[i].sock == WSOCK_INVALID){ break; }
			}
			if (i == WEB_MAX_CLIENTS){ break; }
			s = accept(ws.listen_sock, NULL, NULL);
			if (s == WSOCK_INVALID){ break; }
			nonblock(s);
			ws.clients[i].sock = s;
			ws.clients[i].event_sock = WSOCK_INVALID;
			ws.clients[i].dump_sock = WSOCK_INVALID;
		}
	}
	for (i = 0U; i < WEB_MAX_CLIENTS; ++i){
		web_client_t* c = &ws.clients[i];
		if (c->sock == WSOCK_INVALID){ continue; }
		if (c->tx_off < c->tx_len){
			int w = (int)send(c->sock, c->tx + c->tx_off, (int)(c->tx_len - c->tx_off), 0);
			if (w > 0){ c->tx_off += (auint)w; }
			else if ((w < 0) && (!would_block())){ close_client(i); continue; }
			if (c->tx_off >= c->tx_len){
				c->tx_off = 0U;
				c->tx_len = 0U;
				if (!c->keep_open){ close_client(i); continue; }
			}
		}
		pump_events(c);
		pump_memory_dump(c);
		pump_file_download(c);
		if ((c->sock == WSOCK_INVALID) || c->keep_open || (c->tx_len != 0U)){ continue; }
		{
			int r = (int)recv(c->sock, c->rx + c->rx_len, (int)(WEB_RX_CAP - 1U - c->rx_len), 0);
			if (r > 0){
				c->rx_len += (auint)r;
				c->rx[c->rx_len] = 0;
				if (strstr(c->rx, "\r\n\r\n") != NULL){
					char* cl = strstr(c->rx, "Content-Length:");
					auint need = 0U;
					char* body;
					if (cl != NULL){ need = (auint)strtoul(cl + 15, NULL, 10); }
					body = (char*)find_body(c->rx);
					if ((body != NULL) && (((auint)(body-c->rx) + need) >= WEB_RX_CAP)){ static const unsigned char e[]="request body too large"; http_reply(c,400,"text/plain",e,sizeof(e)-1); }
					else if ((body != NULL) && ((auint)(c->rx + c->rx_len - body) >= need)){ handle(c); }
				}
			}else if (r == 0){ close_client(i); }
			else if (!would_block()){ close_client(i); }
		}
	}
}
void web_server_shutdown(void){auint i;SDL_Thread* t;init_once();ws.stop_thread=TRUE;t=ws.thread;ws.thread=NULL;if(t!=NULL)SDL_WaitThread(t,NULL);ws.enabled=FALSE;api_command_drop();for(i=0;i<WEB_MAX_CLIENTS;i++)close_client(i);for(i=0;i<WEB_UPLOAD_SLOTS;i++)if(ws.uploads[i].active)upload_clear(&ws.uploads[i],TRUE);if(ws.listen_sock!=WSOCK_INVALID){WSOCK_CLOSE(ws.listen_sock);ws.listen_sock=WSOCK_INVALID;}
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
 if(ws.wsa_started){WSACleanup();ws.wsa_started=FALSE;}
#endif
}
#endif
