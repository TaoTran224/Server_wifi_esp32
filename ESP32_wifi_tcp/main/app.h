#ifndef APP_H
#define APP_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// 1. STRUCT CẤU HÌNH HỆ THỐNG (Lưu trữ NVS & Dùng chung toàn hệ thống)
// ============================================================================
typedef struct {
    char wifi_ssid[32];             // SSID Wi-Fi
    char wifi_pass[64];             // Mật khẩu Wi-Fi
    char server_ip[40];             // IP hoặc Domain của TCP Server
    uint16_t server_port;           // Cổng TCP Server (ví dụ: 8080)
    uint32_t heartbeat_interval_sec;// Chu kỳ gửi heartbeat (giây)
} app_config_t;

app_config_t *app_config_get_ptr(void);


// ============================================================================
// 2. STRUCT ĐÓNG GÓI LỆNH / SỰ KIỆN TỪ UART SANG TCP (Dùng cho Callback)
// ============================================================================
typedef enum {
    UART_CMD_SEND_DATA = 0,         // Lệnh gửi dữ liệu thô sang Server
    UART_CMD_UPDATE_CONFIG,         // Lệnh cập nhật cấu hình (Heartbeat/IP...)
    UART_CMD_RECONNECT_TCP          // Lệnh yêu cầu ngắt/kết nối lại TCP
} uart_cmd_type_t;

typedef struct {
    uart_cmd_type_t cmd_type;       // Loại lệnh
    const char *payload;            // Con trỏ trỏ tới vùng đệm dữ liệu
    uint16_t length;                // Độ dài dữ liệu
    uint32_t timeout_ms;            // Timeout xử lý mong muốn (ms)
} uart_cmd_event_t;


// ============================================================================
// 3. STRUCT TRẠNG THÁI VẬN HÀNH TCP (Read-only cho các module khác)
// ============================================================================
typedef struct {
    bool is_wifi_connected;         // Trạng thái kết nối Wi-Fi
    bool is_socket_connected;       // Trạng thái kết nối Socket TCP
    uint32_t total_bytes_sent;      // Tổng số byte đã gửi thành công
    uint32_t total_bytes_received;  // Tổng số byte đã nhận
    int64_t last_sync_timestamp;    // Mốc thời gian gửi nhận thành công gần nhất
} tcp_status_t;


// ============================================================================
// 4. KHAI BÁO KIỂU CALLBACK FUNCTION
// ============================================================================
// Con trỏ hàm Callback: Truyền 1 con trỏ struct duy nhất
typedef void (*uart_event_callback_t)(const uart_cmd_event_t *event);

#endif