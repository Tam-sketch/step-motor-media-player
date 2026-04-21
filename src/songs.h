#ifndef SONGS_H
#define SONGS_H

#include "notes.h"
#include <stdint.h>


// Duration macros
#define MS_64      47
#define MS_32      94
#define MS_16     187
#define MS_Q      375
#define MS_DQ     562
#define MS_H      750
#define MS_DH    1125
#define MS_W     1500

#define STEPS(arr) (sizeof(arr)/sizeof(arr[0]))

// ← Step typedef MUST come before song #includes
typedef struct { uint8_t note; uint16_t dur_ms; } Step;
typedef struct { const char* name; const Step* steps; uint16_t length; } Song;

// ---- song includes ----
#include "songs/mozart-symphony40-1-piano-solo.h"
#include "songs/Tetris - A Theme.h"

static const Song song_list[] = {
    { "mozart-sym40", song_mozart_symphony40_1_piano_solo, STEPS(song_mozart_symphony40_1_piano_solo) },
    { "TetrisThemeA", song_tetris___a_theme, STEPS(song_tetris___a_theme) },
};

#define SONG_COUNT (sizeof(song_list)/sizeof(song_list[0]))

#endif