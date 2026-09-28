#ifndef MT2SDK_COMMANDS_H
#define MT2SDK_COMMANDS_H

#include <wchar.h>

int command_new(const wchar_t* folder, const wchar_t* name, const wchar_t* exe);
int command_find(const wchar_t* exe, int term_count, wchar_t** terms, int raw, int cpp);
int command_symbols(const wchar_t* exe, const wchar_t* output);
int command_check(const wchar_t* mod_folder, const wchar_t* exe);
int command_build(const wchar_t* exe);

#endif
