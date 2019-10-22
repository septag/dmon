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
#    ifndef MAX_PATH
#        define MAX_PATH 260
#    endif

#    include <intrin.h>
#    ifdef _MSC_VER
#        pragma intrinsic(_InterlockedExchange)
#    endif
#endif

// clang-format off
#ifndef DMON_MALLOC
#   include <stdlib.h>
#   define DMON_MALLOC(size)  malloc(size)
#   define DMON_FREE(ptr)     free(ptr)
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
#   define _DMON_LOG_ERRORF(str, ...) do { char msg[512]; snprintf(str, sizeof(str), __VA_ARGS__); DMON_LOG_ERROR(msg); } while(0);
#endif

#ifndef _DMON_LOG_DEBUGF
#   define _DMON_LOG_DEBUGF(str, ...) do { char msg[512]; snprintf(str, sizeof(str), __VA_ARGS__); DMON_LOG_DEBUG(msg); } while(0);
#endif

_DMON_PRIVATE char* dmon__strcpy(char* dst, int dst_sz, const char* src)
{
    DMON_ASSERT(dst);
    DMON_ASSERT(src);

    const int len = strlen(src);
    const int32_t max = dst_sz - 1;
    const int32_t num = (len < max ? len : max);
    memcpy(dst, src, num);
    dst[num] = '\0';

    return dst;
}

_DMON_PRIVATE char* dmon__unixpath(char* dst, int size, const char* path)
{
    int len = strlen(path);
    len = min(len, size - 1);

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

// IOCP (windows)
#if DMON_OS_WINDOWS
#ifdef UNICODE
#   define _DMON_WINAPI_STR(name, size) wchar_t _##name[size]; MultiByteToWideChar(CP_UTF8, 0, name, -1, 0, 0)
#else
#   define _DMON_WINAPI_STR(name, size) const char* _##name = name
#endif

typedef void (dmon__watch_cb)(dmon_watch_id, dmon_action, const char*, const char*, const char*, void*);

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
    char last_mod_file[MAX_PATH];
    char rootdir[MAX_PATH];
    char old_filepath[MAX_PATH];
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

        char abs_filepath[MAX_PATH];
        dmon__strcpy(abs_filepath, sizeof(abs_filepath), watch->rootdir);
        dmon__strcat(abs_filepath, sizeof(abs_filepath), filepath);

        WIN32_FILE_ATTRIBUTE_DATA fad;
        bool is_dir = false;
        if (GetFileAttributesExA(abs_filepath, GetFileExInfoStandard, &fad)) {
            is_dir = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? true : false;
        }

        // if moved directory is the root of other watch directories, change their root path
        if ((watch->watch_flags & DMON_WATCHFLAGS_RECURSIVE) && is_dir) {
            char abs_oldfilepath[MAX_PATH];
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
        return; // not handled        
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
                char filepath[MAX_PATH];
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
                                                    filepath, MAX_PATH - 1, NULL, NULL);
                    filepath[count] = TEXT('\0');
                    dmon__unixpath(filepath, sizeof(filepath), filepath);

                    if (notify->Action != FILE_ACTION_REMOVED &&
                        notify->Action != FILE_ACTION_RENAMED_OLD_NAME) {
                        WIN32_FILE_ATTRIBUTE_DATA fad;
                        char abs_filepath[MAX_PATH];
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


void dmon_init(void)
{
    InitializeCriticalSection(&_dmon.mutex);

    _dmon.thread_handle =
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)dmon__thread, NULL, 0, NULL);
    DMON_ASSERT(_dmon.thread_handle);
}


void dmon_deinit(void)
{
    for (int i = 0; i < _dmon.num_watches; i++) {
        dmon__unwatch(&_dmon.watches[i]);
    }

    _dmon.quit = true;
    if (_dmon.thread_handle != INVALID_HANDLE_VALUE) {
        WaitForSingleObject(_dmon.thread_handle, INFINITE);
        CloseHandle(_dmon.thread_handle);
    }

    DeleteCriticalSection(&_dmon.mutex);
}

dmon_watch_id dmon_watch(const char* rootdir,
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

    _DMON_WINAPI_STR(rootdir, MAX_PATH);
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

#endif    // DMON_OS_WINDOWS (IOCP)

// inotify (linux)

// FSEvents (MacOS)

// public interface
