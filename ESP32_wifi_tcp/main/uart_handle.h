#ifndef UART_HANDLE_H
#define UART_HANDLE_H

#include "driver/uart.h"
#include "app.h"

// --- CẤU HÌNH UART ---
#define UART_PORT_NUM       UART_NUM_0
#define UART_BUF_SIZE       1024

typedef enum {
    UART_CMD_WIFI = 0,
    UART_CMD_TCP,
    UART_CMD_HEARTBEAT
} uart_command_t;

typedef struct {
    uart_command_t cmd_type;
    char *cmd_str;
} uart_command_info_t;

typedef enum {
    UART_RESP_OK = 0,
    UART_RESP_ERROR_FORMAT,
    UART_RESP_ERROR_VALUE,
    UART_RESP_ERROR_NVS
} uart_response_t;

typedef enum {
    UART_NO_DATA = 0,
    UART_REC_DATA
} uart_flag_recv_t;

// Khởi tạo UART driver
void uart_handle_init(void);

// Đăng ký callback nhận thông tin parse được
void uart_handle_register_callback(uart_event_callback_t cb);

// Vòng lặp/Hàm kiểm tra dữ liệu UART
void check_uart_commands(void);

// Hàm hỗ trợ gửi phản hồi ra UART
void uart_send_response(const char *data);
#endif