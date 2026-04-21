# midi_to_song.py
# Usage: python midi_to_song.py <file.mid> [song display name]
# Auto-generates songs/<name>.h AND patches songs.h registry

import mido
import os
import re
import sys

# ===== CONFIG =====
BPM_FALLBACK = 120
SRC_DIR      = os.path.join(os.path.dirname(os.path.abspath(__file__)), "src")
SONGS_DIR    = os.path.join(SRC_DIR, "songs")
SONGS_H      = os.path.join(SRC_DIR, "songs.h")

STANDARD_DURATIONS = {
    "MS_64":   47,
    "MS_32":   94,
    "MS_16":  187,
    "MS_Q":   375,
    "MS_DQ":  562,
    "MS_H":   750,
    "MS_DH": 1125,
    "MS_W":  1500,
}

NOTE_NAMES = ['C','CS','D','DS','E','F','FS','G','GS','A','AS','B']

# ===== MIDI PARSE (your working logic, unchanged) =====
def midi_to_note_name(note):
    octave = (note // 12) - 1
    return f"NOTE_{NOTE_NAMES[note % 12]}{octave}"

def snap_duration(ms):
    best_name, best_diff = "MS_Q", float('inf')
    for name, val in STANDARD_DURATIONS.items():
        diff = abs(ms - val)
        if diff < best_diff:
            best_diff, best_name = diff, name
    return best_name, STANDARD_DURATIONS[best_name]

def load_midi(path):
    try:
        return mido.MidiFile(path, clip=True)
    except Exception as e:
        print("ERROR loading MIDI:", e)
        return None

def parse_midi(mid):
    tempo = mido.bpm2tempo(BPM_FALLBACK)
    ticks_per_beat = mid.ticks_per_beat
    events = []
    abs_tick = 0
    for msg in mido.merge_tracks(mid.tracks):
        abs_tick += msg.time
        events.append((abs_tick, msg))

    tick_to_sec = {}
    current_tempo = tempo
    last_tick = last_sec = 0
    for tick, msg in events:
        last_sec += mido.tick2second(tick - last_tick, ticks_per_beat, current_tempo)
        last_tick = tick
        tick_to_sec[tick] = last_sec
        if msg.is_meta and msg.type == 'set_tempo':
            current_tempo = msg.tempo

    notes, active = [], {}
    for tick, msg in events:
        if msg.is_meta:
            continue
        t = tick_to_sec.get(tick, 0.0)
        if msg.type == 'note_on' and msg.velocity > 0:
            active[msg.note] = t
        elif msg.type == 'note_off' or (msg.type == 'note_on' and msg.velocity == 0):
            if msg.note in active:
                start = active.pop(msg.note)
                dur = t - start
                if dur > 0.02:
                    notes.append((msg.note, start, dur))

    notes.sort(key=lambda x: x[1])
    return notes

def extract_melody_steps(notes):
    if not notes:
        return []
    SLOT = 0.05
    slots = {}
    for note, start, dur in notes:
        key = round(start / SLOT) * SLOT
        if key not in slots or note > slots[key][0]:
            slots[key] = (note, dur)
    return [(note, t, dur) for t in sorted(slots) for note, dur in [slots[t]]]

def build_steps(melody):
    steps = []
    for i, (note, start, dur) in enumerate(melody):
        if i > 0:
            _, prev_start, prev_dur = melody[i - 1]
            gap = start - (prev_start + prev_dur)
            if gap > 0.03:
                rest_name, rest_ms = snap_duration(gap * 1000)
                steps.append(("NOTE_REST", rest_name, rest_ms))
        dur_name, dur_ms = snap_duration(dur * 1000)
        steps.append((midi_to_note_name(note), dur_name, dur_ms))
    return steps

# ===== EXPORT individual song .h into songs/ folder =====
def export_song_h(steps, var_name, song_name, out_path):
    lines = [
        f"// Auto-generated from MIDI — do not edit manually",
        f"// Song: {song_name}",
        f"#pragma once",
        f'#include "../notes.h"',
        f"",
        f"static const Step {var_name}[] = {{",
    ]
    for note_name, dur_name, dur_ms in steps:
        lines.append(f"    {{{note_name}, {dur_name}}},  // {dur_ms}ms")
    lines.append("};")
    lines.append("")

    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"  Written: {out_path}  ({len(steps)} steps)")

# ===== PATCH songs.h — add #include + registry entry if not present =====
def patch_songs_h(var_name, song_name, h_filename):
    if not os.path.exists(SONGS_H):
        print(f"  WARNING: {SONGS_H} not found — skipping patch")
        return

    with open(SONGS_H, "r", encoding="utf-8") as f:
        content = f.read()

    # 1. Add #include if missing
    include_line = f'#include "songs/{h_filename}"'
    if include_line not in content:
        # Insert after last #include line
        last_include = list(re.finditer(r'^#include.*$', content, re.MULTILINE))
        if last_include:
            pos = last_include[-1].end()
            content = content[:pos] + "\n" + include_line + content[pos:]
        else:
            content = include_line + "\n" + content
        print(f"  Added:   {include_line}")
    else:
        print(f"  Already included: {include_line}")

    # 2. Add registry entry inside song_list[] if missing
    registry_entry = f'    {{ "{song_name}", {var_name}, STEPS({var_name}) }},'
    if var_name not in content:
        # Find closing }; of song_list[] array
        match = re.search(r'(static const Song song_list\[\].*?\{)(.*?)(\};)',
                          content, re.DOTALL)
        if match:
            before = content[:match.start(3)]
            after  = content[match.start(3):]
            # Remove trailing whitespace/newline before }; then add entry
            content = before.rstrip() + "\n" + registry_entry + "\n" + after
            print(f"  Added registry entry: {registry_entry.strip()}")
        else:
            print(f"  WARNING: Could not find song_list[] in songs.h")
            print(f"  Add manually: {registry_entry}")
    else:
        print(f"  Already in registry: {var_name}")

    with open(SONGS_H, "w", encoding="utf-8") as f:
        f.write(content)

# ===== MAIN =====
if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python midi_to_song.py <file.mid> [\"Display Name\"]")
        sys.exit(1)

    midi_path = sys.argv[1]
    if not os.path.exists(midi_path):
        print("File not found:", midi_path)
        sys.exit(1)

    # Derive names
    midi_basename = os.path.splitext(os.path.basename(midi_path))[0]
    song_name     = sys.argv[2] if len(sys.argv) > 2 else \
                    midi_basename.replace("-", " ").replace("_", " ").title()
    var_name      = "song_" + re.sub(r'[^a-z0-9]', '_',
                    midi_basename.lower()).strip('_')
    h_filename    = midi_basename + ".h"
    out_path      = os.path.join(SONGS_DIR, h_filename)

    print(f"Song:    {song_name}")
    print(f"Var:     {var_name}")
    print(f"Output:  {out_path}")
    print()

    # Ensure songs/ dir exists
    os.makedirs(SONGS_DIR, exist_ok=True)

    print("Loading MIDI...")
    mid = load_midi(midi_path)
    if mid is None:
        sys.exit(1)

    print("Parsing...")
    notes = parse_midi(mid)
    print(f"  {len(notes)} raw notes")

    melody = extract_melody_steps(notes)
    print(f"  {len(melody)} melody notes")

    steps = build_steps(melody)
    print(f"  {len(steps)} steps total")

    print("Exporting song header...")
    export_song_h(steps, var_name, song_name, out_path)

    print("Patching songs.h...")
    patch_songs_h(var_name, song_name, h_filename)

    print()
    print("DONE — build and upload.")