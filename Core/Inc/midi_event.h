#ifndef MIDI_EVENT_H
#define MIDI_EVENT_H

struct MidiEvent {
	uint8_t status;
	uint8_t note;
	uint8_t velocity;
	uint8_t msg_type;
	uint8_t channel;
};

typedef struct MidiEvent MidiEvent_t;

MidiEvent_t ParseMidiMessage(uint8_t* msg, uint32_t len) {
    MidiEvent_t e = {0};
    if (len < 4) return e;
    e.status   = msg[1];
    e.note     = msg[2];
    e.velocity = msg[3];
    e.msg_type = msg[1] & 0xF0;
    e.channel  = msg[1] & 0x0F;
    return e;
}

#endif
