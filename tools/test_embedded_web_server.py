#!/usr/bin/env python3
import http.client, os, pathlib, shutil, socket, subprocess, tempfile, threading, time, json, urllib.parse
ROOT=pathlib.Path(__file__).resolve().parent.parent
SDL=r'''
#ifndef SDL_H
#define SDL_H
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
typedef struct SDL_Thread { pthread_t t; int (*fn)(void*); void* data; int rc; } SDL_Thread;
static void* sdl_tramp(void* p){ SDL_Thread* t=(SDL_Thread*)p; t->rc=t->fn(t->data); return 0; }
static inline SDL_Thread* SDL_CreateThread(int (*fn)(void*),const char* name,void* data){(void)name;SDL_Thread*t=calloc(1,sizeof(*t));if(!t)return 0;t->fn=fn;t->data=data;if(pthread_create(&t->t,0,sdl_tramp,t)){free(t);return 0;}return t;}
static inline void SDL_WaitThread(SDL_Thread*t,int*status){if(!t)return;pthread_join(t->t,0);if(status)*status=t->rc;free(t);}
static inline void SDL_Delay(unsigned int ms){usleep(ms*1000U);}
#endif
'''
HARNESS=r'''
#include <stdlib.h>
#include <unistd.h>
#include "web_server.h"
int main(int argc,char**argv){unsigned p=(argc>1)?(unsigned)strtoul(argv[1],0,0):24681U;unsigned ap=(argc>2)?(unsigned)strtoul(argv[2],0,0):24680U;web_server_configure(TRUE,p,ap);sleep(4);web_server_shutdown();return 0;}
'''
def free_port():
 s=socket.socket();s.bind(('127.0.0.1',0));p=s.getsockname()[1];s.close();return p
class FakeAPI(threading.Thread):
 def __init__(self,port): super().__init__(daemon=True);self.port=port;self.stop=False;self.s=None;self.mem=bytes((i*7+3)&255 for i in range(4096));self.screenshot=b'BM'+bytes(range(64));self.loaded_path=None;self.loaded_bytes=None;self.rom_dir='.'
 def run(self):
  s=socket.socket();self.s=s;s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);s.bind(('127.0.0.1',self.port));s.listen();s.settimeout(.1)
  while not self.stop:
   try:c,_=s.accept()
   except socket.timeout:continue
   threading.Thread(target=self.client,args=(c,),daemon=True).start()
 def client(self,c):
  try:
   c.sendall(b'{"ok":1,"hello":"CUzeBox API"}\n');f=c.makefile('rb')
   for raw in f:
    cmd=raw.decode().strip()
    if cmd.startswith('SUBSCRIBE'):
     c.sendall(b'{"ok":1,"subscriptions":63}\n');time.sleep(.05);c.sendall(b'{"event":"FRAME","frame":123,"paused":0}\n');time.sleep(1);break
    elif cmd=='GET_STATE': c.sendall(b'{"ok":1,"frame":123,"paused":0}\n')
    elif cmd=='PING': c.sendall(b'{"ok":1,"reply":"PONG"}\n')
    elif cmd=='EMU_STATUS': c.sendall((json.dumps({'ok':1,'rom_dir':self.rom_dir},separators=(',',':'))+'\n').encode())
    elif cmd.startswith('MEM_SIZE '):
     reg=cmd.split()[1].upper();sizes={'SRAM':4096,'IO':256,'FLASH':65536,'EEPROM':2048,'SPIRAM':8192};c.sendall((json.dumps({'ok':1,'region':reg,'size':sizes.get(reg,0)},separators=(',',':'))+'\n').encode())
    elif cmd.startswith('READ_MEM '):
     _,reg,a,n=cmd.split();a=int(a,0);n=int(n,0);vals=list(self.mem[a:a+n]);c.sendall((json.dumps({'ok':1,'addr':a,'len':len(vals),'values':vals},separators=(',',':'))+'\n').encode())
    elif cmd.startswith('LOAD_ROM '):
     parts=cmd.split(' ',2);path=parts[2];self.loaded_path=path
     try:self.loaded_bytes=pathlib.Path(path).read_bytes();c.sendall(b'{"ok":1,"loaded":1}\n')
     except OSError:c.sendall(b'{"ok":0,"error":"missing upload"}\n')
    elif cmd=='SCREENSHOT_CAPTURE': c.sendall((json.dumps({'ok':1,'size':len(self.screenshot),'width':4,'height':4,'format':'bmp24'},separators=(',',':'))+'\n').encode())
    elif cmd.startswith('SCREENSHOT_READ '):
     _,a,n=cmd.split();a=int(a,0);n=int(n,0);vals=list(self.screenshot[a:a+n]);c.sendall((json.dumps({'ok':1,'offset':a,'len':len(vals),'values':vals},separators=(',',':'))+'\n').encode())
    elif cmd=='SCREENSHOT_CLEAR': c.sendall(b'{"ok":1}\n')
    elif cmd.startswith('VIDEO_BEAM'):
     vals=[i&255 for i in range(1820)];c.sendall((json.dumps({'ok':1,'cycle':321,'valid_cycles':321,'line_cycles':1820,'hsync_cycles':136,'active_begin':299,'active_cycles':1440,'active_end':1739,'pulse_counter':41,'last_completed_pulse':40,'start':0,'count':1820,'pixels':vals},separators=(',',':'))+'\n').encode())
    else: c.sendall(b'{"ok":0,"error":"test"}\n')
  finally:c.close()
 def close(self):
  self.stop=True
  if self.s:self.s.close()
def wait_http(port):
 end=time.time()+3
 while time.time()<end:
  try:
   c=http.client.HTTPConnection('127.0.0.1',port,timeout=.2);c.request('GET','/');r=c.getresponse();b=r.read();c.close()
   if r.status==200:return b
  except OSError:time.sleep(.02)
 raise RuntimeError('embedded web server did not start')
def main():
 cc=os.environ.get('CC') or shutil.which('cc') or shutil.which('gcc')
 if not cc: raise SystemExit('No host C compiler')
 with tempfile.TemporaryDirectory(prefix='cuzebox-web-') as td:
  d=pathlib.Path(td);(d/'SDL2').mkdir();(d/'SDL2'/'SDL.h').write_text(SDL);(d/'h.c').write_text(HARNESS)
  exe=d/'webtest';subprocess.run([cc,'-std=gnu99','-DENABLE_API_SERVER=1','-I',str(d),'-I',str(ROOT),str(ROOT/'web_server.c'),str(d/'h.c'),'-pthread','-o',str(exe)],check=True)
  hp,ap=free_port(),free_port();api=FakeAPI(ap);api.rom_dir=td;api.start();proc=subprocess.Popen([str(exe),str(hp),str(ap)])
  try:
   home=wait_http(hp);assert b'CUzeBox Web Tools' in home
   c=http.client.HTTPConnection('127.0.0.1',hp,timeout=2);c.request('GET','/debugger');r=c.getresponse();dbg=r.read();c.close();assert r.status==200 and b'Cycle-domain scanline beam' in dbg and b'VIDEO_BEAM' in dbg
   c=http.client.HTTPConnection('127.0.0.1',hp,timeout=2);c.request('GET','/sd');r=c.getresponse();sd=r.read();c.close();assert r.status==200 and b'SLOW' in sd and b'NORMAL' in sd and b'FAST' in sd and b'CUSTOM' in sd and b'inter-sector' in sd
   c=http.client.HTTPConnection('127.0.0.1',hp,timeout=2);c.request('GET','/audio');r=c.getresponse();aud=r.read();c.close();assert r.status==200 and b'Audio Scope' in aud and b'AUDIO_SCOPE' in aud and b'zero scope hot-path overhead' in aud
   c=http.client.HTTPConnection('127.0.0.1',hp,timeout=2);body=json.dumps({'command':'PING'});c.request('POST','/api/command',body,{'Content-Type':'application/json'});r=c.getresponse();obj=json.loads(r.read());c.close();assert obj['reply']=='PONG'
   # Binary memory downloads are streamed through repeated API READ_MEM calls.
   c=http.client.HTTPConnection('127.0.0.1',hp,timeout=4);c.request('GET','/api/memory-dump?region=SRAM&addr=17&len=700&name=test.bin');r=c.getresponse();dump=r.read();hdr=r.getheader('Content-Disposition');c.close();assert r.status==200 and dump==api.mem[17:717] and 'test.bin' in (hdr or '')

   c=http.client.HTTPConnection('127.0.0.1',hp,timeout=4);c.request('GET','/api/screenshot-download?name=shot.bmp');r=c.getresponse();shot=r.read();hdr=r.getheader('Content-Disposition');c.close();assert r.status==200 and shot==b'BM'+bytes(range(64)) and 'shot.bmp' in (hdr or '') and r.getheader('Content-Type')=='image/bmp'

   # Browser ROM upload accepts binary bodies (including NULs), stores a persistent
   # copy, then asks the ordinary API to load the resulting path.
   payload=bytes((i*13+5)&255 for i in range(120000));q=urllib.parse.urlencode({'name':'upload test.uze','mode':'WAIT'})
   c=http.client.HTTPConnection('127.0.0.1',hp,timeout=4);c.request('POST','/api/upload-rom/start?'+q,b'');r=c.getresponse();up=json.loads(r.read());c.close();assert up['ok']==1
   uid=up['id'];received=0
   for off in range(0,len(payload),48*1024):
    part=payload[off:off+48*1024];c=http.client.HTTPConnection('127.0.0.1',hp,timeout=4);c.request('POST',f'/api/upload-rom/chunk?id={uid}',part,{'Content-Type':'application/octet-stream'});r=c.getresponse();ch=json.loads(r.read());c.close();received+=len(part);assert ch['received']==received
   c=http.client.HTTPConnection('127.0.0.1',hp,timeout=4);c.request('POST',f'/api/upload-rom/finish?id={uid}',b'');r=c.getresponse();fin=json.loads(r.read());c.close();assert fin['ok']==1 and api.loaded_bytes==payload and pathlib.Path(api.loaded_path).exists()

   s=socket.create_connection(('127.0.0.1',hp),timeout=2);s.sendall(b'GET /api/events HTTP/1.1\r\nHost: localhost\r\n\r\n');data=b'';end=time.time()+2
   while time.time()<end and b'"event":"FRAME"' not in data:data+=s.recv(4096)
   s.close();assert b'text/event-stream' in data and b'"event":"FRAME"' in data
  finally:
   api.close();proc.terminate();proc.wait(timeout=2)
 print('embedded web server regression: PASS')
 return 0
if __name__=='__main__':raise SystemExit(main())
