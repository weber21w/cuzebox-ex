#ifndef UN_SCORES_H
#define UN_SCORES_H
#include <stdint.h>
#define USCORE_VERSION 1
#define USCORE_ROWS 8
#define USCORE_NAME 13
#define USCORE_JOBS 4
#define USCORE_CONTROL 0x78
#define USCORE_REQUEST_BYTES 19
#define USCORE_OPEN_PART 0x40 /* [40|offset][up to 3 ROM bytes], offsets 0,3,6 */
#define USCORE_SUBMIT24 0x80 /* [80|board 0..127][score LE24] */
enum { USCORE_OPEN=1,USCORE_SUBMIT,USCORE_TOP,USCORE_READ,USCORE_STATUS };
enum { USCORE_OK,USCORE_DUPLICATE,USCORE_DISABLED,USCORE_INVALID,USCORE_ERROR,
       USCORE_BUSY,USCORE_PENDING,USCORE_EMPTY,USCORE_UNSUPPORTED,USCORE_TIMEOUT };
typedef struct {
    uint32_t id,value,born;
    uint16_t rank;
    uint8_t rom[8],board,kind,state;
} UnScoreJob;
typedef struct {uint32_t score;uint8_t rank,length,name[USCORE_NAME];} UnScoreRow;
typedef struct {
    UnScoreJob job[USCORE_JOBS];
    UnScoreRow row[USCORE_ROWS];
    uint32_t serial,active,sent,snapshot;
    uint8_t rom[8],staging[8],staged,bound,state,count;
} UnScores;
/* No sockets, allocation or credentials. IDs never wrap; OPEN changes only
 * future jobs. Accepted submissions keep their ROM across game/context changes. */
uint8_t uscores_command(UnScores *s,const uint8_t *p,uint8_t n,uint32_t now,uint8_t out[22]);
void uscores_tick(UnScores *s,uint32_t now);
int uscores_next(UnScores *s,uint32_t now,uint8_t out[USCORE_REQUEST_BYTES]);
int uscores_receive(UnScores *s,const uint8_t *p,unsigned n);
void uscores_disconnect(UnScores *s);
#endif
