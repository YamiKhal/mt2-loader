#ifndef PROXY_PATHS_H
#define PROXY_PATHS_H

#include <stdbool.h>
#include <windows.h>

#define PATH_CAPACITY 1024

typedef struct ProxyPaths {
    wchar_t exe_path[PATH_CAPACITY];
    wchar_t game_dir[PATH_CAPACITY];
    wchar_t loader_dir[PATH_CAPACITY];
} ProxyPaths;

bool paths_find(HMODULE proxy_module, ProxyPaths* paths);
bool paths_join(wchar_t* result, const wchar_t* folder, const wchar_t* name);
bool paths_exists(const wchar_t* path);
bool paths_is_folder(const wchar_t* path);
bool paths_host_is_game(const ProxyPaths* paths);

#endif
