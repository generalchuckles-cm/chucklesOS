#ifndef SYNTH_H
#define SYNTH_H

#include <cstdint>

#define AUDIO_SAMPLE_RATE 44100
#define MAX_POLYPHONY 32

enum Waveform { SQUARE, SAWTOOTH, SINE, KICK };

struct Voice {
    bool active;
    int midi_note;
    int channel;
    float phase;
    float phase_inc;
    float amplitude;
    Waveform wave;
    int32_t lifetime_samples;
    Voice* next;
};

class Synth {
public:
    Synth(int sample_rate);
    ~Synth();
    void note_on(int note, int vel, int chan, int inst);
    void note_off(int note, int chan);
    int16_t get_mono_sample();
    int get_active_voice_count() const { return active_count; }

private:
    Voice voice_pool[MAX_POLYPHONY];
    Voice* active_voices;
    int sample_rate;
    int active_count;
};

#endif