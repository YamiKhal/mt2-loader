#include <share.h>
#include <stdio.h>
#include <windows.h>

#include "commands.h"
#include "game_exe.h"

#define POLL_MILLISECONDS 250


static long long size_of(const wchar_t* path) {
    WIN32_FILE_ATTRIBUTE_DATA data;

    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data)) {
        return -1;
    }

    return ((long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
}

// Prints what's in the file from where the last read stopped; the game appends while it runs.
static long long print_from(const wchar_t* path, long long from) {
    char buffer[8192];
    FILE* file = _wfsopen(path, L"rb", _SH_DENYNO);

    if (file == NULL) {
        return from;
    }

    _fseeki64(file, from, SEEK_SET);

    size_t read = 0;

    while ((read = fread(buffer, 1, sizeof buffer, file)) > 0) {
        fwrite(buffer, 1, read, stdout);
        from += (long long)read;
    }

    fflush(stdout);
    fclose(file);

    return from;
}


// The loader's log, as the game writes it: what each plugin logs, hooks, problems. A new game start begins a new log.
int command_log(const wchar_t* exe) {
    wchar_t path[GAME_PATH_CAPACITY];
    wchar_t log[GAME_PATH_CAPACITY];

    if (!game_exe_find(exe, path)) {
        return 1;
    }

    wchar_t* last_slash = wcsrchr(path, L'\\');

    if (last_slash != NULL) {
        *last_slash = L'\0';
    }

    swprintf(log, GAME_PATH_CAPACITY, L"%ls\\mt2loader\\loader.log", path);
    wprintf(L"%ls (Ctrl+C to stop)\n", log);
    fflush(stdout);

    long long shown = 0;

    for (;;) {
        long long size = size_of(log);

        // The game started again: the log began anew.
        if (size >= 0 && size < shown) {
            printf("\n--- the game started again ---\n");
            shown = 0;
        }

        if (size > shown) {
            shown = print_from(log, shown);
        }

        Sleep(POLL_MILLISECONDS);
    }
}
