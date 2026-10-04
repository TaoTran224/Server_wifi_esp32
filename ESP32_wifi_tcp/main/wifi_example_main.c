#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "uart_handle.h"

static const char *TAG = "ESP32_SUPER_LOOP";


void app_main(void)
{

    // G. Super-Loop
    while (1)
     {
        // Nhường CPU ngắn để tránh kích hoạt Task Watchdog
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}