#include "ghidra.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define OLDEST_JDK 21
#define COMMAND_CAPACITY 32768
#define LINE_CAPACITY 8192

typedef bool (*FolderTest)(const wchar_t* folder, void* found);

typedef struct JdkChoice {
    wchar_t folder[GHIDRA_PATH_CAPACITY];
    int major;
} JdkChoice;

static const wchar_t* const GHIDRA_PLACES[] = {
    L"%LOCALAPPDATA%\\Programs\\Ghidra\\ghidra_*",
    L"%LOCALAPPDATA%\\Programs\\ghidra_*",
    L"%USERPROFILE%\\ghidra_*",
    L"C:\\ghidra*",
    L"C:\\Program Files\\ghidra*",
    L"C:\\Tools\\ghidra*",
};

static const wchar_t* const JDK_PLACES[] = {
    L"C:\\Program Files\\Eclipse Adoptium\\jdk-*",
    L"C:\\Program Files\\Java\\jdk-*",
    L"C:\\Program Files\\Microsoft\\jdk-*",
    L"C:\\Program Files\\Zulu\\zulu-*",
    L"C:\\Program Files\\Amazon Corretto\\jdk*",
};


static bool is_file(const wchar_t* path) {
    DWORD attributes = GetFileAttributesW(path);

    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static bool is_ghidra(const wchar_t* folder, void* found) {
    wchar_t script[GHIDRA_PATH_CAPACITY];
    swprintf(script, GHIDRA_PATH_CAPACITY, L"%ls\\support\\analyzeHeadless.bat", folder);

    if (!is_file(script)) {
        return false;
    }

    swprintf((wchar_t*)found, GHIDRA_PATH_CAPACITY, L"%ls", folder);

    return true;
}

// A JDK's "release" file says JAVA_VERSION="21.0.12".
static int jdk_major(const wchar_t* folder) {
    wchar_t path[GHIDRA_PATH_CAPACITY];
    char line[256];
    int major = 0;

    swprintf(path, GHIDRA_PATH_CAPACITY, L"%ls\\bin\\java.exe", folder);

    if (!is_file(path)) {
        return 0;
    }

    swprintf(path, GHIDRA_PATH_CAPACITY, L"%ls\\release", folder);
    FILE* release = _wfopen(path, L"r");

    while (release != NULL && major == 0 && fgets(line, sizeof line, release) != NULL) {
        sscanf(line, "JAVA_VERSION=\"%d", &major);
    }

    if (release != NULL) {
        fclose(release);
    }

    return major;
}

// The oldest JDK that's new enough: the one Ghidra is tested with most.
static bool is_better_jdk(const wchar_t* folder, void* found) {
    JdkChoice* choice = found;
    int major = jdk_major(folder);

    if (major >= OLDEST_JDK && (choice->major == 0 || major < choice->major)) {
        swprintf(choice->folder, GHIDRA_PATH_CAPACITY, L"%ls", folder);
        choice->major = major;
    }

    return false;
}

// Calls test on each folder matching pattern (environment variables expanded), newest name first, until one says yes.
static bool each_folder(const wchar_t* pattern, FolderTest test, void* found) {
    wchar_t expanded[GHIDRA_PATH_CAPACITY];
    wchar_t names[64][MAX_PATH];
    int count = 0;
    WIN32_FIND_DATAW entry;

    ExpandEnvironmentStringsW(pattern, expanded, GHIDRA_PATH_CAPACITY);

    HANDLE search = FindFirstFileW(expanded, &entry);

    while (search != INVALID_HANDLE_VALUE && count < 64) {
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            wcscpy(names[count++], entry.cFileName);
        }

        if (!FindNextFileW(search, &entry)) {
            break;
        }
    }

    if (search != INVALID_HANDLE_VALUE) {
        FindClose(search);
    }

    wchar_t* last_slash = wcsrchr(expanded, L'\\');

    if (last_slash != NULL) {
        *last_slash = L'\0';
    }

    for (int index = count - 1; index >= 0; index--) {
        wchar_t folder[GHIDRA_PATH_CAPACITY];
        swprintf(folder, GHIDRA_PATH_CAPACITY, L"%ls\\%ls", expanded, names[index]);

        if (test(folder, found)) {
            return true;
        }
    }

    return false;
}

static bool find_ghidra_folder(const wchar_t* given, wchar_t* folder) {
    const wchar_t* from_environment = _wgetenv(L"GHIDRA_INSTALL_DIR");
    const wchar_t* chosen = given != NULL ? given : from_environment;

    if (chosen != NULL) {
        wchar_t inside[GHIDRA_PATH_CAPACITY];
        swprintf(inside, GHIDRA_PATH_CAPACITY, L"%ls\\ghidra_*", chosen);

        return is_ghidra(chosen, folder) || each_folder(inside, is_ghidra, folder);
    }

    for (size_t index = 0; index < sizeof GHIDRA_PLACES / sizeof GHIDRA_PLACES[0]; index++) {
        if (each_folder(GHIDRA_PLACES[index], is_ghidra, folder)) {
            return true;
        }
    }

    return false;
}

static bool find_jdk(wchar_t* folder) {
    const wchar_t* java_home = _wgetenv(L"JAVA_HOME");

    if (java_home != NULL && jdk_major(java_home) >= OLDEST_JDK) {
        swprintf(folder, GHIDRA_PATH_CAPACITY, L"%ls", java_home);

        return true;
    }

    JdkChoice choice = { .major = 0 };

    for (size_t index = 0; index < sizeof JDK_PLACES / sizeof JDK_PLACES[0]; index++) {
        each_folder(JDK_PLACES[index], is_better_jdk, &choice);
    }

    if (choice.major == 0) {
        return false;
    }

    swprintf(folder, GHIDRA_PATH_CAPACITY, L"%ls", choice.folder);

    return true;
}


void ghidra_look(const wchar_t* given, Ghidra* ghidra, int* java_major) {
    ghidra->folder[0] = L'\0';
    ghidra->java_home[0] = L'\0';
    *java_major = 0;

    if (!find_ghidra_folder(given, ghidra->folder)) {
        ghidra->folder[0] = L'\0';
    }

    if (find_jdk(ghidra->java_home)) {
        *java_major = jdk_major(ghidra->java_home);
    } else {
        ghidra->java_home[0] = L'\0';
    }
}

bool ghidra_find(const wchar_t* given, Ghidra* ghidra) {
    if (!find_ghidra_folder(given, ghidra->folder)) {
        fwprintf(stderr, L"Ghidra wasn't found. Get it from https://ghidra-sre.org (unzip it anywhere), then give its folder "
                         L"with --ghidra \"<folder>\" or set GHIDRA_INSTALL_DIR\n");

        return false;
    }

    if (!find_jdk(ghidra->java_home)) {
        fwprintf(stderr, L"Ghidra needs Java (a JDK) %d or newer, and none was found. Get one from https://adoptium.net "
                         L"(or: winget install EclipseAdoptium.Temurin.21.JDK), or set JAVA_HOME to it\n", OLDEST_JDK);

        return false;
    }

    return true;
}

// Half the PC's memory, from 4 to 16 GB: a first analysis of MT2.exe needs more than Ghidra's default 2 GB.
static void choose_memory(void) {
    MEMORYSTATUSEX status = { .dwLength = sizeof status };
    wchar_t memory[16];

    if (_wgetenv(L"GHIDRA_HEADLESS_MAXMEM") != NULL || !GlobalMemoryStatusEx(&status)) {
        return;
    }

    unsigned long long gigabytes = status.ullTotalPhys / (1024ull * 1024 * 1024) / 2;
    gigabytes = gigabytes < 4 ? 4 : gigabytes > 16 ? 16 : gigabytes;
    swprintf(memory, 16, L"%lluG", gigabytes);
    SetEnvironmentVariableW(L"GHIDRA_HEADLESS_MAXMEM", memory);
}

// cmd /s /c ""C:\...\analyzeHeadless.bat" "argument" ...": the outer quotes are taken off, the rest is kept as is.
static void build_command(const Ghidra* ghidra, const wchar_t* const* arguments, int argument_count, wchar_t* command) {
    size_t used = (size_t)swprintf(command, COMMAND_CAPACITY, L"cmd.exe /s /c \"\"%ls\\support\\analyzeHeadless.bat\"", ghidra->folder);

    for (int index = 0; index < argument_count && used < COMMAND_CAPACITY; index++) {
        used += (size_t)swprintf(command + used, COMMAND_CAPACITY - used, L" \"%ls\"", arguments[index]);
    }

    swprintf(command + used, COMMAND_CAPACITY - used, L"\"");
}

// The kit's scripts print "INFO  MT2Export.java> ... (GhidraScript)"; show those, and problems with scripts.
static void show_if_useful(char* line) {
    char* script = strstr(line, "(GhidraScript)");
    bool script_problem = strstr(line, ".java") != NULL && (strstr(line, "ERROR") != NULL || strstr(line, "rror:") != NULL);

    if (script != NULL) {
        char* start = strstr(line, "> ");
        *script = '\0';
        printf("  %s\n", start != NULL ? start + 2 : line);
    } else if (script_problem || strstr(line, "Import failed") != NULL || strstr(line, "java.lang.OutOfMemoryError") != NULL) {
        printf("  %s\n", line);
    }
}

static void read_output(HANDLE output, FILE* log) {
    char buffer[4096];
    char line[LINE_CAPACITY];
    size_t length = 0;
    DWORD read = 0;

    while (ReadFile(output, buffer, sizeof buffer, &read, NULL) && read > 0) {
        if (log != NULL) {
            fwrite(buffer, 1, read, log);
            fflush(log);
        }

        for (DWORD index = 0; index < read; index++) {
            if (buffer[index] == '\n' || length + 1 >= sizeof line) {
                line[length > 0 && line[length - 1] == '\r' ? length - 1 : length] = '\0';
                show_if_useful(line);
                length = 0;
            } else {
                line[length++] = buffer[index];
            }
        }

        fflush(stdout);
    }
}


bool ghidra_run(const Ghidra* ghidra, const wchar_t* const* arguments, int argument_count, const wchar_t* log_path) {
    static wchar_t command[COMMAND_CAPACITY];
    SECURITY_ATTRIBUTES inherit = { .nLength = sizeof inherit, .bInheritHandle = TRUE };
    HANDLE read_end = NULL;
    HANDLE write_end = NULL;

    SetEnvironmentVariableW(L"JAVA_HOME", ghidra->java_home);
    choose_memory();
    build_command(ghidra, arguments, argument_count, command);

    if (!CreatePipe(&read_end, &write_end, &inherit, 0)) {
        return false;
    }

    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup = { .cb = sizeof startup, .dwFlags = STARTF_USESTDHANDLES };
    startup.hStdOutput = write_end;
    startup.hStdError = write_end;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process;

    if (!CreateProcessW(NULL, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process)) {
        fwprintf(stderr, L"Ghidra couldn't be started (error %lu)\n", GetLastError());
        CloseHandle(read_end);
        CloseHandle(write_end);

        return false;
    }

    CloseHandle(write_end);

    FILE* log = _wfopen(log_path, L"ab");
    read_output(read_end, log);
    WaitForSingleObject(process.hProcess, INFINITE);

    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);

    if (log != NULL) {
        fclose(log);
    }

    CloseHandle(read_end);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);

    return exit_code == 0;
}
