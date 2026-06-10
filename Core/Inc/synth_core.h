#ifndef __SYNTH_CORE_H
#define __SYNTH_CORE_H

#include "audio_settings.h"
#include "stm32f4xx_hal.h"
#include "math.h"

#define WAVETABLE_LENGTH 128

uint16_t float2uint16(float f);
void fill_buffer(uint32_t start_frame, uint32_t num_frames);
void MIDI_NoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
void MIDI_NoteOff(uint8_t channel, uint8_t note);
void BSP_AUDIO_OUT_HalfTransfer_CallBack(void);
void BSP_AUDIO_OUT_TransferComplete_CallBack(void);

uint16_t* get_synth_audio_buffer(void);


#endif
