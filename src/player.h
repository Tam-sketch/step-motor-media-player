// src/player.h

#ifndef PLAYER_H
#define PLAYER_H

#include <stdint.h>

extern uint8_t volume;   // 0–100

void player_init(void);
void player_play(uint8_t song_index);
void player_stop(void);
void player_set_volume(uint8_t vol);
void player_next_song(void);   // advance to next song, wraps around

#endif // PLAYER_H