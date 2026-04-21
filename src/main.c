// src/main.c
#include "player.h"

int main(void) {
    player_init();
    player_set_volume(75);
    player_play(0);
    while (1) {}
}