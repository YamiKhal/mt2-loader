#include <stdio.h>
#include <windows.h>

#include "../src/core/symbols.h"

static void show(const char* name) {
    SymbolMatch match = symbols_find(name);

    switch (match.result) {
        case SYMBOL_FOUND:
            printf("found     %s -> rva %08x\n", name, match.rva);
            break;

        case SYMBOL_AMBIGUOUS:
            printf("ambiguous %s -> %d: %s\n", name, match.candidate_count, match.candidates);
            break;

        default:
            printf("missing   %s\n", name);
            break;
    }
}

int wmain(int argc, wchar_t** argv) {
    char problem[200];

    if (argc < 2) {
        return 2;
    }

    ULONGLONG started = GetTickCount64();

    if (!symbols_load(argv[1], problem, sizeof problem)) {
        printf("load failed: %s\n", problem);

        return 1;
    }

    printf("symbols %zu, load %llu ms\n", symbols_count(), GetTickCount64() - started);
    started = GetTickCount64();
    symbols_prepare_readable_names();
    printf("readable names %llu ms\n", GetTickCount64() - started);

    for (int index = 2; index < argc; index++) {
        char name[1024];
        WideCharToMultiByte(CP_UTF8, 0, argv[index], -1, name, sizeof name, NULL, NULL);
        show(name);
    }

    return 0;
}
