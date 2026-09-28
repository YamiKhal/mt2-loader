#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <windows.h>

#include "paths.h"

#define LOG_LINE_CAPACITY 2048

static HANDLE log_file = INVALID_HANDLE_VALUE;
static SRWLOCK log_lock = SRWLOCK_INIT;


bool log_open(const wchar_t* loader_dir) {
    wchar_t log_path[PATH_CAPACITY];
    wchar_t previous_log_path[PATH_CAPACITY];

    if (!paths_join(log_path, loader_dir, L"loader.log") || !paths_join(previous_log_path, loader_dir, L"loader.previous.log")) {
        return false;
    }

    MoveFileExW(log_path, previous_log_path, MOVEFILE_REPLACE_EXISTING);

    log_file = CreateFileW(
        log_path,
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    return log_file != INVALID_HANDLE_VALUE;
}


void log_line(const char* format, ...) {
    if (log_file == INVALID_HANDLE_VALUE) {
        return;
    }

    char line[LOG_LINE_CAPACITY];
    SYSTEMTIME now;
    GetLocalTime(&now);

    int prefix_length = snprintf(line, sizeof line, "[%02u:%02u:%02u.%03u] ", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);

    va_list arguments;
    va_start(arguments, format);
    int message_length = vsnprintf(line + prefix_length, sizeof line - prefix_length - 2, format, arguments);
    va_end(arguments);

    size_t length = prefix_length;

    if (message_length > 0) {
        size_t available = sizeof line - prefix_length - 3;
        length += (size_t)message_length < available ? (size_t)message_length : available;
    }

    line[length++] = '\r';
    line[length++] = '\n';

    AcquireSRWLockExclusive(&log_lock);

    DWORD written = 0;
    WriteFile(log_file, line, (DWORD)length, &written, NULL);

    ReleaseSRWLockExclusive(&log_lock);
}


void log_close(void) {
    AcquireSRWLockExclusive(&log_lock);

    if (log_file != INVALID_HANDLE_VALUE) {
        CloseHandle(log_file);
        log_file = INVALID_HANDLE_VALUE;
    }

    ReleaseSRWLockExclusive(&log_lock);
}
