#include "session.h"

#include <stdio.h>
#include <windows.h>

#include "paths.h"

static wchar_t session_path[PATH_CAPACITY];
static bool session_active = false;
static int session_crash_streak = 0;


static bool read_marker(int* crash_streak, int* core_loaded) {
    HANDLE file = CreateFileW(session_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    char text[128] = {0};
    DWORD read = 0;
    ReadFile(file, text, sizeof text - 1, &read, NULL);
    CloseHandle(file);

    *crash_streak = 0;
    *core_loaded = 0;
    sscanf(text, "crash_streak=%d core_loaded=%d", crash_streak, core_loaded);

    return true;
}

static void write_marker(bool core_loaded) {
    HANDLE file = CreateFileW(session_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    char text[128];
    int length = snprintf(text, sizeof text, "crash_streak=%d core_loaded=%d\r\n", session_crash_streak, core_loaded ? 1 : 0);

    DWORD written = 0;
    WriteFile(file, text, (DWORD)length, &written, NULL);
    CloseHandle(file);
}


void session_begin(const wchar_t* loader_dir, SessionHistory* history) {
    history->previous_session_crashed = false;
    history->previous_session_had_core = false;
    history->crash_streak = 0;

    if (!paths_join(session_path, loader_dir, L"session.txt")) {
        return;
    }

    int previous_streak = 0;
    int previous_core_loaded = 0;

    // The marker is deleted on a clean exit, so finding it means the last session ended without one.
    if (read_marker(&previous_streak, &previous_core_loaded)) {
        history->previous_session_crashed = true;
        history->previous_session_had_core = previous_core_loaded != 0;

        // A crash without the loader's code running isn't counted against it.
        history->crash_streak = history->previous_session_had_core ? previous_streak + 1 : 0;
    }

    session_crash_streak = history->crash_streak;
    session_active = true;
    write_marker(false);
}

void session_mark_core_loaded(void) {
    if (session_active) {
        write_marker(true);
    }
}

void session_mark_clean_exit(void) {
    if (session_active) {
        DeleteFileW(session_path);
        session_active = false;
    }
}
