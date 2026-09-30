#ifndef MT2SDK_COMMANDS_H
#define MT2SDK_COMMANDS_H

#include <stdbool.h>
#include <wchar.h>

int command_new(const wchar_t* folder, const wchar_t* name, const wchar_t* exe, const wchar_t* mappings_folder);
int command_find(const wchar_t* exe, int term_count, wchar_t** terms, int raw, int cpp);
int command_symbols(const wchar_t* exe, const wchar_t* output);
int command_check(const wchar_t* mod_folder, const wchar_t* exe);
int command_build(const wchar_t* exe);
int command_dis(const wchar_t* exe, const wchar_t* function);
int command_uses(const wchar_t* exe, const wchar_t* function);
int command_refs(const wchar_t* exe, const wchar_t* name);
int command_text(const wchar_t* exe, int term_count, wchar_t** terms);
int command_vtable(const wchar_t* exe, const wchar_t* class_name);
int command_class(const wchar_t* exe, const wchar_t* class_name, const wchar_t* mappings_folder);
int command_enum(const wchar_t* exe, const wchar_t* name);
int command_program(const wchar_t* exe, const wchar_t* output);
int command_headers(const wchar_t* exe, const wchar_t* output, const wchar_t* mappings_folder);
int command_setup(const wchar_t* exe, const wchar_t* mappings_folder, const wchar_t* ghidra_folder);
int command_log(const wchar_t* exe);
int command_diff(const wchar_t* exe, const wchar_t* old_exe, const wchar_t* mappings_folder);
int command_reference(const wchar_t* exe, const wchar_t* folder, const wchar_t* mappings_folder);
int command_mappings_check(const wchar_t* exe, const wchar_t* folder);
int command_mappings_json(const wchar_t* exe, const wchar_t* folder, const wchar_t* output);
int command_mappings_import(const wchar_t* exe, const wchar_t* found_path, const wchar_t* folder);
int command_stats(const wchar_t* workspace);
int command_snapshot(const wchar_t* action, const wchar_t* workspace, const wchar_t* exe, const wchar_t* ghidra, const wchar_t* mappings);
int command_mappings_cards(const wchar_t* workspace, const wchar_t* only_class);
int command_mappings_name(const wchar_t* exe, const wchar_t* workspace, const wchar_t* folder, const wchar_t* ghidra,
    const wchar_t* written_class, const wchar_t* written_offset, const wchar_t* written_name, const wchar_t* written_type);
bool is_library_class(const char* owner);
int command_mappings_todo(const wchar_t* workspace, const wchar_t* only_class);
int command_mappings_pull(const wchar_t* exe, const wchar_t* workspace, const wchar_t* mappings_folder, const wchar_t* ghidra_folder);
int command_decompile(const wchar_t* exe, const wchar_t* folder, const wchar_t* ghidra_folder, const wchar_t* mappings_folder,
    bool everything, const wchar_t* only_file);

#endif
