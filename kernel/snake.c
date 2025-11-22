#include "snake.h"
#include "terminal.h"
#include "keyboard.h"
#include "timer.h"
#include "sound.h"
#include <stdint.h>

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
    terminal_set_theme(fg, bg);
    // We hack access via moving cursor and writing char for now,
    // as terminal.c doesn't expose direct set_at_xy
    // Using terminal batch commands would be cleaner, but we need X/Y
    // For now, we assume we can just clear screen and redraw, but that flickers.
    // Let's add a helper to terminal.c or just use internal knowledge?
    // Actually, terminal.c has no "set_cursor(x,y)" public API.
    // We will assume the shell cleared the screen, and we track cursor or 
    // reconstruct the scene.
    
    // Implementation Hack:
    // Since we don't have terminal_set_cursor(x,y), we rely on the fact
    // the terminal driver handles wrapping. But to do this properly in 
    // this specific OS architecture, we should probably add `terminal_set_cursor`
    // to terminal.h. For now, we will implement a poor man's version:
    // However, looking at terminal.c, terminal_row/col are static.
    // We will rely on a full redraw approach or modifying terminal.c?
    // Modification to terminal.c is best, but I am in snake.c.
    
    // WORKAROUND: We will abuse the fact that we are the only thing running.
    // We will implement a soft-buffer here if needed, or just accept
    // that we can't seek. 
    
    // WAIT! terminal_buffer is at 0xB8000. We can write directly!
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    vga[y * SCREEN_W + x] = (uint16_t)c | ((uint16_t)(fg | (bg << 4)) << 8);
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
            // Restore tail just in case (it grows)
            // Actually we just don't erase the tail next frame, but logic here is 
            // to append. The loop above shifted the array. The old tail pos is lost
            // unless we track it.
            // Simpler: Just extend len, the new segment appears at next move.
            // But we erased the tail visually above.
            // Let's redraw the new tail segment (which was the old tail)
            // Ideally, logic should separate update from draw.
            // Simplification: Grow logic -> Restore the visual of the tail we just erased
            // Actually, simpler to just NOT erase tail if eating.
            
            // FIX: Re-draw the tail we just erased if we grew
            // But we overwrote the coordinate in the array shift. 
            // Snake logic is tricky in C without dynamic lists.
            
            // Proper Logic:
            // If eating: shift is same, but we add a segment at the END equal to the old end.
            // Since we already shifted, the old end is gone from the array.
            // Let's fix the move logic:
            // 1. Calc new head.
            // 2. If Fruit: Add Head, Keep Tail (snake_len++).
            // 3. If No Fruit: Add Head, Remove Tail.
            
            // Revert logic for simplicity of this snippet:
            // We just moved. If head == fruit, we effectively grew into the empty space 
            // left by the tail? No.
            // We need to keep the tail. 
            // Let's just extend the array from the *back*.
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
