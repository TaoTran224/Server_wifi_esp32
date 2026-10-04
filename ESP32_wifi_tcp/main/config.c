#include "config.h"

// Biến cấu hình toàn cục nội bộ trong file config.c
static app_config_t s_config = {
    .wifi_ssid = "My_WiFi",
    .wifi_pass = "12345678",
    .server_ip = "192.168.1.100",
    .server_port = 8080,
    .heartbeat_interval_sec = 60
};

app_config_t* app_config_get_ptr(void) {
    return &s_config;

}

