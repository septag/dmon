#include <stdio.h>

#include "dmon.h"

static void watch_callback(dmon_watch_id watch_id, dmon_action action, const char* rootdir,
                           const char* filepath, const char* oldfilepath, void* user)
{
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

int main(int argc, char* argv[])
{
    dmon_init();
    puts("waiting for changes ..");
    dmon_watch("c:\\projects\\dmon\\test", watch_callback, DMON_WATCHFLAGS_RECURSIVE|DMON_WATCHFLAGS_IGNORE_DIRECTORIES, NULL); 
    getchar();
    dmon_deinit();
    return 0;
}
