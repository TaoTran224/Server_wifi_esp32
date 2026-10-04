#include "uart_handle.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "config.h"

static const char *TAG = "UART_HANDLE";
static uart_event_callback_t s_event_cb = NULL;

void uart_handle_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
}

void uart_handle_register_callback(uart_event_callback_t cb)
{
    s_event_cb = cb;
}

void uart_send_response(const char *data)
{
    if (data != NULL) {
        uart_write_bytes(UART_PORT_NUM, data, strlen(data));
    }
}

static void parse_at_wifi_command(char *cmd)
{
    app_config_t *cfg = app_config_get_ptr();

    if (strcasecmp(cmd, "at+infowifi=?") == 0) {
        char resp[256];
        snprintf(resp, sizeof(resp), "+INFOWIFI:(%s)(%s),STATUS:%s\r\nOK\r\n", 
                 cfg->wifi_ssid, cfg->wifi_pass, "DISCONNECTED");
        uart_send_response(resp);
        ESP_LOGI(TAG, "Truy van Wi-Fi: SSID=%s", cfg->wifi_ssid);
        return;
    }

    char *start_ssid = strchr(cmd, '(');
    if (!start_ssid) goto format_error;

    char *end_ssid = strchr(start_ssid, ')');
    if (!end_ssid) goto format_error;

    char *start_pass = strchr(end_ssid + 1, '(');
    if (!start_pass) goto format_error;

    char *end_pass = strchr(start_pass, ')');
    if (!end_pass) goto format_error;

    char new_ssid[33] = {0};
    char new_pass[65] = {0};

    int ssid_len = end_ssid - (start_ssid + 1);
    int pass_len = end_pass - (start_pass + 1);

    if (ssid_len > 0 && ssid_len < sizeof(new_ssid) && pass_len >= 0 && pass_len < sizeof(new_pass)) {
        strncpy(new_ssid, start_ssid + 1, ssid_len);
        strncpy(new_pass, start_pass + 1, pass_len);

        // Cập nhật giá trị vào struct cấu hình hệ thống
        strncpy(cfg->wifi_ssid, new_ssid, sizeof(cfg->wifi_ssid) - 1);
        strncpy(cfg->wifi_pass, new_pass, sizeof(cfg->wifi_pass) - 1);

        // Lưu NVS (Hàm luu NVS gọi qua config module)
        if (app_config_save_nvs() == ESP_OK) {
            uart_send_response("OK\r\n");
            ESP_LOGI(TAG, "Luu Wi-Fi vao NVS thanh cong.");
        } else {
            uart_send_response("ERROR_NVS\r\n");
            ESP_LOGE(TAG, "Loi luu Wi-Fi vao NVS!");
        }

        // Bắn Callback thông báo thay đổi Wi-Fi nếu cần
        if (s_event_cb != NULL) {
            uart_cmd_event_t evt = {
                .cmd_type = UART_CMD_UPDATE_CONFIG,
                .payload = "WIFI_CHANGED",
                .length = 12,
                .timeout_ms = 0
            };
            s_event_cb(&evt);
        }
        return;
    }

format_error:
    uart_send_response("ERROR_FORMAT\r\n");
    ESP_LOGE(TAG, "Sai dinh dang! Cu phap: at+infowifi=(SSID)(PASS)");
}

static void parse_at_tcp_command(char *cmd)
{
    app_config_t *cfg = app_config_get_ptr();

    if (strcasecmp(cmd, "at+tcp=?") == 0) {
        char resp[256];
        snprintf(resp, sizeof(resp), "+TCP:(%s)(%d)\r\nOK\r\n", cfg->server_ip, cfg->server_port);
        uart_send_response(resp);
        ESP_LOGI(TAG, "Truy van thong tin TCP Server: IP=%s, Port=%d", cfg->server_ip, cfg->server_port);
        return;
    }

    char *start_ip = strchr(cmd, '(');
    if (!start_ip) goto format_error;

    char *end_ip = strchr(start_ip, ')');
    if (!end_ip) goto format_error;

    char *start_port = strchr(end_ip + 1, '(');
    if (!start_port) goto format_error;

    char *end_port = strchr(start_port, ')');
    if (!end_port) goto format_error;

    char new_ip[16] = {0};
    char port_str[10] = {0};

    int ip_len = end_ip - (start_ip + 1);
    int port_len = end_port - (start_port + 1);

    if (ip_len > 0 && ip_len < sizeof(new_ip) && port_len > 0 && port_len < sizeof(port_str)) {
        strncpy(new_ip, start_ip + 1, ip_len);
        strncpy(port_str, start_port + 1, port_len);

        int port_val = atoi(port_str);
        if (port_val <= 0 || port_val > 65535) {
            uart_send_response("ERROR_PORT\r\n");
            ESP_LOGE(TAG, "Port khong hop le (1 - 65535)!");
            return;
        }

        // Cập nhật cấu hình chung
        memset(cfg->server_ip, 0, sizeof(cfg->server_ip));
        strncpy(cfg->server_ip, new_ip, sizeof(cfg->server_ip) - 1);
        cfg->server_port = (uint16_t)port_val;

        if (app_config_save_nvs() == ESP_OK) {
            uart_send_response("OK\r\n");
            ESP_LOGI(TAG, "Da cai dat & luu TCP Server vao NVS Flash.");
        } else {
            uart_send_response("ERROR_NVS\r\n");
            ESP_LOGE(TAG, "Loi khi luu TCP Server vao NVS Flash!");
        }

        // Yêu cầu TCP Reconnect qua Callback
        if (s_event_cb != NULL) {
            uart_cmd_event_t evt = {
                .cmd_type = UART_CMD_RECONNECT_TCP,
                .payload = NULL,
                .length = 0,
                .timeout_ms = 1000
            };
            s_event_cb(&evt);
        }
        return;
    }

format_error:
    uart_send_response("ERROR_FORMAT\r\n");
    ESP_LOGE(TAG, "Sai dinh dang! Cu phap dung: at+tcp=(IP)(PORT) hoac at+tcp=?");
}

static void parse_at_heartbeat_command(char *cmd)
{
    app_config_t *cfg = app_config_get_ptr();

    if (strcasecmp(cmd, "at+heartbeat=?") == 0) {
        char resp[128];
        snprintf(resp, sizeof(resp), "+HEARTBEAT:(%lu)\r\nOK\r\n", cfg->heartbeat_interval_sec);
        uart_send_response(resp);
        ESP_LOGI(TAG, "Truy van Heartbeat: %lu giay", cfg->heartbeat_interval_sec);
        return;
    }

    char *start_val = strchr(cmd, '(');
    if (!start_val) goto format_error;

    char *end_val = strchr(start_val, ')');
    if (!end_val) goto format_error;

    char val_str[16] = {0};
    int val_len = end_val - (start_val + 1);

    if (val_len > 0 && val_len < sizeof(val_str)) {
        strncpy(val_str, start_val + 1, val_len);
        
        long val = atol(val_str);
        if (val <= 0) {
            uart_send_response("ERROR_VALUE\r\n");
            ESP_LOGE(TAG, "Gia tri Heartbeat phai lon hon 0!");
            return;
        }

        cfg->heartbeat_interval_sec = (uint32_t)val;

        if (app_config_save_nvs() == ESP_OK) {
            uart_send_response("OK\r\n");
            ESP_LOGI(TAG, "Da cai dat & luu Heartbeat vao NVS: %lu giay", cfg->heartbeat_interval_sec);
        } else {
            uart_send_response("ERROR_NVS\r\n");
            ESP_LOGE(TAG, "Loi khi luu Heartbeat vao NVS!");
        }
        return;
    }

format_error:
    uart_send_response("ERROR_FORMAT\r\n");
    ESP_LOGE(TAG, "Sai dinh dang! Cu phap dung: at+heartbeat=(second) hoac at+heartbeat=?");
}

void check_uart_commands(void)
{
    size_t length = 0;
    
    if (uart_get_buffered_data_len(UART_PORT_NUM, &length) == ESP_OK && length > 0) {
        uint8_t data[UART_BUF_SIZE];
        int len = uart_read_bytes(UART_PORT_NUM, data, sizeof(data) - 1, pdMS_TO_TICKS(10));
        if (len > 0) {
            data[len] = '\0';
            
            char *p = (char *)data;
            while (*p) {
                if (*p == '\r' || *p == '\n') {
                    *p = '\0';
                    break;
                }
                p++;
            }

            if (strncasecmp((char *)data, "at+infowifi", 11) == 0) {
                parse_at_wifi_command((char *)data);
            } 
            else if (strncasecmp((char *)data, "at+tcp", 6) == 0) {
                parse_at_tcp_command((char *)data);
            }
            else if (strncasecmp((char *)data, "at+heartbeat", 12) == 0) {
                parse_at_heartbeat_command((char *)data);
            }
        }
    }
}
