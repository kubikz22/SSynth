//#include "synth_core.h"
//
//
//uint16_t audio_buffer[AUDIO_BUFFER_SIZE];
//float wavetable[WAVETABLE_LENGTH];
//float current_wavetable_phase = 0.0f;
//uint16_t current_note = 440;
//float volume = 1.0f;
//
//
//void init_wavetable(void) {
////	float phase = 0;
////	float phase_step = (2.0f * M_PI) / (float)(WAVETABLE_LENGTH);
////	for (int i = 0; i < WAVETABLE_LENGTH; i++) {
////		wavetable[i] = sin(phase);
////		phase += phase_step;
////	}
////	for (int i = 0; i < WAVETABLE_LENGTH; i++) {
////		wavetable[i] = ((2.0f * (float)i) / (float)WAVETABLE_LENGTH) - 1.0f;
////	}
//	for (int i = 0; i < WAVETABLE_LENGTH / 2; i++)
//	{
//		wavetable[i] = 0;
//	}
//	for (int i = WAVETABLE_LENGTH / 2; i < WAVETABLE_LENGTH; i++)
//	{
//		wavetable[i] = 1;
//	}
//}
//
//uint16_t float2uint16(float f)
//{
//  return (uint16_t)(((int16_t)(32767*f + 32768.5)) - 32768);
//}
//void fill_buffer(uint32_t start_frame, uint32_t num_frames)
//{
//	float phase_inc = ((float)current_note / SAMPLE_RATE) * (float)(WAVETABLE_LENGTH);
//
//	for(int frame = start_frame; frame < start_frame+num_frames; frame++) {
//		float sample_f = volume * wavetable[((uint32_t)current_wavetable_phase) % WAVETABLE_LENGTH];
//		uint16_t sample = float2uint16(sample_f);
//
//		audio_buffer[2*frame] = sample;
//		audio_buffer[2*frame + 1] = sample;
//		current_wavetable_phase += phase_inc;
//		if(current_wavetable_phase > WAVETABLE_LENGTH) {
//		  current_wavetable_phase -= WAVETABLE_LENGTH;
//		}
//	}
//}
//
//
//
//void BSP_AUDIO_OUT_HalfTransfer_CallBack(void)
//{
//    fill_buffer(0, AUDIO_BUFFER_SAMPLES / 2);
//}
//
//void BSP_AUDIO_OUT_TransferComplete_CallBack(void)
//{
//    fill_buffer(AUDIO_BUFFER_SAMPLES / 2, AUDIO_BUFFER_SAMPLES / 2);
//}
//uint16_t* get_synth_audio_buffer(void) {
//	return audio_buffer;
//}
