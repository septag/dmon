#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct { uint32_t id; } dmon_watch_id;

typedef enum dmon_watch_flags_t {
    DMON_WATCHFLAGS_RECURSIVE = 0x1,
    DMON_WATCHFLAGS_FOLLOW_SYMLINKS = 0x2,
    DMON_WATCHFLAGS_OUTOFSCOPE_LINKS = 0x4,
    DMON_WATCHFLAGS_IGNORE_DIRECTORIES = 0x8
} dmon_watch_flags;

typedef enum dmon_action_t {
    DMON_ACTION_CREATE = 1,
    DMON_ACTION_DELETE,
    DMON_ACTION_MODIFY,
    DMON_ACTION_MOVE
} dmon_action;

void dmon_init(void);
void dmon_deinit(void);

dmon_watch_id dmon_watch(const char* rootdir,
                         void (*watch_cb)(dmon_watch_id watch_id, dmon_action action,
                                          const char* rootdir, const char* filepath,
                                          const char* oldfilepath, void* user),
                         uint32_t flags, void* user_data);
void dmon_unwatch(dmon_watch_id id);
