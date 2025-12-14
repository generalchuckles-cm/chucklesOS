#include "synth.h"
#include "../cppstd/math.h"
#include "../cppstd/string.h"
#include "../memory/heap.h"

// Fixed math helper for frequency
static float midi_note_to_freq(int note) {
    if (note == 69) return 440.0f; // Exact A4
    return 440.0f * pow(2.0f, (float)(note - 69) / 12.0f);
}

Synth::Synth(int rate) : sample_rate(rate), active_count(0) {
    // Clear the voice pool
    memset(voice_pool, 0, sizeof(voice_pool));
    active_voices = nullptr;
}

Synth::~Synth() {
}

void Synth::note_on(int note, int velocity, int channel, int instrument) {
    if (velocity == 0) {
        note_off(note, channel);
        return;
    }

    // Allocate from fixed pool (simple linear search for inactive)
    Voice* v = nullptr;
    for (int i = 0; i < MAX_POLYPHONY; i++) {
        if (!voice_pool[i].active) {
            v = &voice_pool[i];
            break;
        }
    }
    if (!v) return; // Polyphony limit reached

    // Init Voice
    v->active = true;
    v->midi_note = note;
    v->channel = channel;
    v->phase = 0.0f;
    v->amplitude = (float)velocity / 127.0f;
    v->lifetime_samples = sample_rate * 2; // Auto-kill after 2s
    v->phase_inc = (2.0f * PI * midi_note_to_freq(note)) / (float)sample_rate;

    // Basic Instrument selection
    if (channel == 9) v->wave = KICK;
    else if (instrument < 8) v->wave = SQUARE;
    else if (instrument < 16) v->wave = SINE;
    else if (instrument < 24) v->wave = SQUARE;
    else v->wave = SAWTOOTH;

    // Link into active list
    v->next = active_voices;
    active_voices = v;
    active_count++;
}

void Synth::note_off(int note, int channel) {
    // Just mark inactive. The render loop cleans up.
    // This allows release tails if we implemented envelopes later.
    Voice* v = active_voices;
    while (v) {
        if (v->active && v->midi_note == note && v->channel == channel) {
            v->active = false;
            return;
        }
        v = v->next;
    }
}

int16_t Synth::get_mono_sample() {
    float sample = 0.0f;
    Voice* curr = active_voices;
    Voice* prev = nullptr;
    
    active_count = 0;

    while (curr) {
        // Lifecycle check
        if (!curr->active || curr->lifetime_samples-- <= 0) {
            // Remove from list
            curr->active = false;
            if (prev) prev->next = curr->next;
            else active_voices = curr->next;
            
            curr = (prev) ? prev->next : active_voices;
            continue;
        }

        active_count++;

        // Generate Wave
        float val = 0.0f;
        switch(curr->wave) {
            case SQUARE: val = (curr->phase < PI) ? 1.0f : -1.0f; break;
            case SAWTOOTH: val = (curr->phase / PI) - 1.0f; break;
            case SINE: val = sin(curr->phase); break;
            case KICK: 
                val = sin(curr->phase) * (1.0f - (curr->phase / (2.0f*PI))); 
                if (curr->phase >= 2.0f*PI) curr->active = false;
                break;
        }
        
        sample += val * curr->amplitude;
        
        // Advance Phase
        curr->phase += curr->phase_inc;
        if (curr->phase >= 2.0f * PI) curr->phase -= 2.0f * PI;

        prev = curr;
        curr = curr->next;
    }

    // Mixer Limiter
    sample *= 0.25f; 
    if (sample > 1.0f) sample = 1.0f;
    if (sample < -1.0f) sample = -1.0f;

    return (int16_t)(sample * 32767.0f);
}