#include <stdio.h>

#define DMON_IMPL
#include "dmon.h"
#if defined(__linux__) || defined(linux) || defined(__linux)
#include "dmon_extra.h"
#endif /* defined(__linux__) || defined(linux) || defined(__linux) */

static void watch_callback(dmon_watch_id watch_id, dmon_action action, const char* rootdir,
                           const char* filepath, const char* oldfilepath, void* user)
{
    (void)(user);
    (void)(watch_id);

    switch (action) {
    case DMON_ACTION_CREATE:
        printf("CREATE: [%s]%s\n", rootdir, filepath);
        break;
    case DMON_ACTION_DELETE:
        printf("DELETE: [%s]%s\n", rootdir, filepath);
        break;
    case DMON_ACTION_MODIFY:
        printf("MODIFY: [%s]%s\n", rootdir, filepath);
        break;
    case DMON_ACTION_MOVE:
        printf("MOVE: [%s]%s -> [%s]%s\n", rootdir, oldfilepath, rootdir, filepath);
        break;
    }
}

static int prompt_command_loop(dmon_watch_id watch_id)
{
    char cmd[256];
    while (1) {
        fputs("> ", stdout);
        char *s = fgets(cmd, 256, stdin);
        char *newline = (s ? strchr(cmd, '\n') : NULL);
        if (!s || !newline) {
            fprintf(stderr, "error reading input\n");
            return 1;
        }
        *newline = 0;
        if (strncmp(cmd, "add ", 4) == 0) {
#if defined(__linux__) || defined(linux) || defined(__linux)
            char *subdir = cmd + 4;
            if (!dmon_watch_add(watch_id, subdir)) {
                fprintf(stdout, "cannot add directory %s\n", subdir);
            } else {
                fprintf(stdout, "added directory %s\n", subdir);
            }
#else
            fputs("dmon_watch_add not implemented for this OS", stderr);
#endif

        } else if (strncmp(cmd, "remove ", 7) == 0) {
#if defined(__linux__) || defined(linux) || defined(__linux)
            char *subdir = cmd + 7;
            if (!dmon_watch_rm(watch_id, subdir)) {
                fprintf(stdout, "cannot remove directory %s\n", subdir);
            } else {
                fprintf(stdout, "removed directory %s\n", subdir);
            }
#else
            fputs("dmon_watch_rm not implemented for this OS", stderr);
#endif
        } else if (strcmp(cmd, "exit") == 0) {
            return 0;
        } else {
            fprintf(stdout, "unknown command: %s\n", cmd);
        }
    }
}

int main(int argc, char* argv[])
{
    if (argc > 1) {
        dmon_init();
        puts("waiting for changes ..");
        /* We do not watch recursively. */
        dmon_watch_id watch_id = dmon_watch(argv[1], watch_callback, 0, NULL);
        prompt_command_loop(watch_id);
        dmon_deinit();
    } else {
        puts("usage: test dirname");
    }
    return 0;
}
