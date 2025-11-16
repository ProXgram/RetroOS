#ifndef FS_H
#define FS_H

#include <stddef.h>

struct fs_entry {
    const char* name;
    const char* description;
    const char* contents;
};

void fs_init(void);
size_t fs_entry_count(void);
const struct fs_entry* fs_entry_at(size_t index);
const struct fs_entry* fs_find(const char* name);

#endif /* FS_H */
