
#include "log.h"
#include "uart_handle.h"

char log1[LOG_MAX_SIZE];
uint8_t logLen;
uint16_t logTime;

void DBG_SendBuffer(uint8_t *buf, const uint16_t len)
{
uart_write_bytes(UART_PORT_NUM, buf, len);
}

void DBG_SendStr(const char* str)
{
	uart_write_bytes(UART_PORT_NUM, (uint8_t*)str, strlen(str));
}
void ConvertHexToStr(uint8_t* ch, uint8_t hex)
{
	if (9 >= (hex>>4))
	{
		ch[0] = (hex>>4) + 0x30;
	}
	else
	{
		ch[0] = (hex>>4) + 0x37;
	}
	if (9 >= (0x0F & hex))
	{
		ch[1] = (0x0F & hex) + 0x30;
	}
	else
	{
		ch[1] = (0x0F & hex) + 0x37;
	}
}

void DBG_SendHexToStr(uint8_t* buf, uint16_t len)
{
	uint8_t str[3] = {0,0,0x20};//0x20:space
	DBG_SendStr("\nHEX[");
	for (uint16_t i = 0; i < len; i++)
	{
		ConvertHexToStr(str, buf[i]);
		if ((len - 1) == i)
		{
			DBG_SendBuffer(str, sizeof(str) - 1);
			break;
		}
		DBG_SendBuffer(str, sizeof(str));
	}
	DBG_SendStr("]ENDHEX\n");
}
