#include "fs.h"

#include "kstring.h"
#include "os_info.h"
#include "syslog.h"

static struct fs_file FILES[FS_MAX_FILES];

static void fs_clear(struct fs_file* file) {
    if (file == NULL) {
        return;
    }

    file->in_use = false;
    file->name[0] = '\0';
    file->size = 0;
    file->data[0] = '\0';
}

static bool fs_is_valid_name(const char* name) {
    if (name == NULL || *name == '\0') {
        return false;
    }

    size_t length = 0;
    while (name[length] != '\0') {
        char c = name[length];
        if (c == ' ' || c == '\t' || c == '/' || c == '\\') {
            return false;
        }
        length++;
        if (length >= FS_MAX_FILENAME) {
            return false;
        }
    }

    return true;
}

static void fs_copy_name(struct fs_file* file, const char* name) {
    size_t i = 0;
    while (name[i] != '\0' && i + 1 < FS_MAX_FILENAME) {
        file->name[i] = name[i];
        i++;
    }
    file->name[i] = '\0';
}

static struct fs_file* fs_find_mutable(const char* name) {
    if (name == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < FS_MAX_FILES; i++) {
        if (!FILES[i].in_use) {
            continue;
        }
        if (kstrcmp(FILES[i].name, name) == 0) {
            return &FILES[i];
        }
    }

    return NULL;
}

static struct fs_file* fs_allocate_slot(void) {
    for (size_t i = 0; i < FS_MAX_FILES; i++) {
        if (!FILES[i].in_use) {
            return &FILES[i];
        }
    }
    return NULL;
}

static bool fs_seed_file(const char* name, const char* contents) {
    if (!fs_touch(name)) {
        return false;
    }
    return fs_write(name, contents);
}

void fs_init(void) {
    for (size_t i = 0; i < FS_MAX_FILES; i++) {
        fs_clear(&FILES[i]);
    }

    fs_seed_file(
        "readme.txt",
        OS_NAME " is a retro-themed playground kernel.\n"
        "Use 'help' to explore the built-in utilities.\n");

    fs_seed_file(
        "motd.txt",
        "Hold fast to curiosity and keep building!\n"
        "Type 'history' to revisit previous commands.\n");

    fs_seed_file(
        "colors.map",
        "Color IDs 0-15 follow the standard IBM PC palette.\n"
        "Run 'palette' to preview swatches.\n");

    fs_seed_file(
        "system.log",
        "Use the 'logs' command to view the in-memory event log.\n");

    syslog_write("FS: mounted retro volume");
}

size_t fs_file_count(void) {
    size_t count = 0;
    for (size_t i = 0; i < FS_MAX_FILES; i++) {
        if (FILES[i].in_use) {
            count++;
        }
    }
    return count;
}

const struct fs_file* fs_file_at(size_t index) {
    size_t seen = 0;
    for (size_t i = 0; i < FS_MAX_FILES; i++) {
        if (!FILES[i].in_use) {
            continue;
        }
        if (seen == index) {
            return &FILES[i];
        }
        seen++;
    }
    return NULL;
}

const struct fs_file* fs_find(const char* name) {
    return fs_find_mutable(name);
}

bool fs_touch(const char* name) {
    if (!fs_is_valid_name(name)) {
        return false;
    }

    struct fs_file* existing = fs_find_mutable(name);
    if (existing != NULL) {
        return true;
    }

    struct fs_file* slot = fs_allocate_slot();
    if (slot == NULL) {
        return false;
    }

    slot->in_use = true;
    fs_copy_name(slot, name);
    slot->size = 0;
    slot->data[0] = '\0';
    return true;
}

bool fs_write(const char* name, const char* contents) {
    if (contents == NULL) {
        return false;
    }

    if (!fs_touch(name)) {
        return false;
    }

    struct fs_file* file = fs_find_mutable(name);
    if (file == NULL) {
        return false;
    }

    size_t length = kstrlen(contents);
    if (length >= FS_MAX_FILE_SIZE) {
        return false;
    }

    for (size_t i = 0; i < length; i++) {
        file->data[i] = contents[i];
    }
    file->data[length] = '\0';
    file->size = length;
    return true;
}

bool fs_append(const char* name, const char* contents) {
    if (contents == NULL) {
        return false;
    }

    if (!fs_touch(name)) {
        return false;
    }

    struct fs_file* file = fs_find_mutable(name);
    if (file == NULL) {
        return false;
    }

    size_t length = kstrlen(contents);
    if (file->size + length >= FS_MAX_FILE_SIZE) {
        return false;
    }

    for (size_t i = 0; i < length; i++) {
        file->data[file->size + i] = contents[i];
    }
    file->size += length;
    file->data[file->size] = '\0';
    return true;
}

bool fs_remove(const char* name) {
    struct fs_file* file = fs_find_mutable(name);
    if (file == NULL) {
        return false;
    }

    fs_clear(file);
    return true;
}
