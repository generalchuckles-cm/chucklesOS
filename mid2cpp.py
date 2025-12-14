import sys
import struct

# --- CONFIGURATION ---
INPUT_FILE = sys.argv[1] if len(sys.argv) > 1 else ""
OUTPUT_FILE = "kernel/src/sound/midi_data.h" # Generates Header now
VELOCITY_THRESHOLD = 50

# --- HELPER FUNCTIONS ---
def read_be32(f): return struct.unpack('>I', f.read(4))[0]
def read_be16(f): return struct.unpack('>H', f.read(2))[0]
def read_vlq(f):
    value = 0
    while True:
        byte = ord(f.read(1))
        value = (value << 7) | (byte & 0x7F)
        if not (byte & 0x80): break
    return value
def midi_note_to_freq(note):
    return int(440.0 * (2.0 ** ((note - 69) / 12.0)))

def main():
    if not INPUT_FILE:
        print("Usage: python midi2cpp.py <input.mid>")
        return

    print(f"Processing '{INPUT_FILE}'...")
    all_events = [] 
    ticks_per_beat = 480

    try:
        with open(INPUT_FILE, "rb") as f:
            if f.read(4) != b'MThd': return
            f.read(4)
            fmt, num_tracks, ticks_per_beat = struct.unpack('>HHH', f.read(6))
            
            for _ in range(num_tracks):
                while True:
                    chunk_type = f.read(4)
                    if not chunk_type: break
                    chunk_len = read_be32(f)
                    
                    if chunk_type == b'MTrk':
                        track_end = f.tell() + chunk_len
                        abs_tick = 0
                        running_status = 0
                        active_notes_in_track = set()
                        
                        while f.tell() < track_end:
                            abs_tick += read_vlq(f)
                            status = ord(f.read(1))
                            if status >= 0x80:
                                running_status = status
                                if status < 0xF0: pass
                            else:
                                f.seek(-1, 1)
                                status = running_status

                            cmd = status & 0xF0
                            if cmd == 0x80: # Off
                                note = ord(f.read(1)); f.read(1)
                                if note in active_notes_in_track:
                                    all_events.append((abs_tick, 2, note, 0))
                                    active_notes_in_track.remove(note)
                            elif cmd == 0x90: # On
                                note = ord(f.read(1)); vel = ord(f.read(1))
                                if vel == 0:
                                    if note in active_notes_in_track:
                                        all_events.append((abs_tick, 2, note, 0))
                                        active_notes_in_track.remove(note)
                                elif vel >= VELOCITY_THRESHOLD:
                                    all_events.append((abs_tick, 1, note, vel))
                                    active_notes_in_track.add(note)
                            elif cmd in [0xA0, 0xB0, 0xE0]: f.read(2)
                            elif cmd in [0xC0, 0xD0]: f.read(1)
                            elif status == 0xFF:
                                mtype = ord(f.read(1))
                                mlen = read_vlq(f)
                                if mtype == 0x51 and mlen == 3:
                                    val = f.read(3)
                                    tempo = (val[0]<<16)|(val[1]<<8)|val[2]
                                    all_events.append((abs_tick, 3, tempo, 0))
                                else: f.seek(mlen, 1)
                            elif status in [0xF0, 0xF7]: f.seek(read_vlq(f), 1)
                        break
                    else: f.seek(chunk_len, 1)
    except: pass

    all_events.sort(key=lambda x: x[0])
    
    print(f"Generating '{OUTPUT_FILE}'...")

    with open(OUTPUT_FILE, "w") as out:
        out.write("#ifndef MIDI_DATA_H\n#define MIDI_DATA_H\n\n#include <cstdint>\n\n")
        
        # Freq Table
        out.write('static const uint32_t g_freq_table[] = {\n')
        for i in range(0, 128, 16):
            out.write("    " + ", ".join([str(midi_note_to_freq(x)) for x in range(i, i+16)]) + ",\n")
        out.write('};\n\n')

        # Bytecode
        out.write('static const uint8_t g_music_data[] = {\n    ')
        
        current_tempo = 500000
        last_event_tick = 0
        byte_counter = 0

        def emit(val_str):
            nonlocal byte_counter
            out.write(val_str + ", ")
            byte_counter += 1
            if byte_counter % 16 == 0: out.write("\n    ")

        for ev in all_events:
            tick, type, d1, d2 = ev
            delta_ticks = tick - last_event_tick
            
            if delta_ticks > 0:
                ms_wait = int(delta_ticks * (current_tempo / 1000000.0) / ticks_per_beat * 1000)
                while ms_wait > 0:
                    chunk = 239 if ms_wait <= 239 else 65535 if ms_wait > 65535 else ms_wait
                    if chunk <= 239:
                        emit(f"0x{chunk:02X}"); ms_wait = 0
                    else:
                        emit("0xF0"); emit(f"0x{chunk&0xFF:02X}"); emit(f"0x{(chunk>>8)&0xFF:02X}"); ms_wait -= chunk
                last_event_tick = tick

            if type == 1:   emit("0xF1"); emit(f"0x{d1:02X}"); emit(f"0x{d2:02X}")
            elif type == 2: emit("0xF2"); emit(f"0x{d1:02X}")
            elif type == 3: current_tempo = d1

        out.write("0xFF };\n\n#endif\n")

if __name__ == "__main__": main()