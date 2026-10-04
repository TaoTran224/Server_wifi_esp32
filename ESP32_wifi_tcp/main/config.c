#include "config.h"
#include "app.h"

/*
 * File này giữ lại để tương thích với API cấu hình hiện tại.
 * Logic thật sự đang nằm ở app.c để tránh có 2 biến cấu hình riêng biệt.
 */

app_config_t *config_get_default_ptr(void)
{
    return app_config_get_ptr();
}

