"""Exercise the actual connect helper with Winsock's exception-only failure."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'cu_esp_net.c').read_text()
start=source.index('static sint32 cu_sock_connect_timeout(')
brace=source.index('{',start);depth=1;end=brace+1
while depth:
    depth+=(source[end]=='{')-(source[end]=='}');end+=1
function=source[start:end]
mock=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define _WIN32 1
typedef int32_t sint32;typedef int ESP_SOCKET;typedef int socklen_t;
struct sockaddr {int unused;};struct timeval {long tv_sec,tv_usec;};
typedef unsigned fd_set;
#define FD_ZERO(p) (*(p)=0)
#define FD_SET(s,p) (*(p)|=1u<<(s))
#define WSAEWOULDBLOCK 10035
#define WSAEINPROGRESS 10036
#define WSAEALREADY 10037
#define SOL_SOCKET 1
#define SO_ERROR 4
static int mode,queried,observed_failure,option_failed;
static void cu_sock_set_nonblocking(int s){(void)s;}
static int connect(int s,const struct sockaddr *a,int n){(void)s;(void)a;(void)n;return -1;}
static int WSAGetLastError(void){return option_failed?10038:WSAEWOULDBLOCK;}
static int select(int n,fd_set*r,fd_set*w,fd_set*e,struct timeval*t){
 (void)n;(void)r;(void)t;
 if(mode==2)return 0;
 if(mode==1){observed_failure=e&&(*e&8u);*w=0;return observed_failure?1:0;}
 return !!(*w&8u);
}
static int getsockopt(int s,int level,int option,char*out,int*len){
 (void)s;(void)level;(void)option;(void)len;queried++;
 if(mode==3){option_failed=1;return -1;}
 int error=mode==1?10061:0;memcpy(out,&error,sizeof(error));return 0;
}
'''
checks=r'''
int main(void){
 struct sockaddr a;
 mode=1;if(cu_sock_connect_timeout(3,&a,sizeof(a),5000)!=-10061||!observed_failure)return 1;
 mode=0;if(cu_sock_connect_timeout(3,&a,sizeof(a),5000)!=0)return 2;
 mode=2;if(cu_sock_connect_timeout(3,&a,sizeof(a),5000)!=-1)return 3;
 mode=3;if(cu_sock_connect_timeout(3,&a,sizeof(a),5000)!=-10038)return 4;
 if(queried!=3)return 5;
 puts("connect helper: Winsock exception refusal, success, timeout and SO_ERROR query failure PASS");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path=Path(tmp);(path/'test.c').write_text(mock+function+checks)
    subprocess.run([os.environ.get('CC','cc'),'-std=gnu17','-Wall','-Wextra',str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
