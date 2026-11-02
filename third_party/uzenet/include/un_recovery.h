#ifndef UN_RECOVERY_H
#define UN_RECOVERY_H
#include <stdint.h>
#include <string.h>
/* Explicit match-option boundary. Legacy RCF1/timed and other games never
 * enter this policy. All authority travels through the authenticated Room. */
#define UR_RECOVERY_REQUEST 15
#define UR_RECOVERY_STATUS 0x8d
#define UR_RECOVERY_DETECT_MS 2000u
#define UR_RECOVERY_GRACE_MS 15000u
#define UR_RECOVERY_COUNTDOWN_MS 3000u
static inline int un_recovery_enabled(const uint8_t *c){
    return !memcmp(c,"RCF1SNP2",8) && c[12]=='R' && c[13]=='C' && c[14]==1 && !c[15];
}
static inline unsigned un_recovery_owner(uint8_t active,uint8_t host,unsigned car){
    return (active&(1u<<car))?1u<<car:host<4?1u<<host:0;
}
#endif
