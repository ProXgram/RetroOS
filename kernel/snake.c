#include "snake.h"
#include "terminal.h"
#include "keyboard.h"
#include "timer.h"
#include "sound.h"
#include <stdint.h>
#include <stdbool.h>

#define SCREEN_W 80
#define SCREEN_H 25
#define MAX_SNAKE (SCREEN_W * SCREEN_H)

typedef struct {
    int x;
    int y;
} Point;

static Point snake[MAX_SNAKE];
static int snake_len;
static Point fruit;
static int score;
static int dir_x; // -1, 0, 1
static int dir_y; // -1, 0, 1

// Pseudo-random generator
static unsigned long next = 1;
static int rand(void) {
    next = next * 1103515245 + 12345;
    return (unsigned int)(next / 65536) % 32768;
}

static void spawn_fruit(void) {
    bool collision;
    do {
        collision = false;
        fruit.x = rand() % (SCREEN_W - 2) + 1;
        fruit.y = rand() % (SCREEN_H - 2) + 1;

        for (int i = 0; i < snake_len; i++) {
            if (snake[i].x == fruit.x && snake[i].y == fruit.y) {
                collision = true;
                break;
            }
        }
    } while (collision);
}

static void draw_point(int x, int y, char c, uint8_t fg, uint8_t bg) {
    // FIXED: Removed terminal_set_theme() call.
    // calling terminal_* functions triggers a screen refresh from the text buffer,
    // which overwrites the game graphics. We write directly to VRAM instead.
    
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    uint16_t attrib = (uint16_t)((bg << 4) | (fg & 0x0F));
    vga[y * SCREEN_W + x] = (uint16_t)c | (attrib << 8);
}

void snake_game_run(void) {
    terminal_clear();
    
    // Init game state
    snake_len = 3;
    snake[0].x = 40; snake[0].y = 12;
    snake[1].x = 39; snake[1].y = 12;
    snake[2].x = 38; snake[2].y = 12;
    dir_x = 1; dir_y = 0;
    score = 0;
    next = (unsigned long)timer_get_ticks(); // Seed
    spawn_fruit();

    // Draw Border
    for (int x = 0; x < SCREEN_W; x++) {
        draw_point(x, 0, '#', 0x08, 0x00);
        draw_point(x, SCREEN_H-1, '#', 0x08, 0x00);
    }
    for (int y = 0; y < SCREEN_H; y++) {
        draw_point(0, y, '#', 0x08, 0x00);
        draw_point(SCREEN_W-1, y, '#', 0x08, 0x00);
    }

    // Intro Sound
    sound_beep(440, 10);
    sound_beep(554, 10);
    sound_beep(659, 20);

    bool running = true;
    while (running) {
        // 1. Input Handling (Non-blocking)
        char key = keyboard_poll_char();
        if (key == 'q') running = false;
        if (key == 'w' && dir_y == 0) { dir_x = 0; dir_y = -1; }
        if (key == 's' && dir_y == 0) { dir_x = 0; dir_y = 1; }
        if (key == 'a' && dir_x == 0) { dir_x = -1; dir_y = 0; }
        if (key == 'd' && dir_x == 0) { dir_x = 1; dir_y = 0; }

        // 2. Game Logic
        Point next_head = { snake[0].x + dir_x, snake[0].y + dir_y };

        // Wall Collision
        if (next_head.x <= 0 || next_head.x >= SCREEN_W - 1 ||
            next_head.y <= 0 || next_head.y >= SCREEN_H - 1) {
            running = false;
            sound_beep(100, 50); // Crash sound
            continue;
        }

        // Self Collision
        for (int i = 0; i < snake_len; i++) {
            if (snake[i].x == next_head.x && snake[i].y == next_head.y) {
                running = false;
                sound_beep(100, 50);
                break;
            }
        }
        if (!running) continue;

        // Move Snake
        // Erase tail
        draw_point(snake[snake_len-1].x, snake[snake_len-1].y, ' ', 0x07, 0x00);

        for (int i = snake_len - 1; i > 0; i--) {
            snake[i] = snake[i-1];
        }
        snake[0] = next_head;

        // Check Fruit
        if (snake[0].x == fruit.x && snake[0].y == fruit.y) {
            snake_len++;
            snake[snake_len] = snake[snake_len-1]; // Duplicate tail
            snake_len++;
            score += 10;
            sound_beep(1000 + (score * 5), 5); // Pickup sound
            spawn_fruit();
        }

        // Draw
        // Head
        draw_point(snake[0].x, snake[0].y, 'O', 0x0A, 0x00); // Green Head
        // Body (just overwrite the neck)
        draw_point(snake[1].x, snake[1].y, 'o', 0x02, 0x00); // Darker body
        
        // Fruit
        draw_point(fruit.x, fruit.y, '@', 0x0C, 0x00); // Red fruit

        // 3. Delay (Game Speed)
        timer_wait(5); // 5 ticks = 50ms approx
    }

    // Game Over Screen
    terminal_set_theme(0x0F, 0x00);
    terminal_clear();
    terminal_writestring("\n\n   GAME OVER\n");
    terminal_writestring("   Score: ");
    terminal_write_uint(score);
    terminal_writestring("\n   Press any key to exit...");
    
    // Flush input
    while(keyboard_poll_char()); 
    // Wait for key
    while(!keyboard_poll_char());
    
    // Restore terminal
    terminal_set_theme(0x0F, 0x01); // Restore blue background
    terminal_clear();
}
