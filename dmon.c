#include "dmon.h"

#define DMON_OS_WINDOWS 0
#define DMON_OS_MACOS 0
#define DMON_OS_LINUX 0

#if defined(_WIN32) || defined(_WIN64)
#    undef DMON_OS_WINDOWS
#    define DMON_OS_WINDOWS 1
#elif defined(__linux__)
#    undef DMON_OS_LINUX
#    define DMON_OS_LINUX 1
#elif defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
#    undef DMON_OS_MACOS
#    define DMON_OS_MACOS __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__
#else
#    define DMON_OS 0
#    error "unsupported platform"
#endif

#if DMON_OS_WINDOWS
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#    ifndef DMON_MAX_PATH
#        define DMON_MAX_PATH 260
#    endif

#    include <intrin.h>
#    ifdef _MSC_VER
#        pragma intrinsic(_InterlockedExchange)
#    endif
#elif DMON_OS_LINUX
#    ifndef __USE_MISC
#        define __USE_MISC
#    endif
#    include <dirent.h>
#    include <errno.h>
#    include <fcntl.h>
#    include <linux/limits.h>
#    include <pthread.h>
#    include <sys/inotify.h>
#    include <sys/stat.h>
#    include <sys/time.h>
#    include <time.h>
#    include <unistd.h>

#    define STB_STRETCHY_BUFFER_IMPL
#endif

// clang-format off
#ifndef DMON_MALLOC
#   include <stdlib.h>
#   define DMON_MALLOC(size)        malloc(size)
#   define DMON_FREE(ptr)           free(ptr)
#   define DMON_REALLOC(ptr, size)  realloc(ptr, size)
#endif

#ifndef DMON_ASSERT
#   include <assert.h>
#   define DMON_ASSERT(e)   assert(e)
#endif

#ifndef DMON_LOG_ERROR
#   include <stdio.h>
#   define DMON_LOG_ERROR(s)    do { puts(s); DMON_ASSERT(0); } while(0)
#endif

#ifndef DMON_LOG_DEBUG
#   ifndef NDEBUG
#       include <stdio.h>
#       define DMON_LOG_DEBUG(s)    do { puts(s); } while(0)
#   else
#       define DMON_LOG_DEBUG(s)    
#   endif
#endif

#ifndef DMON_API_DECL
#   define DMON_API_DECL
#endif

#ifndef DMON_API_IMPL
#   define DMON_API_IMPL
#endif

#ifndef DMON_MAX_WATCHES
#   define DMON_MAX_WATCHES 64
#endif

#ifndef DMON_MAX_PATH
#   define DMON_MAX_PATH 260
#endif

#define _DMON_UNUSED(x) (void)(x)

#ifndef _DMON_PRIVATE
#   if defined(__GNUC__)
#       define _DMON_PRIVATE __attribute__((unused)) static
#   else
#       define _DMON_PRIVATE static
#   endif
#endif

#include <string.h>

#ifndef _DMON_LOG_ERRORF
#   define _DMON_LOG_ERRORF(str, ...) do { char msg[512]; snprintf(msg, sizeof(msg), str, __VA_ARGS__); DMON_LOG_ERROR(msg); } while(0);
#endif

#ifndef _DMON_LOG_DEBUGF
#   define _DMON_LOG_DEBUGF(str, ...) do { char msg[512]; snprintf(msg, sizeof(msg), str, __VA_ARGS__); DMON_LOG_DEBUG(msg); } while(0);
#endif

#ifndef dmon__min
#   define dmon__min(a, b) ((a) < (b) ? (a) : (b))    
#endif

#ifndef dmon__max
#   define dmon__max(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef dmon__swap
#   define dmon__swap(a, b, _type)  \
        do {                        \
            _type tmp = a;          \
            a = b;                  \
            b = tmp;                \
        } while (0)
#endif

_DMON_PRIVATE char* dmon__strcpy(char* dst, int dst_sz, const char* src)
{
    DMON_ASSERT(dst);
    DMON_ASSERT(src);

    const int len = strlen(src);
    const int32_t _max = dst_sz - 1;
    const int32_t num = (len < _max ? len : _max);
    memcpy(dst, src, num);
    dst[num] = '\0';

    return dst;
}

_DMON_PRIVATE char* dmon__unixpath(char* dst, int size, const char* path)
{
    int len = strlen(path);
    len = dmon__min(len, size - 1);

    for (int i = 0; i < len; i++) {
        if (path[i] != '\\')
            dst[i] = path[i];
        else
            dst[i] = '/';
    }
    dst[len] = '\0';
    return dst;
}

_DMON_PRIVATE char* dmon__strcat(char* dst, int dst_sz, const char* src)
{
    int len = strlen(dst);
    return dmon__strcpy(dst + len, dst_sz - len, src);
}

// stretchy buffer: https://github.com/nothings/stb/blob/master/stretchy_buffer.h
#ifdef STB_STRETCHY_BUFFER_IMPL
#define stb_sb_free(a)         ((a) ? free(stb__sbraw(a)),0 : 0)
#define stb_sb_push(a,v)       (stb__sbmaybegrow(a,1), (a)[stb__sbn(a)++] = (v))
#define stb_sb_count(a)        ((a) ? stb__sbn(a) : 0)
#define stb_sb_add(a,n)        (stb__sbmaybegrow(a,n), stb__sbn(a)+=(n), &(a)[stb__sbn(a)-(n)])
#define stb_sb_last(a)         ((a)[stb__sbn(a)-1])
#define stb_sb_reset(a)        ((a) ? (stb__sbn(a) = 0) : 0)

#define stb__sbraw(a) ((int *) (a) - 2)
#define stb__sbm(a)   stb__sbraw(a)[0]
#define stb__sbn(a)   stb__sbraw(a)[1]

#define stb__sbneedgrow(a,n)  ((a)==0 || stb__sbn(a)+(n) >= stb__sbm(a))
#define stb__sbmaybegrow(a,n) (stb__sbneedgrow(a,(n)) ? stb__sbgrow(a,n) : 0)
#define stb__sbgrow(a,n)      (*((void **)&(a)) = stb__sbgrowf((a), (n), sizeof(*(a))))

#include <stdlib.h>

static void * stb__sbgrowf(void *arr, int increment, int itemsize)
{
   int dbl_cur = arr ? 2*stb__sbm(arr) : 0;
   int min_needed = stb_sb_count(arr) + increment;
   int m = dbl_cur > min_needed ? dbl_cur : min_needed;
   int *p = (int *) DMON_REALLOC(arr ? stb__sbraw(arr) : 0, itemsize * m + sizeof(int)*2);
   if (p) {
      if (!arr)
         p[1] = 0;
      p[0] = m;
      return p+2;
   } else {
      #ifdef STRETCHY_BUFFER_OUT_OF_MEMORY
      STRETCHY_BUFFER_OUT_OF_MEMORY ;
      #endif
      return (void *) (2*sizeof(int)); // try to force a NULL pointer exception later
   }
}
#endif // STB_STRETCHY_BUFFER_IMPL

// watcher callback (same as dmon.h's decleration)
typedef void (dmon__watch_cb)(dmon_watch_id, dmon_action, const char*, const char*, const char*, void*);

// IOCP (windows)
#if DMON_OS_WINDOWS
#ifdef UNICODE
#   define _DMON_WINAPI_STR(name, size) wchar_t _##name[size]; MultiByteToWideChar(CP_UTF8, 0, name, -1, 0, 0)
#else
#   define _DMON_WINAPI_STR(name, size) const char* _##name = name
#endif

typedef struct dmon__watch_state {
    dmon_watch_id id;
    OVERLAPPED overlapped;
    HANDLE dir_handle;
    uint8_t buffer[64512]; // http://msdn.microsoft.com/en-us/library/windows/desktop/aa365465(v=vs.85).aspx
    DWORD notify_filter;
    dmon__watch_cb* watch_cb;
    uint32_t watch_flags;
    void* user_data;
    uint64_t last_mod_time;
    uint64_t last_mod_size;
    char last_mod_file[DMON_MAX_PATH];
    char rootdir[DMON_MAX_PATH];
    char old_filepath[DMON_MAX_PATH];
} dmon__watch_state;

typedef struct dmon__state {
    int num_watches;
    dmon__watch_state watches[DMON_MAX_WATCHES];
    HANDLE thread_handle;
    CRITICAL_SECTION mutex;
    volatile LONG modify_watches;
    bool quit;
} dmon__state;

static dmon__state _dmon;

// clang-format on

_DMON_PRIVATE bool dmon__refresh_watch(dmon__watch_state* watch)
{
    return ReadDirectoryChangesW(watch->dir_handle, watch->buffer, sizeof(watch->buffer),
                                 (watch->watch_flags & DMON_WATCHFLAGS_RECURSIVE) ? TRUE : FALSE,
                                 watch->notify_filter, NULL, &watch->overlapped, NULL) != 0;
}

_DMON_PRIVATE void dmon__unwatch(dmon__watch_state* watch)
{
    CancelIo(watch->dir_handle);
    CloseHandle(watch->overlapped.hEvent);
    CloseHandle(watch->dir_handle);
}

_DMON_PRIVATE void dmon__notify_user(dmon__watch_state* watch, const char* filepath, DWORD action)
{
    dmon_action _action;

    switch (action) {
    case FILE_ACTION_RENAMED_OLD_NAME:
        dmon__strcpy(watch->old_filepath, sizeof(watch->old_filepath), filepath);
        return;
    case FILE_ACTION_ADDED:
        _action = DMON_ACTION_CREATE;
        break;
    case FILE_ACTION_RENAMED_NEW_NAME: {
        _action = DMON_ACTION_MOVE;

        char abs_filepath[DMON_MAX_PATH];
        dmon__strcpy(abs_filepath, sizeof(abs_filepath), watch->rootdir);
        dmon__strcat(abs_filepath, sizeof(abs_filepath), filepath);

        WIN32_FILE_ATTRIBUTE_DATA fad;
        bool is_dir = false;
        if (GetFileAttributesExA(abs_filepath, GetFileExInfoStandard, &fad)) {
            is_dir = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? true : false;
        }

        // if moved directory is the root of other watch directories, change their root path
        if ((watch->watch_flags & DMON_WATCHFLAGS_RECURSIVE) && is_dir) {
            char abs_oldfilepath[DMON_MAX_PATH];
            dmon__strcpy(abs_oldfilepath, sizeof(abs_oldfilepath) - 1, watch->rootdir);
            dmon__strcat(abs_oldfilepath, sizeof(abs_oldfilepath) - 1, watch->old_filepath);
            int rootdir_len = strlen(abs_oldfilepath);
            if (abs_oldfilepath[rootdir_len - 1] != '/') {
                abs_oldfilepath[rootdir_len] = '/';
                abs_oldfilepath[rootdir_len + 1] = '\0';
            }

            for (int i = 0; i < _dmon.num_watches; i++) {
                if (strcmp(_dmon.watches[i].rootdir, abs_oldfilepath) == 0) {
                    dmon__strcpy(_dmon.watches[i].rootdir, sizeof(_dmon.watches[i].rootdir),
                                 abs_oldfilepath);
                }
            }
        }
    } break;
    case FILE_ACTION_REMOVED:
        _action = DMON_ACTION_DELETE;
        break;
    case FILE_ACTION_MODIFIED:
        _action = DMON_ACTION_MODIFY;
        break;
    default:
        return;    // not handled
    }

    DMON_ASSERT(watch->watch_cb);
    watch->watch_cb(watch->id, _action, watch->rootdir, filepath,
                    _action != DMON_ACTION_MOVE ? NULL : watch->old_filepath, watch->user_data);
}

_DMON_PRIVATE DWORD WINAPI dmon__thread(LPVOID arg)
{
    HANDLE wait_handles[DMON_MAX_WATCHES];

    while (!_dmon.quit) {
        if (_dmon.modify_watches || !TryEnterCriticalSection(&_dmon.mutex)) {
            Sleep(10);
            continue;
        }

        if (_dmon.num_watches == 0) {
            Sleep(10);
            LeaveCriticalSection(&_dmon.mutex);
            continue;
        }

        for (int i = 0; i < _dmon.num_watches; i++) {
            dmon__watch_state* watch = &_dmon.watches[i];
            wait_handles[i] = watch->overlapped.hEvent;
        }

        DWORD wait_result = WaitForMultipleObjects(_dmon.num_watches, wait_handles, FALSE, 10);
        DMON_ASSERT(wait_result != WAIT_FAILED);
        if (wait_result != WAIT_TIMEOUT) {
            dmon__watch_state* watch = &_dmon.watches[WAIT_OBJECT_0 - wait_result];
            DMON_ASSERT(HasOverlappedIoCompleted(&watch->overlapped));

            DWORD bytes;
            if (GetOverlappedResult(watch->dir_handle, &watch->overlapped, &bytes, FALSE)) {
                char filepath[DMON_MAX_PATH];
                PFILE_NOTIFY_INFORMATION notify;
                size_t offset = 0;

                if (bytes == 0) {
                    dmon__refresh_watch(watch);
                    LeaveCriticalSection(&_dmon.mutex);
                    continue;
                }

                do {
                    notify = (PFILE_NOTIFY_INFORMATION)&watch->buffer[offset];
                    offset += notify->NextEntryOffset;

                    int count = WideCharToMultiByte(CP_UTF8, 0, notify->FileName,
                                                    notify->FileNameLength / sizeof(WCHAR),
                                                    filepath, DMON_MAX_PATH - 1, NULL, NULL);
                    filepath[count] = TEXT('\0');
                    dmon__unixpath(filepath, sizeof(filepath), filepath);

                    if (notify->Action != FILE_ACTION_REMOVED &&
                        notify->Action != FILE_ACTION_RENAMED_OLD_NAME) {
                        WIN32_FILE_ATTRIBUTE_DATA fad;
                        char abs_filepath[DMON_MAX_PATH];
                        dmon__strcpy(abs_filepath, sizeof(abs_filepath), watch->rootdir);
                        dmon__strcat(abs_filepath, sizeof(abs_filepath), filepath);
                        if (!GetFileAttributesExA(abs_filepath, GetFileExInfoStandard, &fad)) {
                            continue;
                        }

                        if ((watch->watch_flags & DMON_WATCHFLAGS_IGNORE_DIRECTORIES) &&
                            (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                            continue;
                        }

                        // remove duplicate entries if action is MODIFIED
                        if (notify->Action == FILE_ACTION_MODIFIED) {
                            LARGE_INTEGER tm;
                            tm.HighPart = fad.ftLastWriteTime.dwHighDateTime;
                            tm.LowPart = fad.ftLastWriteTime.dwLowDateTime;
                            uint64_t last_mod = (uint64_t)(tm.QuadPart / 10000000 - 11644473600LL);

                            if (watch->last_mod_time == last_mod &&
                                strcmp(filepath, watch->last_mod_file) == 0) {
                                continue;
                            } else {
                                watch->last_mod_time = last_mod;
                                // watch->last_mod_size = file_size;
                                dmon__strcpy(watch->last_mod_file, sizeof(watch->last_mod_file),
                                             filepath);
                            }
                        }
                    }    // notify->action != FILE_ACTION_MODIFIED

                    dmon__notify_user(watch, filepath, notify->Action);
                } while (notify->NextEntryOffset > 0);

                if (!_dmon.quit) {
                    dmon__refresh_watch(watch);
                }
            }
        }    // if (WaitForMultipleObjects)

        LeaveCriticalSection(&_dmon.mutex);
    }
    return 0;
}


DMON_API_IMPL void dmon_init(void)
{
    InitializeCriticalSection(&_dmon.mutex);

    _dmon.thread_handle =
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)dmon__thread, NULL, 0, NULL);
    DMON_ASSERT(_dmon.thread_handle);
}


DMON_API_IMPL void dmon_deinit(void)
{
    _dmon.quit = true;
    if (_dmon.thread_handle != INVALID_HANDLE_VALUE) {
        WaitForSingleObject(_dmon.thread_handle, INFINITE);
        CloseHandle(_dmon.thread_handle);
    }

    for (int i = 0; i < _dmon.num_watches; i++) {
        dmon__unwatch(&_dmon.watches[i]);
    }

    DeleteCriticalSection(&_dmon.mutex);
}

DMON_API_IMPL dmon_watch_id dmon_watch(const char* rootdir,
                                       void (*watch_cb)(dmon_watch_id watch_id, dmon_action action,
                                                        const char* dirname, const char* filename,
                                                        const char* oldname, void* user),
                                       uint32_t flags, void* user_data)
{
    DMON_ASSERT(watch_cb);
    DMON_ASSERT(rootdir && rootdir[0]);

    _InterlockedExchange(&_dmon.modify_watches, 1);
    EnterCriticalSection(&_dmon.mutex);

    DMON_ASSERT(_dmon.num_watches < DMON_MAX_WATCHES);

    uint32_t id = ++_dmon.num_watches;
    dmon__watch_state* watch = &_dmon.watches[id - 1];
    watch->id = (dmon_watch_id){ id };
    watch->watch_flags = flags;
    watch->watch_cb = watch_cb;
    watch->user_data = user_data;

    dmon__strcpy(watch->rootdir, sizeof(watch->rootdir) - 1, rootdir);
    dmon__unixpath(watch->rootdir, sizeof(watch->rootdir), rootdir);
    int rootdir_len = strlen(watch->rootdir);
    if (watch->rootdir[rootdir_len - 1] != '/') {
        watch->rootdir[rootdir_len] = '/';
        watch->rootdir[rootdir_len + 1] = '\0';
    }

    _DMON_WINAPI_STR(rootdir, DMON_MAX_PATH);
    watch->dir_handle =
        CreateFile(_rootdir, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                   NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
    if (watch->dir_handle != INVALID_HANDLE_VALUE) {
        watch->notify_filter = FILE_NOTIFY_CHANGE_CREATION | FILE_NOTIFY_CHANGE_LAST_WRITE |
                               FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                               FILE_NOTIFY_CHANGE_SIZE;
        watch->overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        DMON_ASSERT(watch->overlapped.hEvent != INVALID_HANDLE_VALUE);

        if (!dmon__refresh_watch(watch)) {
            dmon__unwatch(watch);
            DMON_LOG_ERROR("ReadDirectoryChanges failed");
            LeaveCriticalSection(&_dmon.mutex);
            _InterlockedExchange(&_dmon.modify_watches, 0);
            return (dmon_watch_id){ 0 };
        }
    } else {
        _DMON_LOG_ERRORF("Could not open: %s", rootdir);
        LeaveCriticalSection(&_dmon.mutex);
        _InterlockedExchange(&_dmon.modify_watches, 0);
        return (dmon_watch_id){ 0 };
    }

    LeaveCriticalSection(&_dmon.mutex);
    _InterlockedExchange(&_dmon.modify_watches, 0);
    return (dmon_watch_id){ id };
}
#elif DMON_OS_LINUX

#    define _DMON_TEMP_BUFFSIZE ((sizeof(struct inotify_event) + PATH_MAX) * 1024)

typedef struct dmon__watch_subdir {
    char rootdir[DMON_MAX_PATH];
} dmon__watch_subdir;

typedef struct dmon__inotify_event {
    char filepath[DMON_MAX_PATH];
    uint32_t mask;
    uint32_t cookie;
    dmon_watch_id watch_id;
    bool skip;
} dmon__inotify_event;

typedef struct dmon__watch_state {
    dmon_watch_id id;
    int fd;
    uint32_t watch_flags;
    dmon__watch_cb* watch_cb;
    void* user_data;
    char rootdir[DMON_MAX_PATH];
    dmon__watch_subdir* subdirs;
    int* wds;
} dmon__watch_state;

typedef struct dmon__state {
    dmon__watch_state watches[DMON_MAX_WATCHES];
    dmon__inotify_event* events;
    int num_watches;
    volatile int modify_watches;
    pthread_t thread_handle;
    pthread_mutex_t mutex;
    bool quit;
} dmon__state;

static dmon__state _dmon;

_DMON_PRIVATE void dmon__watch_recursive(const char* dirname, int fd, uint32_t mask,
                                         bool followlinks, dmon__watch_state* watch)
{
    struct dirent* entry;
    DIR* dir = opendir(dirname);
    DMON_ASSERT(dir);

    char watchdir[DMON_MAX_PATH];

    while ((entry = readdir(dir)) != NULL) {
        bool entry_valid = false;
        if (entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, "..") != 0 && strcmp(entry->d_name, ".") != 0) {
                dmon__strcpy(watchdir, sizeof(watchdir), dirname);
                dmon__strcat(watchdir, sizeof(watchdir), entry->d_name);
                entry_valid = true;
            }
        } else if (followlinks && entry->d_type == DT_LNK) {
            char linkpath[PATH_MAX];
            dmon__strcpy(watchdir, sizeof(watchdir), dirname);
            dmon__strcat(watchdir, sizeof(watchdir), entry->d_name);
            char* r = realpath(watchdir, linkpath);
            _DMON_UNUSED(r);
            DMON_ASSERT(r);
            dmon__strcpy(watchdir, sizeof(watchdir), linkpath);
            entry_valid = true;
        }

        // add sub-directory to watch dirs
        if (entry_valid) {
            int watchdir_len = strlen(watchdir);
            if (watchdir[watchdir_len - 1] != '/') {
                watchdir[watchdir_len] = '/';
                watchdir[watchdir_len + 1] = '\0';
            }
            int wd = inotify_add_watch(fd, watchdir, mask);
            _DMON_UNUSED(wd);
            DMON_ASSERT(wd != -1);

            dmon__watch_subdir subdir;
            dmon__strcpy(subdir.rootdir, sizeof(subdir.rootdir), watchdir);
            stb_sb_push(watch->subdirs, subdir);
            stb_sb_push(watch->wds, wd);

            // recurse
            dmon__watch_recursive(watchdir, fd, mask, followlinks, watch);
        }
    }
    closedir(dir);
}

_DMON_PRIVATE const char* dmon__find_subdir(const dmon__watch_state* watch, int wd)
{
    const int* wds = watch->wds;
    for (int i = 0, c = stb_sb_count(wds); i < c; i++) {
        if (wd == wds[i]) {
            return watch->subdirs[i].rootdir;
        }
    }

    DMON_ASSERT(0);
    return NULL;
}

_DMON_PRIVATE void dmon__inotify_process_events(void)
{
    for (int i = 0, c = stb_sb_count(_dmon.events); i < c; i++) {
        dmon__inotify_event* ev = &_dmon.events[i];
        if (ev->skip) {
            continue;
        }

        // remove redundant modify events on a single file
        if (ev->mask == IN_MODIFY) {
            for (int j = i + 1; j < c; j++) {
                dmon__inotify_event* check_ev = &_dmon.events[j];
                if (check_ev->mask == IN_MODIFY && strcmp(ev->filepath, check_ev->filepath) == 0) {
                    ev->skip = true;
                    break;
                }
            }
        } else if (ev->mask == IN_CREATE) {
            bool loop_break = false;
            for (int j = i + 1; j < c && !loop_break; j++) {
                dmon__inotify_event* check_ev = &_dmon.events[j];
                if (check_ev->mask == IN_MOVED_FROM &&
                    strcmp(ev->filepath, check_ev->filepath) == 0) {
                    // there is a case where some programs (like gedit):
                    // when we save, it creates a temp file, and moves it to the file being modified
                    // search for these cases and remove all of them
                    for (int k = j + 1; k < c; k++) {
                        dmon__inotify_event* third_ev = &_dmon.events[k];
                        if (third_ev->mask == IN_MOVED_TO && check_ev->cookie == third_ev->cookie) {
                            third_ev->mask = IN_MODIFY;    // change to modified
                            ev->skip = check_ev->skip = true;
                            loop_break = true;
                            break;
                        }
                    }
                } else if (check_ev->mask == IN_MODIFY &&
                           strcmp(ev->filepath, check_ev->filepath) == 0) {
                    // Another case is that file is copied. CREATE and MODIFY happens sequentially
                    // so we ignore modify event
                    check_ev->skip = true;
                }
            }
        } else if (ev->mask == IN_MOVED_FROM) {
            bool move_valid = false;
            for (int j = i + 1; j < c; j++) {
                dmon__inotify_event* check_ev = &_dmon.events[j];
                if (check_ev->mask == IN_MOVED_TO && ev->cookie == check_ev->cookie) {
                    move_valid = true;
                    break;
                }
            }

            // in some environments like nautilus file explorer:
            // when a file is deleted, it is moved to recycle bin
            // so if the destination of the move is not valid, it's probably DELETE
            if (!move_valid) {
                ev->mask = IN_DELETE;
            }
        } else if (ev->mask == IN_MOVED_TO) {
            bool move_valid = false;
            for (int j = 0; j < i; j++) {
                dmon__inotify_event* check_ev = &_dmon.events[j];
                if (check_ev->mask == IN_MOVED_FROM && ev->cookie == check_ev->cookie) {
                    move_valid = true;
                    break;
                }
            }

            // in some environments like nautilus file explorer:
            // when a file is deleted, it is moved to recycle bin, on undo it is moved back it
            // so if the destination of the move is not valid, it's probably CREATE
            if (!move_valid) {
                ev->mask = IN_CREATE;
            }
        }
    }

    // trigger user callbacks
    for (int i = 0, c = stb_sb_count(_dmon.events); i < c; i++) {
        dmon__inotify_event* ev = &_dmon.events[i];
        if (ev->skip) {
            continue;
        }
        dmon__watch_state* watch = &_dmon.watches[ev->watch_id.id - 1];

        switch (ev->mask) {
        case IN_CREATE:
            watch->watch_cb(ev->watch_id, DMON_ACTION_CREATE, watch->rootdir, ev->filepath, NULL,
                            watch->user_data);
            break;
        case IN_MODIFY:
            watch->watch_cb(ev->watch_id, DMON_ACTION_MODIFY, watch->rootdir, ev->filepath, NULL,
                            watch->user_data);
            break;
        case IN_MOVED_FROM: {
            for (int j = i + 1; j < c; j++) {
                dmon__inotify_event* check_ev = &_dmon.events[j];
                if (ev->mask == IN_MOVED_TO && ev->cookie == check_ev->cookie) {
                    watch->watch_cb(check_ev->watch_id, DMON_ACTION_MOVE, watch->rootdir,
                                    check_ev->filepath, ev->filepath, watch->user_data);
                    break;
                }
            }
        } break;
        case IN_DELETE:
            watch->watch_cb(ev->watch_id, DMON_ACTION_DELETE, watch->rootdir, ev->filepath, NULL,
                            watch->user_data);
            break;
        }
    }


    stb_sb_reset(_dmon.events);
}

static void* dmon__thread(void* arg)
{
    static uint8_t buff[_DMON_TEMP_BUFFSIZE];
    struct timespec req = { (time_t)10 / 1000, (long)(10 * 1000000) };
    struct timespec rem = { 0, 0 };
    struct timeval timeout = { .tv_usec = 100000 };
    uint64_t usecs_elapsed = 0;

    struct timeval starttm;
    gettimeofday(&starttm, 0);

    while (!_dmon.quit) {

        if (_dmon.modify_watches || pthread_mutex_trylock(&_dmon.mutex) != 0) {
            nanosleep(&req, &rem);
            continue;
        }

        if (_dmon.num_watches == 0) {
            nanosleep(&req, &rem);
            pthread_mutex_unlock(&_dmon.mutex);
            continue;
        }

        for (int i = 0; i < _dmon.num_watches; i++) {
            dmon__watch_state* watch = &_dmon.watches[i];
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(watch->fd, &rfds);

            if (select(FD_SETSIZE, &rfds, NULL, NULL, &timeout)) {
                ssize_t offset = 0;
                ssize_t len = read(watch->fd, buff, _DMON_TEMP_BUFFSIZE);
                if (len <= 0) {
                    continue;
                }

                while (offset < len) {
                    struct inotify_event* iev = (struct inotify_event*)&buff[offset];

                    char filepath[DMON_MAX_PATH];
                    dmon__strcpy(filepath, sizeof(filepath), dmon__find_subdir(watch, iev->wd));
                    dmon__strcat(filepath, sizeof(filepath), iev->name);

                    if (stb_sb_count(_dmon.events) == 0) {
                        usecs_elapsed = 0;
                    }
                    dmon__inotify_event dev = { .mask = iev->mask,
                                                .cookie = iev->cookie,
                                                .watch_id = watch->id };
                    dmon__strcpy(dev.filepath, sizeof(dev.filepath), filepath);
                    stb_sb_push(_dmon.events, dev);

                    offset += sizeof(struct inotify_event) + iev->len;
                }
            }
        }

        pthread_mutex_unlock(&_dmon.mutex);

        struct timeval tm;
        gettimeofday(&tm, 0);
        long dt = (tm.tv_sec - starttm.tv_sec) * 1000000 + tm.tv_usec - starttm.tv_usec;
        starttm = tm;
        usecs_elapsed += dt;
        if (usecs_elapsed > 100000 && stb_sb_count(_dmon.events) > 0) {
            dmon__inotify_process_events();
            usecs_elapsed = 0;
        }
    }
    return 0x0;
}

_DMON_PRIVATE void dmon__unwatch(dmon__watch_state* watch)
{
    close(watch->fd);
    stb_sb_free(watch->subdirs);
    stb_sb_free(watch->wds);
    memset(watch, 0x0, sizeof(dmon__watch_state));
}

DMON_API_IMPL void dmon_init(void)
{
    pthread_mutex_init(&_dmon.mutex, NULL);

    int r = pthread_create(&_dmon.thread_handle, NULL, dmon__thread, NULL);
    _DMON_UNUSED(r);
    DMON_ASSERT(r == 0 && "pthread_create failed");
}

DMON_API_IMPL void dmon_deinit(void)
{
    _dmon.quit = true;
    pthread_join(_dmon.thread_handle, NULL);

    for (int i = 0; i < _dmon.num_watches; i++) {
        dmon__unwatch(&_dmon.watches[i]);
    }

    pthread_mutex_destroy(&_dmon.mutex);
    stb_sb_free(_dmon.events);
}

DMON_API_IMPL dmon_watch_id dmon_watch(const char* rootdir,
                                       void (*watch_cb)(dmon_watch_id watch_id, dmon_action action,
                                                        const char* dirname, const char* filename,
                                                        const char* oldname, void* user),
                                       uint32_t flags, void* user_data)
{
    DMON_ASSERT(watch_cb);
    DMON_ASSERT(rootdir && rootdir[0]);

    __sync_lock_test_and_set(&_dmon.modify_watches, 1);
    pthread_mutex_lock(&_dmon.mutex);

    DMON_ASSERT(_dmon.num_watches < DMON_MAX_WATCHES);

    uint32_t id = ++_dmon.num_watches;
    dmon__watch_state* watch = &_dmon.watches[id - 1];
    watch->id = (dmon_watch_id){ id };
    watch->watch_flags = flags;
    watch->watch_cb = watch_cb;
    watch->user_data = user_data;

    struct stat root_st;
    if (stat(rootdir, &root_st) != 0 || !S_ISDIR(root_st.st_mode) ||
        (root_st.st_mode & S_IRUSR) != S_IRUSR) {
        _DMON_LOG_ERRORF("Could not open/read directory: %s", rootdir);
        goto ret_error;
    }


    if (S_ISLNK(root_st.st_mode)) {
        if (flags & DMON_WATCHFLAGS_FOLLOW_SYMLINKS) {
            char linkpath[PATH_MAX];
            char* r = realpath(rootdir, linkpath);
            _DMON_UNUSED(r);
            DMON_ASSERT(r);

            dmon__strcpy(watch->rootdir, sizeof(watch->rootdir) - 1, linkpath);
        } else {
            _DMON_LOG_ERRORF("symlinks are unsupported: %s. use DMON_WATCHFLAGS_FOLLOW_SYMLINKS",
                             rootdir);
            goto ret_error;
        }
    } else {
        dmon__strcpy(watch->rootdir, sizeof(watch->rootdir) - 1, rootdir);
    }

    // add trailing slash
    int rootdir_len = strlen(watch->rootdir);
    if (watch->rootdir[rootdir_len - 1] != '/') {
        watch->rootdir[rootdir_len] = '/';
        watch->rootdir[rootdir_len + 1] = '\0';
    }

    watch->fd = inotify_init();
    if (watch->fd < -1) {
        DMON_LOG_ERROR("could not create inotify instance");
        goto ret_error;
    }

    uint32_t inotify_mask = IN_MOVED_TO | IN_CREATE | IN_MOVED_FROM | IN_DELETE | IN_MODIFY;
    int wd = inotify_add_watch(watch->fd, watch->rootdir, inotify_mask);
    if (wd < 0) {
        _DMON_LOG_ERRORF("watch failed: %s", watch->rootdir);
        goto ret_error;
    }
    dmon__watch_subdir subdir;
    dmon__strcpy(subdir.rootdir, sizeof(subdir.rootdir), watch->rootdir);
    stb_sb_push(watch->subdirs, subdir);
    stb_sb_push(watch->wds, wd);

    // recursive mode: enumarate all child directories and add them to watch
    if (flags & DMON_WATCHFLAGS_RECURSIVE) {
        dmon__watch_recursive(watch->rootdir, watch->fd, inotify_mask,
                              (flags & DMON_WATCHFLAGS_FOLLOW_SYMLINKS) ? true : false, watch);
    }


    pthread_mutex_unlock(&_dmon.mutex);
    __sync_lock_test_and_set(&_dmon.modify_watches, 0);
    return (dmon_watch_id){ id };

ret_error:
    pthread_mutex_unlock(&_dmon.mutex);
    __sync_lock_test_and_set(&_dmon.modify_watches, 0);
    return (dmon_watch_id){ 0 };
}

DMON_API_IMPL void dmon_unwatch(dmon_watch_id id)
{
    DMON_ASSERT(id.id > 0);

    __sync_lock_test_and_set(&_dmon.modify_watches, 1);
    pthread_mutex_lock(&_dmon.mutex);

    DMON_ASSERT(id.id <= _dmon.num_watches);

    int index = id.id - 1;
    dmon__unwatch(&_dmon.watches[index]);
    if (index != _dmon.num_watches - 1) {
        dmon__swap(_dmon.watches[index], _dmon.watches[_dmon.num_watches-1], dmon__watch_state);
    }
    --_dmon.num_watches;

    pthread_mutex_unlock(&_dmon.mutex);
    __sync_lock_test_and_set(&_dmon.modify_watches, 0);
}

#endif   
