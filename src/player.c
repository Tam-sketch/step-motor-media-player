// src/player.c — engine + B1 button (PA0) → next song

#include "player.h"
#include "songs.h"
#include "stm32f4xx.h"

uint8_t volume = 75;

// ---------------------------------------------------------------------------
// SEQUENCER STATE
// ---------------------------------------------------------------------------
static const Song*       current_song  = 0;
static volatile uint16_t note_ms       = 0;   // ms remaining on current note
static volatile uint8_t  note_idx      = 0;
static volatile uint8_t  load_next     = 1;
static volatile uint8_t  current_song_idx = 0;

// ---------------------------------------------------------------------------
// CLOCK — HSI → PLL → 100MHz
// ---------------------------------------------------------------------------
static void clock_init(void) {
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY));
    RCC->PLLCFGR = (16<<0)|(200<<6)|(0<<16)|(0<<22);
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY));
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN | 3;
    RCC->CFGR  = (0<<4)|(4<<10)|(0<<13);
    RCC->CFGR |= 2;
    while (((RCC->CFGR>>2)&3) != 2);
}

// ---------------------------------------------------------------------------
// TIM1 — PWM on PA8 (TIM1_CH1)
// ---------------------------------------------------------------------------
static void tim1_init(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    GPIOA->MODER  = (GPIOA->MODER & ~(3<<16)) | (2<<16);  // PA8 AF
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~0xF)    | 1;        // AF1 = TIM1

    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
    TIM1->PSC   = 99;
    TIM1->ARR   = NOTE_ARR[NOTE_A4];
    TIM1->CCR1  = 0;
    TIM1->CCMR1 = (6<<4)|(1<<3);    // PWM mode 1, preload
    TIM1->CCER  = TIM_CCER_CC1E;
    TIM1->BDTR  = TIM_BDTR_MOE;
    TIM1->CR1  |= TIM_CR1_ARPE;
    TIM1->EGR   = TIM_EGR_UG;
}

// ---------------------------------------------------------------------------
// TIM4 — 1ms tick IRQ
// ---------------------------------------------------------------------------
static void tim4_init(void) {
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;
    TIM4->PSC  = 999;
    TIM4->ARR  = 99;
    TIM4->DIER = TIM_DIER_UIE;
    TIM4->EGR  = TIM_EGR_UG;
    TIM4->SR   = 0;
    TIM4->CR1  = TIM_CR1_CEN;
    NVIC_SetPriority(TIM4_IRQn, 1);
    NVIC_EnableIRQ(TIM4_IRQn);
}

// ---------------------------------------------------------------------------
// B1 USER BUTTON — PA0, active HIGH on DISCO board, no pull needed
// EXTI0 → next song on rising edge
// Debounce: ignore second press within 300ms using TIM4 ms counter
// ---------------------------------------------------------------------------
static void button_init(void) {
    // PA0 input, no pull (board has external pull-down, button pulls HIGH)
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    GPIOA->MODER &= ~(3<<0);   // PA0 = input
    GPIOA->PUPDR &= ~(3<<0);   // no pull

    // EXTI0 → PA0
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    SYSCFG->EXTICR[0] &= ~(0xF<<0);   // EXTI0 = PA (0000)

    EXTI->IMR  |= (1<<0);   // unmask EXTI0
    EXTI->RTSR |= (1<<0);   // rising edge trigger (button press = PA0 goes HIGH)
    EXTI->FTSR &= ~(1<<0);  // not falling

    NVIC_SetPriority(EXTI0_IRQn, 2);
    NVIC_EnableIRQ(EXTI0_IRQn);
}

// ---------------------------------------------------------------------------
// INTERNAL — apply one step to TIM1
// ---------------------------------------------------------------------------
static inline void play_step(uint8_t idx) {
    uint32_t arr = NOTE_ARR[current_song->steps[idx].note];
    TIM1->CR1 &= ~TIM_CR1_CEN;
    if (arr == 0) return;                   // REST
    TIM1->ARR  = arr;
    TIM1->CCR1 = (arr * volume) / 200;
    TIM1->EGR  = TIM_EGR_UG;
    TIM1->CR1 |= TIM_CR1_CEN;
}

// ---------------------------------------------------------------------------
// TIM4 ISR — 1ms sequencer tick
// ---------------------------------------------------------------------------
static volatile uint32_t ms_tick       = 0;   // free-running ms counter

void TIM4_IRQHandler(void) {
    TIM4->SR &= ~TIM_SR_UIF;
    ms_tick++;
    if (!current_song) return;

    if (load_next) {
        load_next = 0;
        play_step(note_idx);
        note_ms = current_song->steps[note_idx].dur_ms;   // direct ms now
        note_idx++;
        if (note_idx >= current_song->length) note_idx = 0;
    }

    if (note_ms > 0) note_ms--;
    if (note_ms == 0) load_next = 1;
}

// ---------------------------------------------------------------------------
// EXTI0 ISR — B1 button press → next song
// ---------------------------------------------------------------------------
static volatile uint32_t last_press_ms = 0;   // for debounce


// ms_tick incremented in TIM4 ISR — add this line to TIM4_IRQHandler above:
// (already handled below via a shared counter — see note)

void EXTI0_IRQHandler(void) {
    EXTI->PR = (1<<0);   // clear pending flag FIRST

    // Debounce: ignore if < 300ms since last press
    if ((ms_tick - last_press_ms) < 300) return;
    last_press_ms = ms_tick;

    player_next_song();
}

// ---------------------------------------------------------------------------
// PUBLIC API
// ---------------------------------------------------------------------------
void player_init(void) {
    clock_init();
    tim1_init();
    tim4_init();
    button_init();
}

void player_play(uint8_t song_index) {
    if (song_index >= SONG_COUNT) return;
    TIM1->CR1   &= ~TIM_CR1_CEN;
    note_idx     = 0;
    load_next    = 1;
    current_song = &song_list[song_index];
    current_song_idx = song_index;
}

void player_next_song(void) {
    uint8_t next = (current_song_idx + 1) % SONG_COUNT;
    player_play(next);
}

void player_stop(void) {
    current_song = 0;
    TIM1->CR1 &= ~TIM_CR1_CEN;
}

void player_set_volume(uint8_t vol) {
    volume = (vol > 100) ? 100 : vol;
}