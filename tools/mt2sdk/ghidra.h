#ifndef MT2SDK_GHIDRA_H
#define MT2SDK_GHIDRA_H

#include <stdbool.h>
#include <wchar.h>

#define GHIDRA_PATH_CAPACITY 1024
#define GHIDRA_MAX_ARGUMENTS 48

typedef struct Ghidra {
    wchar_t folder[GHIDRA_PATH_CAPACITY];
    wchar_t java_home[GHIDRA_PATH_CAPACITY];
} Ghidra;

// Ghidra from --ghidra, GHIDRA_INSTALL_DIR, or the usual places (%LOCALAPPDATA%\Programs\Ghidra, C:\ghidra*), and a
// JDK 21 or newer for it (JAVA_HOME, or the usual install folders). Prints what's missing and where to get it.
bool ghidra_find(const wchar_t* given, Ghidra* ghidra);

// The same without printing: what's found is filled in (an empty folder when not), for mt2sdk setup.
void ghidra_look(const wchar_t* given, Ghidra* ghidra, int* java_major);

// Runs Ghidra's analyzeHeadless with the arguments and waits. Everything it prints goes to log; only the kit's own
// script lines, errors and warnings about scripts are shown.
bool ghidra_run(const Ghidra* ghidra, const wchar_t* const* arguments, int argument_count, const wchar_t* log);

#endif
