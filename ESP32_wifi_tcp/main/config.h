#ifndef CONFIG_H
#define CONFIG_H
#include "app.h"
#include <stdint.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_err.h"


// Khởi tạo và đọc cấu hình từ NVS Flash
esp_err_t app_config_init(void);

// Lưu toàn bộ struct app_config_t hiện tại vào NVS Flash
esp_err_t app_config_save_nvs(void);

#endif // CONFIG_H