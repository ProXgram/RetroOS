#include "fs.h"

#include "kstring.h"
#include "os_info.h"
#include "syslog.h"
#include "ata.h"    // Added for persistence

#define FS_STORAGE_LBA 2048
#define FS_MAGIC_VAL   0xBA5EBA11

static struct fs_file FILES[FS_MAX_FILES];

// Helper to persist data
static void fs_sync_to_disk(void) {
    if (!ata_init()) return; // Cannot sync if drive fails

    // Calculate sectors needed
    // sizeof(FILES) is roughly 34KB => ~68 sectors
    uint32_t total_bytes = sizeof(FILES);
    uint8_t sectors = (uint8_t)((total_bytes + 511) / 512);

    // Write metadata (Magic Number) at FS_STORAGE_LBA
    // We use the first 4 bytes of a sector to store magic, rest is 0
    uint32_t magic_sector[128]; // 128 * 4 bytes = 512 bytes
    for(int i=0; i<128; i++) magic_sector[i] = 0;
    magic_sector[0] = FS_MAGIC_VAL;
    
    ata_write(FS_STORAGE_LBA, 1, (uint8_t*)magic_sector);

    // Write actual data at FS_STORAGE_LBA + 1
    ata_write(FS_STORAGE_LBA + 1, sectors, (uint8_t*)FILES);
}

static bool fs_load_from_disk(void) {
    if (!ata_init()) return false;

    uint32_t magic_sector[128];
    ata_read(FS_STORAGE_LBA, 1, (uint8_t*)magic_sector);

    if (magic_sector[0] != FS_MAGIC_VAL) {
        return false; // Not formatted or invalid magic
    }

    uint32_t total_bytes = sizeof(FILES);
    uint8_t sectors = (uint8_t)((total_bytes + 511) / 512);
    
    ata_read(FS_STORAGE_LBA + 1, sectors, (uint8_t*)FILES);
    return true;
}

static void fs_self_test(void);

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
    // Internal helper, does NOT trigger sync to avoid partial writes during init
    // We call touch logic manually here
    struct fs_file* existing = fs_find_mutable(name);
    struct fs_file* target = existing;

    if (target == NULL) {
        target = fs_allocate_slot();
        if (target == NULL) return false;
        target->in_use = true;
        fs_copy_name(target, name);
    }

    size_t length = kstrlen(contents);
    if (length >= FS_MAX_FILE_SIZE) return false;

    for (size_t i = 0; i < length; i++) {
        target->data[i] = contents[i];
    }
    target->data[length] = '\0';
    target->size = length;
    return true;
}

void fs_init(void) {
    // Try to load existing FS
    if (fs_load_from_disk()) {
        syslog_write("FS: loaded from persistent storage");
        return;
    }

    // Fallback: Fresh init
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

    syslog_write("FS: mounted fresh volume (unsaved)");
    
    // Save the fresh state immediately
    fs_sync_to_disk();
    syslog_write("FS: filesystem formatted and saved");
    
    fs_self_test();
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
    
    fs_sync_to_disk(); // Persist
    return true;
}

bool fs_write(const char* name, const char* contents) {
    if (name == NULL || contents == NULL) {
        return false;
    }

    bool existed = fs_find_mutable(name) != NULL;
    if (!fs_touch(name)) {
        return false;
    }

    struct fs_file* file = fs_find_mutable(name);
    if (file == NULL) {
        return false;
    }

    size_t length = kstrlen(contents);
    if (length >= FS_MAX_FILE_SIZE) {
        if (!existed) {
            fs_clear(file);
        }
        return false;
    }

    for (size_t i = 0; i < length; i++) {
        file->data[i] = contents[i];
    }
    file->data[length] = '\0';
    file->size = length;
    
    fs_sync_to_disk(); // Persist
    return true;
}

bool fs_append(const char* name, const char* contents) {
    if (name == NULL || contents == NULL) {
        return false;
    }

    bool existed = fs_find_mutable(name) != NULL;
    if (!fs_touch(name)) {
        return false;
    }

    struct fs_file* file = fs_find_mutable(name);
    if (file == NULL) {
        return false;
    }

    size_t length = kstrlen(contents);
    if (file->size + length >= FS_MAX_FILE_SIZE) {
        if (!existed) {
            fs_clear(file);
        }
        return false;
    }

    for (size_t i = 0; i < length; i++) {
        file->data[file->size + i] = contents[i];
    }
    file->size += length;
    file->data[file->size] = '\0';
    
    fs_sync_to_disk(); // Persist
    return true;
}

bool fs_remove(const char* name) {
    struct fs_file* file = fs_find_mutable(name);
    if (file == NULL) {
        return false;
    }

    fs_clear(file);
    fs_sync_to_disk(); // Persist
    return true;
}

static void fs_self_test(void) {
    const char* scratch = "__fs_self_test__";
    
    // Clean slate for the test file
    fs_remove(scratch);
    
    if (!fs_touch(scratch)) {
        syslog_write("FS: self-test (touch) failed");
        return;
    }
    
    if (!fs_remove(scratch)) {
        syslog_write("FS: self-test (remove) failed");
        return;
    }
    
    syslog_write("FS: self-test sequence complete");
}
