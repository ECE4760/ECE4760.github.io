#ifndef CHARACTER_H
#define CHARACTER_H

#include <stdint.h>

#define CLOUD_COUNT 6
#define CLOUD_WIDTH 16
#define CLOUD_HEIGHT 8

extern const uint16_t cloud_mask[CLOUD_HEIGHT];

void draw_cloud_bitmask_scaled(int x, int y, uint16_t color, float ratio);

#define COW_WIDTH 37
#define COW_HEIGHT 20

void draw_cow_bitmask_scaled(int x, int y, uint16_t color, float ratio);

extern const uint64_t cow_mask[COW_HEIGHT];

#define BEAR_WIDTH 35
#define BEAR_HEIGHT 31

extern const uint64_t bear_mask[BEAR_HEIGHT];

void draw_bear_bitmask_scaled(int x, int y, uint16_t color, float ratio);

#define BANANA_WIDTH 35
#define BANANA_HEIGHT 20

extern const uint64_t banana_mask[BANANA_HEIGHT];

void draw_banana_bitmask_scaled(int x, int y, uint16_t color, float ratio);

// power-ups: chimes, pumkin, dragon

#define DRAGON_WIDTH 50
#define DRAGON_HEIGHT 40

extern const uint64_t dragon_mask[DRAGON_HEIGHT];

void draw_dragon_bitmask_scaled(int x, int y, uint16_t color, float ratio);

#define PUMPKIN_WIDTH 32
#define PUMPKIN_HEIGHT 22

extern const uint64_t pumpkin_mask[PUMPKIN_HEIGHT];

void draw_pumpkin_bitmask_scaled(int x, int y, uint16_t color, float ratio);

#define BELL_WIDTH 16
#define BELL_HEIGHT 12

extern const uint64_t bell_mask[BELL_HEIGHT];

void draw_bell_bitmask_scaled(int x, int y, uint16_t color, float ratio);

#endif
