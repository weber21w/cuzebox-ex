#include "un_round.h"
#include <string.h>

static int member(const UrRound *r, uint8_t seats) {
    return seats && !(seats & (uint8_t)~r->active);
}
void urr_init(UrRound *r) {
    memset(r,0,sizeof *r);
    r->active=r->bound=1; r->epoch=1; r->phase=URR_SETUP;
}
int urr_bound(UrRound *r, uint8_t bound) {
    if(r->phase==URR_FAILED || (bound & 0xf0))return URR_REJECT;
    if((bound & r->active)!=r->active){r->phase=URR_FAILED;return URR_REJECT;}
    r->bound=bound;return URR_ACCEPT;
}
int urr_finish(UrRound *r, uint8_t seats, uint16_t epoch, uint32_t boundary) {
    if(r->phase==URR_FAILED || !seats)return URR_REJECT;
    /* Retry of the operation that closed the preceding round cannot admit
     * another pending seat or advance another epoch, including after loading. */
    if(r->epoch>1 && epoch==r->epoch-1 &&
       !(seats & (uint8_t)~r->previous_active) && boundary==r->boundary)
        return URR_RETRY;
    if(epoch!=r->epoch || r->phase!=URR_PLAY || !member(r,seats) ||
       boundary<r->base || boundary>UINT32_MAX-128u)return URR_REJECT;
    if(r->finished && boundary!=r->boundary){r->phase=URR_FAILED;return URR_REJECT;}
    if((r->finished & seats)==seats)return URR_RETRY;
    r->boundary=boundary;r->finished|=seats;
    if(r->finished!=r->active)return URR_ACCEPT;
    if(r->epoch==UINT16_MAX){r->phase=URR_FAILED;return URR_REJECT;}
    r->previous_active=r->active;r->active=r->bound;r->epoch++;
    r->base=boundary+64u;r->finished=r->ready=r->descriptor_valid=0;
    memset(r->descriptor,0,sizeof r->descriptor);
    r->phase=URR_SETUP;return URR_TRANSITION;
}
int urr_describe(UrRound *r, uint8_t seats, uint16_t epoch,
                 const uint8_t descriptor[URR_DESCRIPTOR_SIZE]) {
    if(r->phase!=URR_SETUP || epoch!=r->epoch || !member(r,seats) || !(seats&1))
        return URR_REJECT;
    if(r->descriptor_valid)
        return memcmp(r->descriptor,descriptor,URR_DESCRIPTOR_SIZE)?URR_REJECT:URR_RETRY;
    memcpy(r->descriptor,descriptor,URR_DESCRIPTOR_SIZE);
    r->descriptor_valid=1;return URR_ACCEPT;
}
int urr_loaded(UrRound *r, uint8_t seats, uint16_t epoch,
               const uint8_t descriptor[URR_DESCRIPTOR_SIZE]) {
    if(r->phase==URR_FAILED || epoch!=r->epoch || !member(r,seats) ||
       !r->descriptor_valid || memcmp(r->descriptor,descriptor,URR_DESCRIPTOR_SIZE))
        return URR_REJECT;
    if((r->ready&seats)==seats)return URR_RETRY;
    if(r->phase!=URR_SETUP)return URR_REJECT;
    r->ready|=seats;
    if(r->ready!=r->active)return URR_ACCEPT;
    r->phase=URR_PLAY;return URR_TRANSITION;
}
