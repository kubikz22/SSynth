#pragma once
#include <stdint.h>

#define DEBUG_MSG_MAX_LEN  128
#define DEBUG_QUEUE_DEPTH  16

void  Debug_Init(void);
void  Debug_Print(const char *fmt, ...);   // safe from any task
//void  Debug_PrintFromISR(const char *msg); // safe from ISR (no formatting)
void Debug_PrintFromISR(const char *fmt, ...);
void  DebugUartTask(void *pv);             // register this with FreeRTOS
