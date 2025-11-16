#include "fs.h"

#include "kstring.h"
#include "os_info.h"
#include "syslog.h"

static const struct fs_entry FS_ENTRIES[] = {
    {
        .name = "readme.txt",
        .description = "Project overview",
        .contents =
            OS_NAME " is a retro-themed playground kernel.\n"
            "Use 'help' to explore the built-in utilities.\n",
    },
    {
        .name = "motd.txt",
        .description = "Message of the day",
        .contents =
            "Hold fast to curiosity and keep building!\n"
            "Type 'history' to revisit previous commands.\n",
    },
    {
        .name = "colors.map",
        .description = "VGA palette reference",
        .contents =
            "Color IDs 0-15 follow the standard IBM PC palette.\n"
            "Run 'palette' to preview swatches.\n",
    },
    {
        .name = "system.log",
        .description = "Live diagnostics pointer",
        .contents =
            "Use the 'logs' command to view the in-memory event log.\n",
    },
};

void fs_init(void) {
    syslog_write("FS: mounted retro volume");
}

size_t fs_entry_count(void) {
    return sizeof(FS_ENTRIES) / sizeof(FS_ENTRIES[0]);
}

const struct fs_entry* fs_entry_at(size_t index) {
    if (index >= fs_entry_count()) {
        return NULL;
    }
    return &FS_ENTRIES[index];
}

const struct fs_entry* fs_find(const char* name) {
    if (name == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < fs_entry_count(); i++) {
        if (kstrcmp(FS_ENTRIES[i].name, name) == 0) {
            return &FS_ENTRIES[i];
        }
    }

    return NULL;
}
