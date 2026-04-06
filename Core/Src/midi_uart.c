//#include "midi_uart.h"
//
//static UART_HandleTypeDef *_huart;
//static uint8_t rx_byte;           /* single byte DMA/IT target            */
//static uint8_t midi_buf[3];       /* accumulate up to 3 bytes             */
//static uint8_t midi_buf_idx = 0;  /* how many bytes collected so far      */
//static uint8_t expected_len = 0;  /* how many bytes this message needs    */
//
///* ── How many data bytes follow each status byte ────────────────────────── */
//static uint8_t midi_message_length(uint8_t status)
//{
//    switch (status & 0xF0) {
//        case 0x80: return 3;   /* Note Off          */
//        case 0x90: return 3;   /* Note On           */
//        case 0xA0: return 3;   /* Aftertouch        */
//        case 0xB0: return 3;   /* Control Change    */
//        case 0xC0: return 2;   /* Program Change    */
//        case 0xD0: return 2;   /* Channel Pressure  */
//        case 0xE0: return 3;   /* Pitch Bend        */
//        default:   return 0;   /* ignore sysex etc. */
//    }
//}
//
///* ── Start listening for one byte ────────────────────────────────────────── */
//void MIDI_UART_Init(UART_HandleTypeDef *huart)
//{
//    _huart = huart;
//    HAL_UART_Receive_IT(_huart, &rx_byte, 1);
//}
//
///* ── Called from HAL_UART_RxCpltCallback ────────────────────────────────── */
//static void MIDI_UART_ByteReceived(uint8_t byte)
//{
//    /* Status byte — start of a new message */
//    if (byte & 0x80) {
//        midi_buf[0]   = byte;
//        midi_buf_idx  = 1;
//        expected_len  = midi_message_length(byte);
//        if (expected_len == 0) {
//            midi_buf_idx = 0;   /* unknown/sysex, reset */
//        }
//        return;
//    }
//
//    /* Data byte — ignore if we haven't seen a status yet */
//    if (expected_len == 0) return;
//
//    midi_buf[midi_buf_idx++] = byte;
//
//    /* Do we have a complete message? */
//    if (midi_buf_idx >= expected_len) {
//        uint8_t status   = midi_buf[0];
//        uint8_t data1    = midi_buf[1];
//        uint8_t data2    = (expected_len > 2) ? midi_buf[2] : 0;
//        uint8_t msg_type = status & 0xF0;
//        uint8_t channel  = status & 0x0F;
//
//        if (msg_type == 0x90 && data2 > 0) {
//            MIDI_NoteOn(channel, data1, data2);
//        }
//        else if (msg_type == 0x80 || (msg_type == 0x90 && data2 == 0)) {
//            MIDI_NoteOff(channel, data1);
//        }
//
//        /* Ready for next message — keep status for running status */
//        midi_buf_idx = 1;
//    }
//}
//
///* ── HAL UART RX complete callback — put this in stm32f4xx_it.c ─────────── */
//void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
//{
//    if (huart->Instance == USART2) {
//        MIDI_UART_ByteReceived(rx_byte);
//        /* Re-arm for next byte */
//        HAL_UART_Receive_IT(_huart, &rx_byte, 1);
//    }
//}
