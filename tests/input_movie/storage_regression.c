#define SDL_MAIN_HANDLED
#undef main
#include "../../cu_vfat.h"
#define main storage_emulator_main
#include "../../main.c"
#undef main
#include "../../capture.c"
#include "../../savestate.c"
#include <time.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <process.h>
#define STATE_TEST_PID _getpid
#define STATE_TEST_CWD _getcwd
#define STATE_TEST_CD _chdir
#define STATE_TEST_RMDIR _rmdir
#else
#include <unistd.h>
#define STATE_TEST_PID getpid
#define STATE_TEST_CWD getcwd
#define STATE_TEST_CD chdir
#define STATE_TEST_RMDIR rmdir
#endif

#define CHECK(c, message) do { \
 if (!(c)){ fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); return 1; } \
} while (0)

static uint32 reference_crc(uint32 value, uint8 const *data, size_t size)
{
 size_t i;
 unsigned bit;
 for (i = 0; i < size; ++i){
  value ^= data[i];
  for (bit = 0; bit < 8; ++bit){
   value = (value >> 1) ^ ((0U - (value & 1U)) & 0xEDB88320U);
  }
 }
 return value;
}

static int crc_test(boole benchmark)
{
 uint8 data[65536];
 uint32 random = 1234567U;
 unsigned i, length;
 CHECK(crc(NULL, 0) == 0U, "empty CRC");
 CHECK(crc((uint8 const*)"123456789", 9) == 0xCBF43926U, "IEEE CRC32 known vector");
 for (i = 0; i < sizeof(data); ++i){
  random = random * 1664525U + 1013904223U;
  data[i] = (uint8)(random >> 24);
 }
 for (length = 0; length <= sizeof(data); length = length < 64 ? length + 1 : length * 2){
  uint32 expected = reference_crc(random, data, length);
  uint32 first = crc_bytes(random, data, length / 2);
  CHECK(crc_bytes(random, data, length) == expected, "random payload/initial CRC matches bitwise reference");
  CHECK(crc_bytes(first, data + length / 2, length - length / 2) == expected, "incremental CRC matches");
 }
 if (benchmark){
  uint32 (*functions[])(uint32, uint8 const*, size_t) = { reference_crc, crc_bytes };
  unsigned trial, algorithm;
  volatile uint32 digest = 0;
  for (trial = 0; trial < 3; ++trial){
   for (algorithm = 0; algorithm < 2; ++algorithm){
    clock_t start = clock();
    for (i = 0; i < 512; ++i){ digest = functions[algorithm](digest, data, sizeof(data)); }
    printf("CRC %s, 32 MiB, trial %u: %.6f s (digest %08x)\n",
           algorithm ? "nibble" : "bitwise", trial + 1,
           (double)(clock() - start) / CLOCKS_PER_SEC, (unsigned)digest);
   }
  }
 }
 return 0;
}

static int chunks_test(void)
{
 uint8 payload[524], header[12], *buffer = NULL, *saved;
 uint32 size, capacity = 0;
 char type[4];
 FILE *file = fopen("chunks.bin", "w+b");
 CHECK(file, "temporary chunk file");
 memset(payload, 0xA5, sizeof(payload));
 capture_file = file; capture_failed = FALSE;
 CHECK(write_chunk("SECT", payload, sizeof(payload)), "write sector chunk");
 CHECK(write_chunk("LACH", payload, 28), "write smaller chunk");
 CHECK(write_chunk("TEST", NULL, 0), "write zero length chunk");
 rewind(file);
 CHECK(read_chunk(file, type, &buffer, &size, &capacity) && size == sizeof(payload), "read sector chunk");
 CHECK(!memcmp(buffer, payload, size), "chunk payload preserved");
 saved = buffer;
 CHECK(read_chunk(file, type, &buffer, &size, &capacity) && size == 28 && !memcmp(type, "LACH", 4), "read following latch");
 CHECK(buffer == saved && capacity == sizeof(payload), "reuse bounded buffer for smaller chunk");
 CHECK(read_chunk(file, type, &buffer, &size, &capacity) && size == 0, "empty chunk CRC");
 CHECK(!read_chunk(file, type, &buffer, &size, &capacity), "EOF rejected as a chunk");
 rewind(file);
 memcpy(header, "TEST", 4); put32(header + 4, MOVIE_MAX_CHUNK + 1U); put32(header + 8, 0);
 CHECK(fwrite(header, 1, sizeof(header), file) == sizeof(header), "oversized fixture");
 rewind(file);
 CHECK(!read_chunk(file, type, &buffer, &size, &capacity) && capacity == sizeof(payload), "oversize rejected before allocation");
 rewind(file); put32(header + 4, sizeof(payload)); put32(header + 8, crc(payload, sizeof(payload)) ^ 1U);
 CHECK(fwrite(header, 1, sizeof(header), file) == sizeof(header), "bad CRC fixture");
 rewind(file);
 CHECK(!read_chunk(file, type, &buffer, &size, &capacity), "bad CRC rejected with reused buffer");
 free(buffer); fclose(file); capture_file = NULL;
 CHECK(remove("chunks.bin") == 0, "remove chunk fixture");
 return 0;
}

static int blocks_test(void)
{
 movie_stream *writer=calloc(1,sizeof(*writer)), *reader=calloc(1,sizeof(*reader));
 uint8 data[65536], output[65536], header[16];uint32 random=123, i, trial;
 CHECK(writer && reader,"block memory");
 for(trial=0;trial<100;trial++){
  uint32 size=trial<4?(trial==0?1:trial==1?127:trial==2?128:65536):trial*659U;
  FILE *f=fopen("block-test.bin","w+b");CHECK(f,"block file");
  for(i=0;i<size;i++){random=random*1664525U+1013904223U;data[i]=trial%3==0?0:trial%3==1?(uint8)i:(uint8)(random>>24);}
  memset(writer,0,sizeof(*writer));memset(reader,0,sizeof(*reader));
  CHECK(movie_write(f,writer,data,size) && movie_flush(f,writer),"compress varied blocks");
  rewind(f);CHECK(movie_read(f,reader,output,size) && !memcmp(data,output,size),"lossless varied block round trip");
  CHECK(movie_eof(f,reader),"block exact EOF");fclose(f);
 }
 /* Chunk headers and payloads may cross arbitrary block boundaries. */
 {FILE *f=fopen("block-test.bin","w+b");CHECK(f,"multiblock file");memset(writer,0,sizeof(*writer));memset(reader,0,sizeof(*reader));
  memset(data,42,sizeof(data));
  CHECK(movie_write(f,writer,data,3) && movie_write(f,writer,data,sizeof(data)) && movie_flush(f,writer),"cross block write");
  rewind(f);CHECK(movie_read(f,reader,output,sizeof(output)) && movie_read(f,reader,output,3) && movie_eof(f,reader),"cross block read");fclose(f);}
 for(trial=0;trial<7;trial++){
  FILE *f=fopen("block-test.bin","w+b");CHECK(f,"bad block file");memset(reader,0,sizeof(*reader));
  memcpy(header,"CMP2",4);put32(header+4,4);put32(header+8,4);put32(header+12,crc(data,4));
  if(trial==0)put32(header+4,65537); /* expansion bound */
  if(trial==1)put32(header+8,65537); /* stored bound */
  if(trial==2)put32(header+4,0);
  if(trial==3)header[12]^=1; /* checksum */
  if(trial==4)header[0]='X';
  if(trial==5)put32(header+8,0);
  fwrite(header,1,16,f);fwrite(data,1,trial==6?3:4,f);rewind(f);
  CHECK(!movie_read(f,reader,output,4),"malformed block rejected");fclose(f);
 }
 reader->size=4;
 reader->packed[0]=128;reader->packed[1]=0;reader->packed[2]=0;
 CHECK(!movie_unpack(reader,3),"zero back reference rejected");
 reader->packed[1]=1;CHECK(!movie_unpack(reader,3),"back reference before block rejected");
 reader->packed[0]=3;CHECK(!movie_unpack(reader,3),"short literal rejected");
 reader->packed[0]=127;CHECK(!movie_unpack(reader,3),"literal expansion overflow rejected");
 /* Deterministic malformed-token exercise; sanitizer builds check every bound. */
 for(trial=0;trial<10000;trial++){
  uint32 n=1+trial%512;reader->size=1+trial%1024;
  for(i=0;i<n;i++){random=random*1664525U+1013904223U;reader->packed[i]=(uint8)(random>>24);}
  (void)movie_unpack(reader,n);
 }
 {FILE *f=fopen("readonly.bin","wb");CHECK(f,"write failure fixture");fclose(f);f=fopen("readonly.bin","rb");CHECK(f,"read only fixture");
  memset(writer,0,sizeof(*writer));CHECK(movie_write(f,writer,data,4),"buffer before flush");
  CHECK(!movie_flush(f,writer),"block flush reports write failure");fclose(f);remove("readonly.bin");}
 printf("Compact transport workspace: %u bytes per active reader/writer.\n",(unsigned)sizeof(*writer));
 CHECK(remove("block-test.bin")==0,"remove block fixture");
 free(writer);free(reader);return 0;
}

static int states_test(void)
{
 savestate_blob_t blob = {0};
 savestate_hdr_t header, bad;
 savestate_spir_meta_t meta, bad_meta;
 size_t meta_offset;
 auint pc;
 cu_avr_reset(); cu_spir_set_size(2); cu_vfat_reset();
 CHECK(savestate_capture_to_mem(&blob) && savestate_validate_blob(&blob), "valid current snapshot");
 memcpy(&header, blob.data, sizeof(header));
 meta_offset = sizeof(header) + header.cpu_size;
 memcpy(&meta, blob.data + meta_offset, sizeof(meta));
 pc = cu_avr_get_state()->pc = 123U;
 bad = header; bad.vfat_size = 0xFFFFFFFFU;
 memcpy(blob.data, &bad, sizeof(bad));
 CHECK(!savestate_restore_from_mem(&blob) && cu_avr_get_state()->pc == pc, "overflowing block sum rejected before CPU mutation");
 memcpy(blob.data, &header, sizeof(header));
 bad_meta = meta; bad_meta.size = MAX_SPI_RAM_SIZE + 1U;
 memcpy(blob.data + meta_offset, &bad_meta, sizeof(bad_meta));
 CHECK(!savestate_restore_from_mem(&blob) && cu_avr_get_state()->pc == pc, "nested SPI size cannot overrun backing allocation");
 CHECK(cu_spir_get_state()->size == meta.size, "invalid state leaves SPI RAM unchanged");
 memcpy(blob.data + meta_offset, &meta, sizeof(meta));
 size_t timer_end=blob.size-(CU_HW_FEATURES?sizeof(savestate_hardware_t):0U);
 blob.data[timer_end - 1U] = 0xFF;
 CHECK(!savestate_validate_blob(&blob), "movie preflight validates timer trailer");
 blob.data[timer_end - 1U] = 0;
 CHECK(savestate_restore_from_mem(&blob), "current state restores");
 blob.size=timer_end-sizeof(savestate_timer1_t);
 CHECK(savestate_restore_from_mem(&blob), "legacy state without timer trailer restores");
 savestate_free_blob(&blob);
 cu_spir_set_size(0);
 CHECK(savestate_capture_to_mem(&blob) && savestate_validate_blob(&blob), "snapshot with disabled SPI RAM is valid");
 CHECK(savestate_restore_from_mem(&blob), "disabled SPI RAM restores");
 savestate_free_blob(&blob);
 return 0;
}

static int state_paths_test(void)
{
 char original[1024],work[64],absolute[1024];struct stat info;FILE* file;
 char const* current="savestates/Path_Test/slot_0.czs";
 char const* legacy="savestates/savestates/Path_Test/slot_0.czs";
 CHECK(STATE_TEST_CWD(original,sizeof(original)),"get original test directory");
 snprintf(work,sizeof(work),"statepaths-%u",(unsigned)STATE_TEST_PID());
 CHECK(SAVESTATE_MKDIR(work)==0&&STATE_TEST_CD(work)==0,"isolated save-path fixture");
 appcfg_defaults(&main_cfg);savestate_init();
 main_copy_str(main_rom_name,sizeof(main_rom_name),"Path Test");
 CHECK(!savestate_slot_exists(0U)&&!savestate_load_slot(0U),"empty slots remain absent");
 CHECK(stat("savestates",&info)!=0&&errno==ENOENT,"slot queries and failed loads do not create directories");
 cu_avr_get_state()->pc=111U;
 CHECK(savestate_save_slot(0U)&&stat(current,&info)==0,"default save path has one savestates level");
 CHECK(stat("savestates/savestates",&info)!=0,"saving does not add a second savestates directory");
 CHECK(savestate_mkdirs("savestates/savestates/Path_Test")&&rename(current,legacy)==0,"legacy-only fixture");
 cu_avr_get_state()->pc=222U;
 CHECK(savestate_slot_exists(0U)&&savestate_load_slot(0U)&&cu_avr_get_state()->pc==111U,"legacy slot remains visible and loadable");
 CHECK(stat(current,&info)!=0,"legacy read does not migrate or create a new file");
 cu_avr_get_state()->pc=123U;
 CHECK(savestate_save_slot(0U)&&stat(current,&info)==0&&stat(legacy,&info)==0,"new writes use corrected path and preserve old save");
 cu_avr_get_state()->pc=222U;
 CHECK(savestate_load_slot(0U)&&cu_avr_get_state()->pc==123U,"corrected slot takes priority over old save");
 file=fopen(current,"wb");CHECK(file,"invalid current slot fixture");
 CHECK(fwrite("bad",1U,3U,file)==3U&&fclose(file)==0,"write invalid current slot");
 cu_avr_get_state()->pc=222U;
 CHECK(!savestate_load_slot(0U)&&cu_avr_get_state()->pc==222U,"invalid current save does not silently load an older save");
 CHECK(remove(current)==0&&savestate_load_slot(0U)&&cu_avr_get_state()->pc==111U,"missing current slot permits legacy fallback");
 CHECK(remove(legacy)==0,"remove owned legacy fixture");
 CHECK(STATE_TEST_CWD(absolute,sizeof(absolute)),"absolute custom root");
 CHECK(strlen(absolute)+sizeof("/custom states/")<=sizeof(main_cfg.save_path),"custom fixture fits configured path");
 main_copy_str(main_cfg.save_path,sizeof(main_cfg.save_path),absolute);
 strcat(main_cfg.save_path,"/custom states/");
 cu_avr_get_state()->pc=321U;
 CHECK(savestate_save_slot(1U)&&stat("custom states/Path_Test/slot_1.czs",&info)==0,"absolute custom path with spaces and trailing slash used directly");
 cu_avr_get_state()->pc=222U;
 CHECK(savestate_load_slot(1U)&&cu_avr_get_state()->pc==321U,"custom-root state restores");
 main_copy_str(main_rom_name,sizeof(main_rom_name),"Unused ROM");
 CHECK(!savestate_slot_exists(2U)&&!savestate_load_slot(2U)&&stat("custom states/Unused_ROM",&info)!=0,"browsing another ROM creates no empty directory");
 CHECK(remove("custom states/Path_Test/slot_1.czs")==0,"remove owned custom fixture");
 CHECK(STATE_TEST_RMDIR("custom states/Path_Test")==0&&STATE_TEST_RMDIR("custom states")==0,"remove empty custom fixture directories");
 CHECK(STATE_TEST_RMDIR("savestates/savestates/Path_Test")==0&&STATE_TEST_RMDIR("savestates/savestates")==0,"remove empty legacy fixture directories");
 CHECK(STATE_TEST_RMDIR("savestates/Path_Test")==0&&STATE_TEST_RMDIR("savestates")==0,"remove empty canonical fixture directories");
 savestate_shutdown();appcfg_defaults(&main_cfg);main_rom_name[0]=0;
 CHECK(STATE_TEST_CD(original)==0&&STATE_TEST_RMDIR(work)==0,"restore test directory and remove owned fixture root");
 puts("Save-state paths: PASS (direct configured root, read-only queries, legacy fallback and canonical priority)");
 return 0;
}

int main(int argc, char **argv)
{
 (void)argv;
 if (crc_test(argc > 1) || chunks_test() || blocks_test() || states_test() || state_paths_test()) return 1;
 puts("Movie CRC/buffer and savestate validation regression passed.");
 return 0;
}
