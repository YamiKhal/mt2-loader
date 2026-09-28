#include "symbols.h"

#include <ctype.h>
#include <libiberty/demangle.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define COFF_ENTRY_SIZE 18
#define CLASS_EXTERNAL 2
#define CLASS_STATIC 3
#define CLASS_LABEL 6
#define MAX_SECTIONS 96
#define MAX_CANDIDATES_SHOWN 4

typedef struct Symbol {
    uint32_t rva;
    uint32_t name;
} Symbol;

typedef enum KeyKind {
    KEY_FULL,
    KEY_WITHOUT_PARAMETERS,
} KeyKind;

typedef struct KeyEntry {
    uint64_t hash;
    uint32_t symbol;
    uint32_t kind;
} KeyEntry;

typedef struct TextBuffer {
    char* text;
    size_t size;
    size_t length;
    bool truncated;
} TextBuffer;

typedef struct Replacement {
    const char* from;
    const char* to;
} Replacement;

static const Replacement KEY_REPLACEMENTS[] = {
    { "std::__cxx11::basic_string<char,std::char_traits<char>,std::allocator<char>>", "std::string" },
    { "std::__cxx11::", "std::" },
    { "[abi:cxx11]", "" },
};

// [abi:cxx11] marks functions that return a std::string; it says nothing a modder needs.
static const Replacement DISPLAY_REPLACEMENTS[] = {
    { "std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >", "std::string" },
    { "std::__cxx11::", "std::" },
    { "[abi:cxx11]", "" },
};

// Words a readable name can have that its raw name spells differently, or not at all.
static const char* const UNMANGLED_WORDS[] = {
    "std", "string", "basic_string", "cxx11", "abi", "const", "volatile", "unsigned", "signed", "char", "bool", "int",
    "short", "long", "float", "double", "void", "operator", "anonymous", "namespace", "clone", "part", "isra", "constprop",
    "cold", "non", "virtual", "thunk", "to", "typeinfo", "for", "vtable", "name", "guard", "variable",
};

static Symbol* symbols;
static size_t symbol_count;
static char* names;
static uint32_t* by_raw_name;

static KeyEntry* keys;
static size_t key_count;
static SRWLOCK keys_lock = SRWLOCK_INIT;
static bool keys_built;


static const char* name_of(size_t index) {
    return names + symbols[index].name;
}

static uint8_t* read_whole_file(const wchar_t* path, size_t* size) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (file == INVALID_HANDLE_VALUE) {
        return NULL;
    }

    LARGE_INTEGER file_size;
    uint8_t* bytes = NULL;
    size_t total = 0;

    if (GetFileSizeEx(file, &file_size) && file_size.QuadPart > 0 && file_size.QuadPart < 0x40000000) {
        bytes = malloc((size_t)file_size.QuadPart);
    }

    while (bytes != NULL && total < (size_t)file_size.QuadPart) {
        DWORD read = 0;
        DWORD wanted = (DWORD)((size_t)file_size.QuadPart - total);

        if (!ReadFile(file, bytes + total, wanted, &read, NULL) || read == 0) {
            free(bytes);
            bytes = NULL;
        }

        total += read;
    }

    CloseHandle(file);
    *size = total;

    return bytes;
}


static void append_text(const char* piece, size_t length, void* opaque) {
    TextBuffer* buffer = opaque;

    if (buffer->length + length + 1 > buffer->size) {
        buffer->truncated = true;

        return;
    }

    memcpy(buffer->text + buffer->length, piece, length);
    buffer->length += length;
    buffer->text[buffer->length] = '\0';
}

// Readable C++ name with its parameters, as c++filt shows it. Names that aren't C++ stay as they are.
static void demangle(const char* raw, char* out, size_t size) {
    TextBuffer buffer = { out, size, 0, false };
    out[0] = '\0';

    bool is_cpp = strncmp(raw, "_Z", 2) == 0;

    if (!is_cpp || !cplus_demangle_v3_callback(raw, DMGL_PARAMS | DMGL_ANSI, append_text, &buffer) || buffer.truncated) {
        snprintf(out, size, "%s", raw);
    }
}

static void replace_all(char* text, const Replacement* replacements, size_t replacement_count) {
    for (size_t index = 0; index < replacement_count; index++) {
        const char* from = replacements[index].from;
        const char* to = replacements[index].to;
        size_t from_length = strlen(from);
        size_t to_length = strlen(to);
        char* found = strstr(text, from);

        // Every replacement is shorter than what it replaces, so the text only ever shrinks.
        while (found != NULL && to_length <= from_length) {
            memmove(found + to_length, found + from_length, strlen(found + from_length) + 1);
            memcpy(found, to, to_length);
            found = strstr(found + to_length, from);
        }
    }
}

static void remove_spaces(char* text) {
    char* write = text;

    for (const char* read = text; *read != '\0'; read++) {
        if (*read != ' ') {
            *write++ = *read;
        }
    }

    *write = '\0';
}

static void make_key(char* text) {
    remove_spaces(text);
    replace_all(text, KEY_REPLACEMENTS, sizeof KEY_REPLACEMENTS / sizeof KEY_REPLACEMENTS[0]);
}

static bool only_qualifiers(const char* text) {
    static const char* qualifiers[] = { "const", "volatile", "&&", "&" };

    while (*text != '\0') {
        bool matched = false;

        for (size_t index = 0; index < sizeof qualifiers / sizeof qualifiers[0] && !matched; index++) {
            size_t length = strlen(qualifiers[index]);

            if (strncmp(text, qualifiers[index], length) == 0) {
                text += length;
                matched = true;
            }
        }

        if (!matched) {
            return false;
        }
    }

    return true;
}

// "Foo::Bar(int,bool)const" -> "Foo::Bar". Returns false when the key has no parameter list.
static bool strip_parameters(char* key) {
    char* last_close = strrchr(key, ')');

    if (last_close == NULL || !only_qualifiers(last_close + 1)) {
        return false;
    }

    int depth = 0;

    for (char* position = last_close; position >= key; position--) {
        depth += *position == ')' ? 1 : 0;
        depth -= *position == '(' ? 1 : 0;

        if (depth == 0) {
            *position = '\0';

            return position > key;
        }
    }

    return false;
}

// "bool LoadFromRecord<Type>(vsRecord*, Type*)" -> "LoadFromRecord<Type>(vsRecord*, Type*)". Only template functions
// have their return type in the name, and a modder writes a function's name without it. Takes a readable name, before
// its spaces are removed.
static void strip_return_type(char* text) {
    if (strstr(text, "operator") != NULL) {
        return;
    }

    // Where the name ends: at its parameter list, or at the end when written without one.
    char* last_close = strrchr(text, ')');
    char* open = last_close == NULL ? text + strlen(text) : NULL;
    int depth = 0;

    for (char* position = last_close; position != NULL && position >= text && open == NULL; position--) {
        depth += *position == ')' ? 1 : 0;
        depth -= *position == '(' ? 1 : 0;
        open = depth == 0 ? position : NULL;
    }

    if (open == NULL || open == text || open[-1] != '>') {
        return;
    }

    char* last_space = NULL;
    depth = 0;

    for (char* position = text; position < open; position++) {
        depth += *position == '<' || *position == '(' ? 1 : 0;
        depth -= *position == '>' || *position == ')' ? 1 : 0;

        if (depth < 0) {
            return;
        }

        if (depth == 0 && *position == ' ') {
            last_space = position;
        }
    }

    // "non-virtual thunk to …" and the like name something other than the function.
    size_t before = last_space != NULL ? (size_t)(last_space - text) : 0;
    bool is_description = (before >= 3 && strncmp(last_space - 3, " to", 3) == 0) || (before >= 4 && strncmp(last_space - 4, " for", 4) == 0)
        || (before == 2 && strncmp(text, "to", 2) == 0) || (before == 3 && strncmp(text, "for", 3) == 0);

    if (last_space != NULL && !is_description) {
        memmove(text, last_space + 1, strlen(last_space + 1) + 1);
    }
}

static uint64_t hash_text(const char* text) {
    uint64_t hash = 14695981039346656037ull;

    for (const unsigned char* character = (const unsigned char*)text; *character != '\0'; character++) {
        hash = (hash ^ *character) * 1099511628211ull;
    }

    return hash;
}

static void key_of(size_t index, KeyKind kind, char* key, size_t size) {
    demangle(name_of(index), key, size);
    strip_return_type(key);
    make_key(key);

    if (kind == KEY_WITHOUT_PARAMETERS && !strip_parameters(key)) {
        key[0] = '\0';
    }
}


static int compare_symbols_by_rva(const void* left, const void* right) {
    const Symbol* a = left;
    const Symbol* b = right;

    return a->rva < b->rva ? -1 : a->rva > b->rva;
}

static int compare_by_raw_name(const void* left, const void* right) {
    return strcmp(name_of(*(const uint32_t*)left), name_of(*(const uint32_t*)right));
}

static int compare_keys(const void* left, const void* right) {
    const KeyEntry* a = left;
    const KeyEntry* b = right;

    return a->hash < b->hash ? -1 : a->hash > b->hash;
}

static bool append_name(char** pool, size_t* pool_length, size_t* pool_size, const char* name, size_t length, uint32_t* offset) {
    if (*pool_length + length + 1 > *pool_size) {
        size_t new_size = (*pool_size + length + 1) * 2;
        char* grown = realloc(*pool, new_size);

        if (grown == NULL) {
            return false;
        }

        *pool = grown;
        *pool_size = new_size;
    }

    memcpy(*pool + *pool_length, name, length);
    (*pool)[*pool_length + length] = '\0';
    *offset = (uint32_t)*pool_length;
    *pool_length += length + 1;

    return true;
}

static bool parse_symbols(const uint8_t* file, size_t file_size, char* problem, size_t problem_size) {
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)file;

    if (file_size < sizeof *dos || dos->e_magic != IMAGE_DOS_SIGNATURE || (size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > file_size) {
        snprintf(problem, problem_size, "MT2.exe isn't a Windows program");

        return false;
    }

    const IMAGE_NT_HEADERS64* nt = (const IMAGE_NT_HEADERS64*)(file + dos->e_lfanew);
    const IMAGE_FILE_HEADER* header = &nt->FileHeader;
    const IMAGE_SECTION_HEADER* sections = (const IMAGE_SECTION_HEADER*)((const uint8_t*)&nt->OptionalHeader + header->SizeOfOptionalHeader);
    size_t table = header->PointerToSymbolTable;
    size_t entry_count = header->NumberOfSymbols;
    size_t strings = table + entry_count * COFF_ENTRY_SIZE;

    if (table == 0 || entry_count == 0 || strings + 4 > file_size || header->NumberOfSections > MAX_SECTIONS) {
        snprintf(problem, problem_size, "MT2.exe has no symbol table, so game functions can't be found by name");

        return false;
    }

    size_t strings_size = *(const uint32_t*)(file + strings);

    if (strings + strings_size > file_size) {
        snprintf(problem, problem_size, "MT2.exe's symbol table is cut short");

        return false;
    }

    symbols = malloc(entry_count * sizeof *symbols);
    size_t pool_size = entry_count * 48;
    size_t pool_length = 0;
    names = malloc(pool_size);

    for (size_t index = 0; symbols != NULL && names != NULL && index < entry_count; index++) {
        const uint8_t* entry = file + table + index * COFF_ENTRY_SIZE;
        uint32_t value = *(const uint32_t*)(entry + 8);
        int16_t section = *(const int16_t*)(entry + 12);
        uint8_t storage_class = entry[16];
        uint8_t aux_count = entry[17];
        const char* name = (const char*)entry;
        size_t length = strnlen(name, 8);

        if (*(const uint32_t*)entry == 0) {
            size_t offset = *(const uint32_t*)(entry + 4);
            name = offset < strings_size ? (const char*)file + strings + offset : "";
            length = strnlen(name, strings_size - (offset < strings_size ? offset : strings_size));
        }

        index += aux_count;

        bool is_kept_class = storage_class == CLASS_EXTERNAL || storage_class == CLASS_STATIC || storage_class == CLASS_LABEL;

        if (section <= 0 || section > header->NumberOfSections || !is_kept_class || length == 0 || name[0] == '.') {
            continue;
        }

        Symbol* symbol = &symbols[symbol_count];
        symbol->rva = sections[section - 1].VirtualAddress + value;

        if (!append_name(&names, &pool_length, &pool_size, name, length, &symbol->name)) {
            break;
        }

        symbol_count++;
    }

    if (symbols == NULL || names == NULL || symbol_count == 0) {
        snprintf(problem, problem_size, "MT2.exe's symbols couldn't be read (out of memory, or none)");

        return false;
    }

    qsort(symbols, symbol_count, sizeof *symbols, compare_symbols_by_rva);
    by_raw_name = malloc(symbol_count * sizeof *by_raw_name);

    for (size_t index = 0; by_raw_name != NULL && index < symbol_count; index++) {
        by_raw_name[index] = (uint32_t)index;
    }

    if (by_raw_name != NULL) {
        qsort(by_raw_name, symbol_count, sizeof *by_raw_name, compare_by_raw_name);
    }

    return by_raw_name != NULL;
}


bool symbols_load(const wchar_t* exe_path, char* problem, size_t problem_size) {
    if (symbol_count > 0) {
        return true;
    }

    size_t file_size = 0;
    uint8_t* file = read_whole_file(exe_path, &file_size);

    if (file == NULL) {
        snprintf(problem, problem_size, "MT2.exe couldn't be read (error %lu)", GetLastError());

        return false;
    }

    bool loaded = parse_symbols(file, file_size, problem, problem_size);
    free(file);

    return loaded;
}

bool symbols_loaded(void) {
    return symbol_count > 0;
}

size_t symbols_count(void) {
    return symbol_count;
}


static void build_keys(void) {
    keys = malloc(symbol_count * 2 * sizeof *keys);

    if (keys == NULL) {
        return;
    }

    char key[SYMBOL_NAME_CAPACITY];

    for (size_t index = 0; index < symbol_count; index++) {
        demangle(name_of(index), key, sizeof key);
        strip_return_type(key);
        make_key(key);
        keys[key_count++] = (KeyEntry){ hash_text(key), (uint32_t)index, KEY_FULL };

        if (strip_parameters(key)) {
            keys[key_count++] = (KeyEntry){ hash_text(key), (uint32_t)index, KEY_WITHOUT_PARAMETERS };
        }
    }

    qsort(keys, key_count, sizeof *keys, compare_keys);
}

void symbols_prepare_readable_names(void) {
    AcquireSRWLockExclusive(&keys_lock);

    if (!keys_built) {
        build_keys();
        keys_built = true;
    }

    ReleaseSRWLockExclusive(&keys_lock);
}

bool symbols_readable_names_ready(void) {
    AcquireSRWLockShared(&keys_lock);

    bool ready = keys_built;

    ReleaseSRWLockShared(&keys_lock);

    return ready;
}

static bool already_counted(const SymbolMatch* match, uint32_t rva) {
    int known = match->candidate_count < SYMBOL_MAX_DISTINCT ? match->candidate_count : SYMBOL_MAX_DISTINCT;

    for (int index = 0; index < known; index++) {
        if (match->distinct_rvas[index] == rva) {
            return true;
        }
    }

    return false;
}

// Aliases (a constructor's two names, say) share one address: they count as one function.
static void add_candidate(SymbolMatch* match, size_t index) {
    uint32_t rva = symbols[index].rva;

    if (already_counted(match, rva)) {
        return;
    }

    if (match->candidate_count < SYMBOL_MAX_DISTINCT) {
        match->distinct_rvas[match->candidate_count] = rva;
    }

    if (match->candidate_count < MAX_CANDIDATES_SHOWN) {
        char readable[SYMBOL_NAME_CAPACITY];
        size_t used = strlen(match->candidates);

        symbols_readable_name_of(index, readable, sizeof readable);
        snprintf(match->candidates + used, sizeof match->candidates - used, "%s%s", used > 0 ? " | " : "", readable);
    }

    match->candidate_count++;
    match->rva = match->distinct_rvas[0];
    match->result = match->candidate_count == 1 ? SYMBOL_FOUND : SYMBOL_AMBIGUOUS;
}

static void find_raw(const char* name, SymbolMatch* match) {
    size_t low = 0;
    size_t high = symbol_count;

    while (low < high) {
        size_t middle = (low + high) / 2;

        if (strcmp(name_of(by_raw_name[middle]), name) < 0) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }

    for (size_t index = low; index < symbol_count && strcmp(name_of(by_raw_name[index]), name) == 0; index++) {
        add_candidate(match, by_raw_name[index]);
    }
}

static void find_by_key(const char* key, KeyKind kind, SymbolMatch* match) {
    uint64_t hash = hash_text(key);
    size_t low = 0;
    size_t high = key_count;

    while (low < high) {
        size_t middle = (low + high) / 2;

        if (keys[middle].hash < hash) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }

    char candidate[SYMBOL_NAME_CAPACITY];

    for (size_t index = low; index < key_count && keys[index].hash == hash; index++) {
        if (keys[index].kind != kind) {
            continue;
        }

        // The hash only narrows it down; the key itself must match.
        key_of(keys[index].symbol, kind, candidate, sizeof candidate);

        if (strcmp(candidate, key) == 0) {
            add_candidate(match, keys[index].symbol);
        }
    }
}

SymbolMatch symbols_find(const char* name) {
    SymbolMatch match = { .result = SYMBOL_MISSING };

    if (symbol_count == 0) {
        match.result = SYMBOL_TABLE_MISSING;

        return match;
    }

    if (name == NULL || name[0] == '\0' || strlen(name) >= SYMBOL_NAME_CAPACITY) {
        return match;
    }

    find_raw(name, &match);

    if (match.candidate_count > 0) {
        return match;
    }

    symbols_prepare_readable_names();

    char key[SYMBOL_NAME_CAPACITY];
    snprintf(key, sizeof key, "%s", name);
    strip_return_type(key);
    make_key(key);

    find_by_key(key, KEY_FULL, &match);

    if (match.candidate_count == 0) {
        find_by_key(key, KEY_WITHOUT_PARAMETERS, &match);
    }

    return match;
}


// Several names can share an address; linker markers like __data_start__ are the least useful of them.
static int name_quality(const char* name) {
    if (strncmp(name, "_Z", 2) == 0) {
        return 3;
    }

    if (name[0] != '_') {
        return 2;
    }

    return name[1] == '_' ? 0 : 1;
}

static bool index_at(uint32_t rva, size_t* index) {
    if (symbol_count == 0 || rva < symbols[0].rva) {
        return false;
    }

    size_t low = 0;
    size_t high = symbol_count;

    while (high - low > 1) {
        size_t middle = (low + high) / 2;

        if (symbols[middle].rva <= rva) {
            low = middle;
        } else {
            high = middle;
        }
    }

    *index = low;

    return true;
}

static size_t best_name_at(size_t last) {
    size_t best = last;

    for (size_t index = last; index > 0 && symbols[index - 1].rva == symbols[last].rva; index--) {
        if (name_quality(name_of(index - 1)) > name_quality(name_of(best))) {
            best = index - 1;
        }
    }

    return best;
}

bool symbols_name_at(uint32_t rva, char* name, size_t name_size, uint32_t* offset) {
    size_t low = 0;

    if (!index_at(rva, &low)) {
        return false;
    }

    size_t best = best_name_at(low);

    symbols_readable_name_of(best, name, name_size);
    *offset = rva - symbols[best].rva;

    return true;
}

bool symbols_extent_at(uint32_t rva, uint32_t* start, uint32_t* size) {
    size_t index = 0;

    if (!index_at(rva, &index)) {
        return false;
    }

    size_t next = index + 1;

    // Several names can share one address (a constructor's two names): the extent ends at the next address.
    while (next < symbol_count && symbols[next].rva == symbols[index].rva) {
        next++;
    }

    *start = symbols[index].rva;
    *size = next < symbol_count ? symbols[next].rva - symbols[index].rva : 0;

    return true;
}

static bool is_unmangled_word(const char* word, size_t length) {
    for (size_t index = 0; index < sizeof UNMANGLED_WORDS / sizeof UNMANGLED_WORDS[0]; index++) {
        if (strlen(UNMANGLED_WORDS[index]) == length && strncmp(UNMANGLED_WORDS[index], word, length) == 0) {
            return true;
        }
    }

    return false;
}

// A raw name spells each name it's made of as <length><name> (mmoToon is 7mmoToon), so the longest such name in
// text rules out most raw names without demangling them.
static bool longest_mangled_word(const char* text, char* word, size_t word_size) {
    const char* longest = NULL;
    size_t longest_length = 0;

    for (const char* at = text; *at != '\0';) {
        if (!isalpha((unsigned char)*at) && *at != '_') {
            at++;
            continue;
        }

        const char* start = at;

        while (isalnum((unsigned char)*at) || *at == '_') {
            at++;
        }

        size_t length = (size_t)(at - start);
        bool whole = start != text && *at != '\0';

        if (whole && length > longest_length && !is_unmangled_word(start, length)) {
            longest = start;
            longest_length = length;
        }
    }

    if (longest == NULL) {
        return false;
    }

    snprintf(word, word_size, "%zu%.*s", longest_length, (int)longest_length, longest);

    return true;
}

void symbols_each_containing(const char* text, void (*each)(size_t index, const char* readable, void* opaque), void* opaque) {
    if (text == NULL || text[0] == '\0') {
        return;
    }

    char word[SYMBOL_NAME_CAPACITY];
    bool narrowed = longest_mangled_word(text, word, sizeof word);
    char readable[SYMBOL_NAME_CAPACITY];

    for (size_t index = 0; index < symbol_count; index++) {
        if (narrowed && strstr(name_of(index), word) == NULL) {
            continue;
        }

        symbols_readable_name_of(index, readable, sizeof readable);

        if (strstr(readable, text) != NULL) {
            each(index, readable, opaque);
        }
    }
}

uint32_t symbols_rva_of(size_t index) {
    return symbols[index].rva;
}

const char* symbols_raw_name_of(size_t index) {
    return name_of(index);
}

void symbols_readable_name_of(size_t index, char* name, size_t name_size) {
    demangle(name_of(index), name, name_size);
    replace_all(name, DISPLAY_REPLACEMENTS, sizeof DISPLAY_REPLACEMENTS / sizeof DISPLAY_REPLACEMENTS[0]);
}
