/* Verify host LAN permission without opening real network sockets. */
#include "cu_esp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

static cu_state_esp_t state;
static unsigned opened, closed;
cu_state_esp_t* cu_esp_get_state(void){ return &state; }
int __wrap_socket(int domain, int type, int protocol){
    (void)domain; (void)type; (void)protocol; return 100 + (int)opened++;
}
int __wrap_bind(int fd, const struct sockaddr *addr, socklen_t len){
    (void)fd; (void)addr; (void)len; return 0;
}
int __wrap_setsockopt(int fd, int level, int opt, const void *value, socklen_t len){
    (void)fd; (void)level; (void)opt; (void)value; (void)len; return 0;
}
int __wrap_getsockname(int fd, struct sockaddr *addr, socklen_t *len){
    (void)fd; (void)len; ((struct sockaddr_in*)addr)->sin_port = htons(12345); return 0;
}
int __wrap_fcntl(int fd, int cmd, ...){ (void)fd; (void)cmd; return 0; }
int __wrap_close(int fd){ (void)fd; closed++; return 0; }

int main(void){
    cu_esp_reset_factory();
    assert(!cu_esp_get_softap_enabled());
    state.wifi_mode = ESP_WIFI_MODE_STATION;
    cu_esp_lan_init(&state);
    assert(opened == 0); /* Startup must never transiently bind. */
    for(unsigned mode=1; mode<=3; mode++){
        state.wifi_mode = mode;
        cu_esp_lan_on_cwmode_change(&state);
        assert(opened == 0 && state.lan.adv_enable == 0);
    }
    strcpy((char*)state.soft_ap_name, "Fixture");
    state.wifi_mode = ESP_WIFI_MODE_STATION;
    cu_esp_set_softap_enabled(TRUE);
    assert(opened == 2 && closed == 0); /* Hinted/manual enable supports clients. */
    cu_esp_set_softap_enabled(FALSE);
    assert(closed == 2 && state.lan.ucast_port == 0);
    cu_esp_lan_shutdown(&state);
    cu_esp_reset_factory(); /* ESP reset must retain host disable. */
    assert(!cu_esp_get_softap_enabled());
    cu_esp_lan_init(&state);
    assert(opened == 2);
    cu_esp_set_softap_enabled(TRUE);
    cu_esp_lan_shutdown(&state);
    cu_esp_reset_factory(); /* ESP reset must also retain host enable. */
    assert(cu_esp_get_softap_enabled());
    state.wifi_mode = ESP_WIFI_MODE_SOFTAP;
    strcpy((char*)state.soft_ap_name, "Fixture");
    cu_esp_lan_init(&state);
    assert(state.lan.adv_enable && opened == 6);
    cu_esp_set_softap_enabled(FALSE);
    assert(!state.lan.adv_enable && closed == opened);
    puts("ESP LAN policy: disabled startup, station/AP gating, live close and reset persistence OK");
    return 0;
}
