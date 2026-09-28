#include "paths.h"

#include <wchar.h>

#define GAME_EXE_NAME L"MT2.exe"
#define LOADER_FOLDER_NAME L"mt2loader"


static bool module_path(HMODULE module, wchar_t* path) {
    DWORD length = GetModuleFileNameW(module, path, PATH_CAPACITY);

    return length > 0 && length < PATH_CAPACITY;
}

static wchar_t* file_name_of(wchar_t* path) {
    wchar_t* last_separator = wcsrchr(path, L'\\');

    return last_separator ? last_separator + 1 : path;
}


bool paths_join(wchar_t* result, const wchar_t* folder, const wchar_t* name) {
    size_t folder_length = wcslen(folder);
    size_t name_length = wcslen(name);

    if (folder_length + 1 + name_length + 1 > PATH_CAPACITY) {
        return false;
    }

    wmemcpy(result, folder, folder_length);
    result[folder_length] = L'\\';
    wmemcpy(result + folder_length + 1, name, name_length + 1);

    return true;
}

bool paths_exists(const wchar_t* path) {
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

bool paths_is_folder(const wchar_t* path) {
    DWORD attributes = GetFileAttributesW(path);

    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}


bool paths_find(HMODULE proxy_module, ProxyPaths* paths) {
    if (!module_path(NULL, paths->exe_path) || !module_path(proxy_module, paths->game_dir)) {
        return false;
    }

    wchar_t* proxy_name = file_name_of(paths->game_dir);

    if (proxy_name == paths->game_dir) {
        return false;
    }

    proxy_name[-1] = L'\0';

    return paths_join(paths->loader_dir, paths->game_dir, LOADER_FOLDER_NAME);
}

bool paths_host_is_game(const ProxyPaths* paths) {
    wchar_t exe_path[PATH_CAPACITY];
    wmemcpy(exe_path, paths->exe_path, PATH_CAPACITY);

    wchar_t* exe_name = file_name_of(exe_path);

    if (_wcsicmp(exe_name, GAME_EXE_NAME) != 0) {
        return false;
    }

    exe_name[exe_name == exe_path ? 0 : -1] = L'\0';

    // Only the MT2.exe next to this proxy counts: another program that loads this folder's DLLs is left alone.
    return _wcsicmp(exe_path, paths->game_dir) == 0;
}
