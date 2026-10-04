#include "app.h"
#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "CONFIG_NVS";
static app_config_t s_app_config;

app_config_t *app_config_get_ptr(void)
{
    return &s_app_config;
}

esp_err_t app_config_init(void)
{
    memset(&s_app_config, 0, sizeof(s_app_config));
    snprintf(s_app_config.wifi_ssid, sizeof(s_app_config.wifi_ssid), "My_WiFi");
    snprintf(s_app_config.wifi_pass, sizeof(s_app_config.wifi_pass), "12345678");
    snprintf(s_app_config.server_ip, sizeof(s_app_config.server_ip), "192.168.1.100");
    s_app_config.server_port = 8080;
    s_app_config.heartbeat_interval_sec = 60;
    return ESP_OK;
}

esp_err_t app_config_save_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // 1. Mở NVS Namespace "storage" với quyền ReadWrite
    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Lỗi mở NVS Handle: %s", esp_err_to_name(err));
        return err;
    }

    // 2. Ghi toàn bộ struct s_app_config vào NVS dạng BLOB
    err = nvs_set_blob(nvs_handle, "app_cfg", &s_app_config, sizeof(app_config_t));
    if (err == ESP_OK) {
        // 3. Commit dữ liệu xuống Flash
        err = nvs_commit(nvs_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Lưu cấu hình vào NVS Flash thành công!");
        } else {
            ESP_LOGE(TAG, "Lỗi NVS Commit: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "Lỗi nvs_set_blob: %s", esp_err_to_name(err));
    }

    // 4. Đóng Handle
    nvs_close(nvs_handle);
    return err;
}
