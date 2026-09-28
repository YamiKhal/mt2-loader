#ifndef MT2LOADER_HOST_H
#define MT2LOADER_HOST_H

#include <stdint.h>
#include <windows.h>

#define MT2LOADER_HOST_ABI 1

typedef void (*Mt2LoaderLogFunction)(const char* format, ...);

typedef struct Mt2LoaderHost {
    uint32_t size;
    uint32_t abi;
    const char* proxy_version;
    const wchar_t* game_dir;
    const wchar_t* loader_dir;
    const wchar_t* exe_path;
    HMODULE exe_module;
    Mt2LoaderLogFunction log;
} Mt2LoaderHost;

typedef int (*Mt2LoaderStartFunction)(const Mt2LoaderHost* host);

enum Mt2LoaderStartResult {
    MT2LOADER_STARTED = 0,
    MT2LOADER_HOST_MISMATCH = 1,
};

#endif
