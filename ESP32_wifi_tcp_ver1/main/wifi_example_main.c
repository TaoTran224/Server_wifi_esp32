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

// --- CẤU HÌNH UART ---
#define UART_PORT_NUM       UART_NUM_0
#define UART_BUF_SIZE       1024

// --- CẤU HÌNH THỜI GIAN & TCP SERVER MẶC ĐỊNH ---
#define DEFAULT_SERVER_IP       "192.168.1.7"         // Địa chỉ IP mặc định
#define DEFAULT_SERVER_PORT     502                   // Cổng TCP Server mặc định (Modbus TCP)
#define DEFAULT_HEARTBEAT_SEC   30                     // Chu kỳ gửi mặc định: 5 giây

// --- GIÁ TRỊ WI-FI MẶC ĐỊNH ---
#define DEFAULT_WIFI_SSID       "KEM GAO_5G"
#define DEFAULT_WIFI_PASS       "23090609"

static const char *TAG = "ESP32_SUPER_LOOP";

static bool s_wifi_connected = false;
static int s_retry_num = 0;
static int s_sock = -1;

// Biến lưu trữ cấu hình đang hoạt động
static char s_wifi_ssid[33] = DEFAULT_WIFI_SSID;
static char s_wifi_pass[65] = DEFAULT_WIFI_PASS;
static char s_server_ip[16] = DEFAULT_SERVER_IP;
static uint16_t s_server_port = DEFAULT_SERVER_PORT;
static uint32_t s_heartbeat_interval_sec = DEFAULT_HEARTBEAT_SEC; // Chu kỳ Heartbeat tính bằng giây

// Frame dữ liệu Modbus TCP / Hex gửi tới Server (12 bytes)
static const uint8_t payload[] = {
    0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x06, 0x00, 0x01, 0x00, 0x64
};

// ============================================================================
// 1. QUẢN LÝ NVS FLASH (WI-FI, TCP SERVER & HEARTBEAT)
// ============================================================================

static esp_err_t save_wifi_credentials_to_nvs(const char *ssid, const char *pass)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(my_handle, "wifi_ssid", ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(my_handle, "wifi_pass", pass);
    }
    if (err == ESP_OK) {
        err = nvs_commit(my_handle);
    }
    nvs_close(my_handle);
    return err;
}

static esp_err_t load_wifi_credentials_from_nvs(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_get_str(my_handle, "wifi_ssid", ssid, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(my_handle, "wifi_pass", pass, &pass_len);
    }
    nvs_close(my_handle);
    return err;
}

static esp_err_t save_tcp_config_to_nvs(const char *ip, uint16_t port)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(my_handle, "tcp_ip", ip);
    if (err == ESP_OK) {
        err = nvs_set_u16(my_handle, "tcp_port", port);
    }
    if (err == ESP_OK) {
        err = nvs_commit(my_handle);
    }
    nvs_close(my_handle);
    return err;
}

static esp_err_t load_tcp_config_from_nvs(char *ip, size_t ip_len, uint16_t *port)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_get_str(my_handle, "tcp_ip", ip, &ip_len);
    if (err == ESP_OK) {
        err = nvs_get_u16(my_handle, "tcp_port", port);
    }
    nvs_close(my_handle);
    return err;
}

static esp_err_t save_heartbeat_to_nvs(uint32_t interval_sec)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_set_u32(my_handle, "heartbeat_sec", interval_sec);
    if (err == ESP_OK) {
        err = nvs_commit(my_handle);
    }
    nvs_close(my_handle);
    return err;
}

static esp_err_t load_heartbeat_from_nvs(uint32_t *interval_sec)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_get_u32(my_handle, "heartbeat_sec", interval_sec);
    nvs_close(my_handle);
    return err;
}

// ============================================================================
// 2. CẤU HÌNH VÀ BẮT SỰ KIỆN WI-FI
// ============================================================================

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        if (s_sock >= 0) {
            close(s_sock);
            s_sock = -1;
        }
        ESP_LOGW(TAG, "Mat ket noi Wi-Fi '%s'! Dang thu ket noi lai... (Lan thu: %d)", s_wifi_ssid, ++s_retry_num);
        esp_wifi_connect();
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        s_wifi_connected = true;
        s_retry_num = 0;
        
        ESP_LOGI(TAG, "====================================");
        ESP_LOGI(TAG, "Ket noi Wi-Fi THANH CONG!");
        ESP_LOGI(TAG, "SSID: %s", s_wifi_ssid);
        ESP_LOGI(TAG, "IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "====================================");
    }
}

static void wifi_init_sta(const char *ssid, const char *pass)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));

    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    //esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "Khoi tao Wi-Fi voi SSID: '%s'", ssid);
}

static void apply_new_wifi_config(const char *ssid, const char *pass)
{
    s_wifi_connected = false;
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }

    esp_wifi_disconnect();

    memset(s_wifi_ssid, 0, sizeof(s_wifi_ssid));
    memset(s_wifi_pass, 0, sizeof(s_wifi_pass));
    strncpy(s_wifi_ssid, ssid, sizeof(s_wifi_ssid) - 1);
    strncpy(s_wifi_pass, pass, sizeof(s_wifi_pass) - 1);

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));

    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    s_retry_num = 0;
    esp_wifi_connect();

    ESP_LOGI(TAG, "Da ap dung cau hinh va dang ket noi Wi-Fi: SSID='%s'", ssid);
}

// ============================================================================
// 3. XỬ LÝ LỆNH AT QUA UART
// ============================================================================

static void uart_init_config(void)
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

static void uart_send_response(const char *msg)
{
    uart_write_bytes(UART_PORT_NUM, msg, strlen(msg));
}

static void parse_at_wifi_command(char *cmd)
{
    if (strcasecmp(cmd, "at+infowifi=?") == 0) {
        char resp[256];
        snprintf(resp, sizeof(resp), "+INFOWIFI:(%s)(%s),STATUS:%s\r\nOK\r\n", 
                 s_wifi_ssid, s_wifi_pass, s_wifi_connected ? "CONNECTED" : "DISCONNECTED");
        uart_send_response(resp);
        ESP_LOGI(TAG, "Truy van Wi-Fi: SSID=%s, Status=%s", s_wifi_ssid, s_wifi_connected ? "CONNECTED" : "DISCONNECTED");
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

        if (save_wifi_credentials_to_nvs(new_ssid, new_pass) == ESP_OK) {
            uart_send_response("OK\r\n");
            ESP_LOGI(TAG, "Luu Wi-Fi vao NVS thanh cong.");
        } else {
            uart_send_response("ERROR_NVS\r\n");
            ESP_LOGE(TAG, "Loi luu Wi-Fi vao NVS!");
        }

        apply_new_wifi_config(new_ssid, new_pass);
        return;
    }

format_error:
    uart_send_response("ERROR_FORMAT\r\n");
    ESP_LOGE(TAG, "Sai dinh dang! Cu phap: at+infowifi=(SSID)(PASS)");
}

static void parse_at_tcp_command(char *cmd)
{
    if (strcasecmp(cmd, "at+tcp=?") == 0) {
        char resp[256];
        snprintf(resp, sizeof(resp), "+TCP:(%s)(%d)\r\nOK\r\n", s_server_ip, s_server_port);
        uart_send_response(resp);
        ESP_LOGI(TAG, "Truy van thong tin TCP Server: IP=%s, Port=%d", s_server_ip, s_server_port);
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

        uint16_t new_port = (uint16_t)port_val;

        if (s_sock >= 0) {
            close(s_sock);
            s_sock = -1;
        }

        memset(s_server_ip, 0, sizeof(s_server_ip));
        strncpy(s_server_ip, new_ip, sizeof(s_server_ip) - 1);
        s_server_port = new_port;

        if (save_tcp_config_to_nvs(s_server_ip, s_server_port) == ESP_OK) {
            uart_send_response("OK\r\n");
            ESP_LOGI(TAG, "Da cai dat & luu TCP Server vao NVS Flash.");
        } else {
            uart_send_response("ERROR_NVS\r\n");
            ESP_LOGE(TAG, "Loi khi luu TCP Server vao NVS Flash!");
        }
        return;
    }

format_error:
    uart_send_response("ERROR_FORMAT\r\n");
    ESP_LOGE(TAG, "Sai dinh dang! Cu phap dung: at+tcp=(IP)(PORT) hoac at+tcp=?");
}

static void parse_at_heartbeat_command(char *cmd)
{
    // 1. Lệnh truy vấn: at+heartbeat=?
    if (strcasecmp(cmd, "at+heartbeat=?") == 0) {
        char resp[128];
        snprintf(resp, sizeof(resp), "+HEARTBEAT:(%lu)\r\nOK\r\n", s_heartbeat_interval_sec);
        uart_send_response(resp);
        ESP_LOGI(TAG, "Truy van Heartbeat: %lu giay", s_heartbeat_interval_sec);
        return;
    }

    // 2. Lệnh cài đặt: at+heartbeat=(second)
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

        s_heartbeat_interval_sec = (uint32_t)val;

        if (save_heartbeat_to_nvs(s_heartbeat_interval_sec) == ESP_OK) {
            uart_send_response("OK\r\n");
            ESP_LOGI(TAG, "Da cai dat & luu Heartbeat vao NVS Flash: %lu giay", s_heartbeat_interval_sec);
        } else {
            uart_send_response("ERROR_NVS\r\n");
            ESP_LOGE(TAG, "Loi khi luu Heartbeat vao NVS Flash!");
        }
        return;
    }

format_error:
    uart_send_response("ERROR_FORMAT\r\n");
    ESP_LOGE(TAG, "Sai dinh dang! Cu phap dung: at+heartbeat=(second) hoac at+heartbeat=?");
}

static void check_uart_commands(void)
{
    uint8_t data[UART_BUF_SIZE];
    int length = 0;
    
    ESP_ERROR_CHECK(uart_get_buffered_data_len(UART_PORT_NUM, (size_t*)&length));
    if (length > 0) {
        int len = uart_read_bytes(UART_PORT_NUM, data, length, 0);
        if (len > 0) {
            data[len] = '\0';
            
            // Xóa ký tự xuống dòng \r \n
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

// ============================================================================
// 4. KẾT NỐI, GỬI VÀ NHẬN DỮ LIỆU TCP
// ============================================================================

static bool connect_to_server(void)
{
    if (s_sock >= 0) {
        return true;
    }

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(s_server_ip);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(s_server_port);

    s_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "Khong the tao Socket: errno %d", errno);
        return false;
    }

    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    ESP_LOGI(TAG, "Dang ket noi toi TCP Server %s:%d...", s_server_ip, s_server_port);
    int err = connect(s_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Ket noi Server that bai: errno %d", errno);
        close(s_sock);
        s_sock = -1;
        return false;
    }

    ESP_LOGI(TAG, "Ket noi TCP Server %s:%d thanh cong!", s_server_ip, s_server_port);
    return true;
}

static void send_payload_and_read_response(void)
{
    ESP_LOGI(TAG, "Chu ky gui du lieu (%lu s) - Wi-Fi dang su dung: '%s'", s_heartbeat_interval_sec, s_wifi_ssid);

    if (!connect_to_server()) {
        return;
    }

    size_t payload_len = sizeof(payload);
    int err = send(s_sock, payload, payload_len, 0);
    if (err < 0) {
        ESP_LOGE(TAG, "Loi khi gui du lieu: errno %d", errno);
        close(s_sock);
        s_sock = -1;
        return;
    }

    ESP_LOGI(TAG, "Da gui %d bytes payload toi Server (qua Wi-Fi '%s').", err, s_wifi_ssid);

    uint8_t rx_buffer[128];
    int len = recv(s_sock, rx_buffer, sizeof(rx_buffer) - 1, 0);

    if (len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ESP_LOGW(TAG, "Het thoi gian cho (Timeout 2s), Server khong phan hoi.");
        } else {
            ESP_LOGE(TAG, "Loi khi nhan du lieu tu Server: errno %d", errno);
            close(s_sock);
            s_sock = -1;
        }
    } else if (len == 0) {
        ESP_LOGW(TAG, "Server da chu dong dong ket noi.");
        close(s_sock);
        s_sock = -1;
    } else {
        char hex_str[128 * 3 + 1] = {0};
        for (int i = 0; i < len; i++) {
            sprintf(hex_str + strlen(hex_str), "%02X ", rx_buffer[i]);
        }

        ESP_LOGI(TAG, "Rec from server %s:%d, Nhan %d bytes ", s_server_ip, s_server_port, len);
        ESP_LOGI(TAG, "HEX : [ %s]", hex_str);
    }
}

// ============================================================================
// 5. HÀM CHÍNH (APP_MAIN) & SUPER-LOOP
// ============================================================================

void app_main(void)
{
    // A. Khởi tạo NVS Flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // B. Khởi tạo UART Driver
    uart_init_config();

    // C. Đọc SSID / Pass từ NVS Flash
    if (load_wifi_credentials_from_nvs(s_wifi_ssid, sizeof(s_wifi_ssid), s_wifi_pass, sizeof(s_wifi_pass)) == ESP_OK) {
        ESP_LOGI(TAG, "Da tai thong tin Wi-Fi tu NVS Flash: '%s'", s_wifi_ssid);
    } else {
        ESP_LOGW(TAG, "Chua co thong tin Wi-Fi trong NVS. Su dung mac dinh: '%s'", s_wifi_ssid);
    }

    // D. Đọc IP / Port Server từ NVS Flash
    if (load_tcp_config_from_nvs(s_server_ip, sizeof(s_server_ip), &s_server_port) == ESP_OK) {
        ESP_LOGI(TAG, "Da tai thong tin TCP Server tu NVS: %s:%d", s_server_ip, s_server_port);
    } else {
        ESP_LOGW(TAG, "Chua co thong tin TCP Server trong NVS. Su dung mac dinh: %s:%d", s_server_ip, s_server_port);
    }

    // E. Đọc chu kỳ Heartbeat từ NVS Flash
    if (load_heartbeat_from_nvs(&s_heartbeat_interval_sec) == ESP_OK) {
        ESP_LOGI(TAG, "Da tai chu ky Heartbeat tu NVS: %lu giay", s_heartbeat_interval_sec);
    } else {
        ESP_LOGW(TAG, "Chua co Heartbeat trong NVS. Su dung mac dinh: %lu giay", s_heartbeat_interval_sec);
    }

    // F. Khởi tạo Wi-Fi STA
    wifi_init_sta(s_wifi_ssid, s_wifi_pass);

    int64_t last_send_time = esp_timer_get_time();

    // G. Super-Loop
    while (1) {
        // 1. Kiểm tra lệnh AT gửi tới từ UART
        check_uart_commands();

        // 2. Kiểm tra chu kỳ định kỳ dựa trên s_heartbeat_interval_sec
        int64_t now = esp_timer_get_time();
        int64_t interval_us = (int64_t)s_heartbeat_interval_sec * 1000000LL;

        if (now - last_send_time >= interval_us) {
            last_send_time = now;

            if (s_wifi_connected) {
                send_payload_and_read_response();
            } else {
                ESP_LOGW(TAG, "Wi-Fi (%s) not ready!", s_wifi_ssid);
            }
        }

        // Nhường CPU ngắn để tránh kích hoạt Task Watchdog
        vTaskDelay(pdMS_TO_TICKS(11));
    }
}