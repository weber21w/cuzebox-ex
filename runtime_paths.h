/* Native package startup. Legacy source/handheld launches retain their CWD.
 * Bundled resources are read-only inputs; all generated files use a writable
 * working directory. Explicit paths are resolved before switching directory. */
#ifndef CUEX_RUNTIME_PATHS_H
#define CUEX_RUNTIME_PATHS_H
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(_WIN32)
#include <direct.h>
#define RP_GETCWD _getcwd
#define RP_CHDIR _chdir
#define RP_MKDIR(p) _mkdir(p)
#define RP_PATH_SEP ';'
#else
#define RP_GETCWD getcwd
#define RP_CHDIR chdir
#define RP_MKDIR(p) mkdir(p,0700)
#define RP_PATH_SEP ':'
#endif
#ifndef O_BINARY
#define O_BINARY 0
#endif
#define RP_CAP 1024U
enum { RP_AUTO, RP_LEGACY, RP_USER, RP_PORTABLE };
static struct {
 int mode;
 char initial[RP_CAP],data[RP_CAP],resources[RP_CAP],config[RP_CAP],binary[RP_CAP];
} runtime_paths;

static boole rp_copy(char* out,size_t cap,char const* in)
{if(strlen(in)>=cap){errno=ENAMETOOLONG;return FALSE;}memcpy(out,in,strlen(in)+1U);return TRUE;}
static boole rp_join(char* out,size_t cap,char const* a,char const* b)
{
 int n=snprintf(out,cap,"%s%s%s",a,(!a[0]||a[strlen(a)-1U]=='/'||a[strlen(a)-1U]=='\\')?"":"/",b);
 if(n<0||(size_t)n>=cap){errno=ENAMETOOLONG;return FALSE;}return TRUE;
}
static boole rp_absolute(char* out,size_t cap,char const* name)
{
 boole absolute=name[0]=='/';
#if defined(_WIN32)
 absolute=absolute||name[0]=='\\'||(name[0]&&name[1]==':');
#endif
 return absolute?rp_copy(out,cap,name):rp_join(out,cap,runtime_paths.initial,name);
}
static boole rp_file(char const* path)
{struct stat st;return stat(path,&st)==0&&S_ISREG(st.st_mode);}
static boole rp_mkdirs(char const* path)
{
 char tmp[RP_CAP];size_t i;struct stat st;
 if(!path||!path[0])return FALSE;
 if(!rp_copy(tmp,sizeof(tmp),path))return FALSE;
 for(i=1U;;i++){
  char saved=tmp[i];
#if defined(_WIN32)
  /* _stat("C:") fails in the MinGW CRT although _mkdir("C:") reports
   * EEXIST. The drive prefix is not a directory component to create. */
  if(i==2U&&tmp[1]==':'&&(saved=='/'||saved=='\\'))continue;
#endif
  if(!saved||saved=='/'||saved=='\\'){
   tmp[i]=0;
   if(tmp[0]){
    if(stat(tmp,&st)==0){if(!S_ISDIR(st.st_mode)){errno=ENOTDIR;return FALSE;}}
    else if(RP_MKDIR(tmp)!=0&&(errno!=EEXIST||stat(tmp,&st)!=0||!S_ISDIR(st.st_mode)))return FALSE;
   }
   tmp[i]=saved;
  }
  if(!saved)break;
 }
 return TRUE;
}
/* Exclusive creation preserves existing configs and user-edited templates. */
static boole rp_seed_file(char const* source,char const* dest)
{
 unsigned char buf[8192];FILE *in,*out;size_t n;int fd;boole ok=TRUE;
 if(!rp_file(source))return TRUE;
 fd=open(dest,O_WRONLY|O_CREAT|O_EXCL|O_BINARY,0600);
 if(fd<0)return errno==EEXIST;
 out=fdopen(fd,"wb");if(!out){close(fd);remove(dest);return FALSE;}
 in=fopen(source,"rb");if(!in){fclose(out);remove(dest);return FALSE;}
 while((n=fread(buf,1U,sizeof(buf),in))!=0U)if(fwrite(buf,1U,n,out)!=n){ok=FALSE;break;}
 if(ferror(in))ok=FALSE;
 fclose(in);if(fclose(out)!=0)ok=FALSE;
 if(!ok)remove(dest);
 return ok;
}
static boole rp_seed_resources(boole explicit_config)
{
 char src[RP_CAP],dst[RP_CAP];DIR* dir;struct dirent* entry;
 if(!explicit_config){
  if(!rp_join(src,sizeof(src),runtime_paths.resources,"config.default.cfg")||!rp_seed_file(src,runtime_paths.config))return FALSE;
 }
 if(!rp_join(src,sizeof(src),runtime_paths.resources,"themes"))return FALSE;
 dir=opendir(src);
 if(dir){
  if(!rp_mkdirs("themes")){closedir(dir);return FALSE;}
  while((entry=readdir(dir))!=NULL){
   char source[RP_CAP];size_t len=strlen(entry->d_name);
   if(entry->d_name[0]=='.'||len<4U||strcmp(entry->d_name+len-4U,".cfg"))continue;
   if(!rp_join(source,sizeof(source),src,entry->d_name)||!rp_join(dst,sizeof(dst),"themes",entry->d_name)||!rp_seed_file(source,dst)){closedir(dir);return FALSE;}
  }
  closedir(dir);
 }
 if(!rp_join(src,sizeof(src),runtime_paths.resources,"gamecontrollerdb.txt"))return FALSE;
 return rp_seed_file(src,"gamecontrollerdb.txt");
}
static boole runtime_paths_prepare(int requested,char const* data_override,char const* resource_override,
                                   char const* config_override,char* rom,size_t rom_cap)
{
 char *base=NULL,*pref=NULL;char candidate[RP_CAP],marker[RP_CAP],absolute[RP_CAP];
 int mode=requested;
 memset(&runtime_paths,0,sizeof(runtime_paths));
 if(!RP_GETCWD(runtime_paths.initial,sizeof(runtime_paths.initial)))return FALSE;
#ifndef USE_SDL1
 base=SDL_GetBasePath();
#endif
 if(!rp_copy(runtime_paths.binary,sizeof(runtime_paths.binary),base?base:runtime_paths.initial)){SDL_free(base);return FALSE;}
 SDL_free(base);
 if(!rp_copy(runtime_paths.resources,sizeof(runtime_paths.resources),runtime_paths.binary))return FALSE;
 /* SDL returns Resources for macOS bundles, and the executable directory on
  * other native targets. Prefer an installed Linux share directory if found. */
 if(rp_join(candidate,sizeof(candidate),runtime_paths.binary,"../share/cuex")&&rp_join(marker,sizeof(marker),candidate,"config.default.cfg")&&rp_file(marker))rp_copy(runtime_paths.resources,sizeof(runtime_paths.resources),candidate);
 if(rp_join(marker,sizeof(marker),runtime_paths.binary,"../Resources/config.default.cfg")&&rp_file(marker))rp_join(runtime_paths.resources,sizeof(runtime_paths.resources),runtime_paths.binary,"../Resources");
 if(resource_override&&resource_override[0]&&!rp_absolute(runtime_paths.resources,sizeof(runtime_paths.resources),resource_override))return FALSE;
 if(mode==RP_AUTO){
  if(rp_join(marker,sizeof(marker),runtime_paths.binary,"cuex-portable")&&rp_file(marker))mode=RP_PORTABLE;
  else if(rp_join(marker,sizeof(marker),runtime_paths.resources,"cuex-user-data")&&rp_file(marker))mode=RP_USER;
  else mode=RP_LEGACY;
 }
 if(data_override&&data_override[0])mode=RP_USER;
 runtime_paths.mode=mode;
 if(mode==RP_LEGACY){
  rp_copy(runtime_paths.data,sizeof(runtime_paths.data),runtime_paths.initial);
  return rp_copy(runtime_paths.config,sizeof(runtime_paths.config),config_override?config_override:"config.cfg");
 }
 /* Preserve command-line ROM/config meaning when a package changes CWD. */
 if(rom&&rom[0]){if(!rp_absolute(absolute,sizeof(absolute),rom)||!rp_copy(rom,rom_cap,absolute))return FALSE;}
 if(config_override){if(!rp_absolute(runtime_paths.config,sizeof(runtime_paths.config),config_override))return FALSE;}
 if(mode==RP_PORTABLE){
  char const* image=getenv("APPIMAGE");
  if(image&&image[0]){
   char* slash;
   if(!rp_absolute(runtime_paths.data,sizeof(runtime_paths.data),image))return FALSE;
   slash=strrchr(runtime_paths.data,'/');if(!slash)return FALSE;
   if(slash==runtime_paths.data)slash[1]=0;else *slash=0;
  }else rp_copy(runtime_paths.data,sizeof(runtime_paths.data),runtime_paths.binary);
 }
 else if(data_override){if(!rp_absolute(runtime_paths.data,sizeof(runtime_paths.data),data_override))return FALSE;}
 else{
#ifndef USE_SDL1
  pref=SDL_GetPrefPath("uzenet","CUEX");
#endif
  if(!pref||!rp_copy(runtime_paths.data,sizeof(runtime_paths.data),pref)){SDL_free(pref);return FALSE;}
  SDL_free(pref);
 }
 if(!config_override&&!rp_join(runtime_paths.config,sizeof(runtime_paths.config),runtime_paths.data,"config.cfg"))return FALSE;
 /* ESP and application path preferences currently use 256-byte paths. */
 if(strlen(runtime_paths.config)>=APPCFG_PATH_MAX){errno=ENAMETOOLONG;return FALSE;}
 if(!rp_mkdirs(runtime_paths.data)||RP_CHDIR(runtime_paths.data)!=0)return FALSE;
 if(mode==RP_USER||mode==RP_PORTABLE){
  if(!rp_mkdirs("roms")||!rp_seed_resources(config_override!=NULL))return FALSE;
 }
 /* Bundled FFmpeg/curl helpers are selected without a shell wrapper or a
  * dependency on Homebrew/system PATH. macOS helpers live in MacOS/. */
 if(rp_join(candidate,sizeof(candidate),runtime_paths.resources,"../MacOS/ffmpeg")&&rp_file(candidate))rp_join(runtime_paths.binary,sizeof(runtime_paths.binary),runtime_paths.resources,"../MacOS");
 if(rp_join(candidate,sizeof(candidate),runtime_paths.binary,
#if defined(_WIN32)
  "ffmpeg.exe"
#else
  "ffmpeg"
#endif
 )&&rp_file(candidate)){
  char path[8192];char const* previous=getenv("PATH");
  int n=snprintf(path,sizeof(path),"%s%c%s",runtime_paths.binary,RP_PATH_SEP,previous?previous:"");
  if(n<0||(size_t)n>=sizeof(path))return FALSE;
#ifndef USE_SDL1
  if(SDL_setenv("PATH",path,1)!=0)return FALSE;
#else
  {char* env=(char*)malloc(strlen(path)+6U);if(!env)return FALSE;
   sprintf(env,"PATH=%s",path);if(putenv(env)!=0){free(env);return FALSE;}}
#endif
 }
 return TRUE;
}
static void rp_print_string(char const* value)
{
 unsigned char const* p=(unsigned char const*)value;putchar('"');
 for(;*p;p++){if(*p=='"'||*p=='\\'){putchar('\\');putchar(*p);}else if(*p<32U)printf("\\u%04x",*p);else putchar(*p);}
 putchar('"');
}
static void runtime_paths_print(void)
{
 printf("{\"mode\":\"%s\",\"config\":",runtime_paths.mode==RP_USER?"user-data":runtime_paths.mode==RP_PORTABLE?"portable":"legacy");
 rp_print_string(runtime_paths.config);printf(",\"data\":");rp_print_string(runtime_paths.data);
 printf(",\"resources\":");rp_print_string(runtime_paths.resources);puts("}");
}
#endif
