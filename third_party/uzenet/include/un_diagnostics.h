#ifndef UN_DIAGNOSTICS_H
#define UN_DIAGNOSTICS_H
#include <stdint.h>
#define UN_DIAG_PAGES 4
#define UN_DIAG_BYTES 24
enum { UN_RX_CRC=1, UN_RX_MALFORMED };
/* Optional codec observer. Default AVR/browser builds contain no calls. */
#ifdef UN_WIRE_DIAGNOSTICS
void un_diag_rx_fault(uint8_t reason);
#endif
typedef struct {
    uint32_t crc,malformed,overflow,framing,parity,buffer_full;
    uint32_t pause_at,pause_max,pause_total,service_at,service_gap,service_work,services;
    uint8_t paused,service_valid;
} UnDiagnostics;
static inline void un_diag_pause(UnDiagnostics *d,uint32_t now,uint8_t paused){
    if(d->paused){uint32_t elapsed=now-d->pause_at;
        if(elapsed>d->pause_max)d->pause_max=elapsed;
        if(!paused)d->pause_total+=elapsed;
    }else if(paused)d->pause_at=now;
    d->paused=paused;
}
static inline void un_diag_service(UnDiagnostics *d,uint32_t now){
    if(d->service_valid){uint32_t gap=now-d->service_at;if(gap>d->service_gap)d->service_gap=gap;}
    d->service_valid=1;d->service_at=now;d->services++;
}
/* A small reproducible decision generator; test-only callers own packet storage. */
typedef struct {uint32_t seed,seen,affected;uint16_t every,delay;uint8_t mask;} UnFault;
static inline uint8_t un_fault_next(UnFault *f){
    if(!f->every || !f->mask || ++f->seen%f->every)return 0;
    f->seed=f->seed*1664525UL+1013904223UL;
    unsigned pick=(f->seed>>24)%__builtin_popcount(f->mask);uint8_t bit=1;
    while(!(f->mask&bit) || pick--){bit<<=1;}
    f->affected++;return bit;
}
#endif
