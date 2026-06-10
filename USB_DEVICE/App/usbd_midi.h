#ifndef __USBD_MIDI_H
#define __USBD_MIDI_H

#include "usbd_def.h"

#define MIDI_EPOUT_ADDR     0x01
#define MIDI_EPIN_ADDR      0x81
#define MIDI_EPOUT_SIZE     0x40

extern USBD_ClassTypeDef USBD_MIDI;

uint8_t MIDI_DataRx(uint8_t *msg, uint32_t len);
typedef void (*MIDI_ReceiveCallback_t)(uint8_t *data, uint32_t len);

void MIDI_RegisterReceiveCallback(MIDI_ReceiveCallback_t cb);
#endif
