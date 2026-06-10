#include "debug_uart.h"
#include "FreeRTOS.h"
#include "stm32f4xx_hal.h"
#include "task.h"
#include "queue.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

extern UART_HandleTypeDef huart2;

static QueueHandle_t debugQueue;

// Call once before starting the scheduler
void Debug_Init(void) {
    debugQueue = xQueueCreate(DEBUG_QUEUE_DEPTH, DEBUG_MSG_MAX_LEN);
    configASSERT(debugQueue != NULL);
}

// Call from any task — formats and enqueues
void Debug_Print(const char *fmt, ...) {
    char buf[DEBUG_MSG_MAX_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    // Drop message if queue full — never block audio tasks
    xQueueSend(debugQueue, buf, 0);
}
void Debug_PrintFromISR(const char *fmt, ...) {
    char buf[DEBUG_MSG_MAX_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    BaseType_t xHPTW = pdFALSE;
    xQueueSendFromISR(debugQueue, buf, &xHPTW);
    portYIELD_FROM_ISR(xHPTW);
}
// Minimal ISR-safe version (no formatting, no dynamic allocation)
//void Debug_PrintFromISR(const char *msg) {
//    char buf[DEBUG_MSG_MAX_LEN];
//    strncpy(buf, msg, DEBUG_MSG_MAX_LEN - 1);
//    buf[DEBUG_MSG_MAX_LEN - 1] = '\0';
//    BaseType_t xHPTW = pdFALSE;
//    xQueueSendFromISR(debugQueue, buf, &xHPTW);
//    portYIELD_FROM_ISR(xHPTW);
//}

// The actual UART task — lowest priority
void DebugUartTask(void *pv) {
    char buf[DEBUG_MSG_MAX_LEN];
    for (;;) {
        // Block indefinitely until a message arrives
        if (xQueueReceive(debugQueue, buf, portMAX_DELAY) == pdTRUE) {
            HAL_UART_Transmit(&huart2, (uint8_t *)buf, strlen(buf), 100);
        }
    }
}
