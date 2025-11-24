#include "heap.h"
#include "syslog.h"

// A simple linked-list First-Fit allocator
// Header for each block
struct heap_block {
    size_t size;        // Size of data part
    bool is_free;       // Is this block free?
    struct heap_block* next;
};

static struct heap_block* g_head = NULL;
static size_t g_heap_total_size = 0;

void heap_init(void* start_addr, size_t size_bytes) {
    if (size_bytes < sizeof(struct heap_block)) {
        syslog_write("Heap: Too small to initialize");
        return;
    }

    g_head = (struct heap_block*)start_addr;
    g_head->size = size_bytes - sizeof(struct heap_block);
    g_head->is_free = true;
    g_head->next = NULL;
    g_heap_total_size = size_bytes;
    
    syslog_write("Heap: Initialized");
}

void* kmalloc(size_t size) {
    if (size == 0 || g_head == NULL) return NULL;

    // Align size to 16 bytes
    size_t aligned_size = (size + 15) & ~15;
    
    struct heap_block* curr = g_head;
    while (curr) {
        if (curr->is_free && curr->size >= aligned_size) {
            // Found a fit
            // Check if we can split
            if (curr->size >= aligned_size + sizeof(struct heap_block) + 16) {
                struct heap_block* new_block = (struct heap_block*)((uint8_t*)curr + sizeof(struct heap_block) + aligned_size);
                
                new_block->size = curr->size - aligned_size - sizeof(struct heap_block);
                new_block->is_free = true;
                new_block->next = curr->next;
                
                curr->size = aligned_size;
                curr->next = new_block;
            }
            curr->is_free = false;
            return (void*)((uint8_t*)curr + sizeof(struct heap_block));
        }
        curr = curr->next;
    }
    
    syslog_write("Heap: Out of memory");
    return NULL;
}

void kfree(void* ptr) {
    if (!ptr) return;

    // Get header
    struct heap_block* block = (struct heap_block*)((uint8_t*)ptr - sizeof(struct heap_block));
    block->is_free = true;

    // Merge with next if free
    if (block->next && block->next->is_free) {
        block->size += sizeof(struct heap_block) + block->next->size;
        block->next = block->next->next;
    }
    
    // Simple global merge pass (inefficient but safe for small heaps)
    struct heap_block* curr = g_head;
    while (curr && curr->next) {
        if (curr->is_free && curr->next->is_free) {
            curr->size += sizeof(struct heap_block) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}

size_t heap_free_space(void) {
    size_t total = 0;
    struct heap_block* curr = g_head;
    while (curr) {
        if (curr->is_free) total += curr->size;
        curr = curr->next;
    }
    return total;
}
