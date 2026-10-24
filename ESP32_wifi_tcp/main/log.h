#ifndef LOG_H
#define LOG_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define LOG_MAX_SIZE (uint8_t)1000

#define DBG_SEND

extern char log1[LOG_MAX_SIZE];
extern uint8_t logLen;
extern uint16_t logTime;

void DBG_SendStr(const char* str);

void DBG_SendBuffer(uint8_t *buf, const uint16_t len);

void DBG_SendHexToStr(uint8_t* buf, uint16_t len);

#endif
