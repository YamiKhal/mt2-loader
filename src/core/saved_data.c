#include "saved_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "../../sdk/include/mt2loader.h"
#include "../common/pe_image.h"
#include "code.h"
#include "hooks.h"
#include "memory.h"
#include "symbols.h"

#define OWNER "mt2loader"
#define FIELD_NAME "mt2loader"
#define BUCKET_COUNT 4096
#define MAX_SITES 16384
#define MAX_SAVE_FUNCTIONS 4096
#define MAX_WALK_STEPS 4096
#define MAX_WALK_BRANCHES 256
#define MAX_OBJECT_BEGINS 8
#define CALL_SIZE 5
#define OPCODE_CALL 0xe8
#define OPCODE_JMP 0xe9
#define OPCODE_JMP_SHORT 0xeb
#define OPCODE_INDIRECT 0xff
#define MAX_SAVING_DEPTH 256
#define VTABLE_SLOTS_CHECKED 48
#define RECORD_TOKEN_COUNT_OFFSET 0x40
#define RECORD_SIZE 0x78
#define SAVE_TEMPLATE_PREFIX "_ZNK8vsObjectI"
#define SAVE_TO_STREAM_SUFFIX "E12SaveToStreamEP14vsRecordWriterP19vsSaveObjectContext"
#define SAVE_VALUES_SUFFIX "E18SaveValuesToStreamEP14vsRecordWriterP19vsSaveObjectContext"

typedef struct Entry {
    char* key;
    char* value;
} Entry;

typedef struct ObjectData {
    const void* object;
    Entry* entries;
    int count;
    int capacity;
    struct ObjectData* next;
} ObjectData;

// A call the loader checked, by where it returns to. start is where saving that object starts: the return of
// its ContainsObject call (for that call itself too).
typedef struct Site {
    const uint8_t* returns_to;
    const uint8_t* start;
} Site;

typedef struct Sites {
    Site items[MAX_SITES];
    int count;
} Sites;

// What an object's SaveToStream promised the game it would write: the extra field, with these texts.
typedef struct Promise {
    const void* object;
    void* writer;
    char** texts;
    int text_count;
} Promise;

typedef bool (*ContainsObjectFunction)(void* context, const void* object, int* id);
typedef void (*BeginChildrenFunction)(void* writer, int count);
typedef uintptr_t (*RttiSaveFunction)(const void* rtti, const void* object, void* writer, void* context);
typedef uintptr_t (*RttiLoadFunction)(const void* rtti, void* object, void* reader, void* context);
typedef void (*DestructorFunction)(void* object);
typedef void (*WriterNextFunction)(void* writer);
typedef void (*WriterSetLabelFunction)(void* writer, const GameString* label);
typedef void (*WriterSetTokenCountFunction)(void* writer, int count);
typedef void* (*WriterGetTokenFunction)(void* writer, int index);
typedef void (*TokenSetStringFunction)(void* token, const GameString* text);
typedef void* (*ReaderGetFunction)(void* reader);
typedef const GameString* (*TokenAsStringFunction)(const void* token);
typedef const void* (*RecordGetTokenFunction)(const void* record, int index);
typedef uintptr_t (*RttiRecordSaveFunction)(const void* rtti, const void* object, void* record, void* context);
typedef uintptr_t (*RttiRecordLoadFunction)(const void* rtti, void* object, const void* record, void* context);
typedef void* (*AllocateFunction)(size_t size);
typedef void (*RecordMakeFunction)(void* record);
typedef void (*RecordSetLabelFunction)(void* record, const GameString* label);
typedef void (*RecordSetTokenCountFunction)(void* record, int count);
typedef void (*RecordAddChildFunction)(void* record, void* child);

static SavedDataLog log_line;
static bool started;

static ObjectData* buckets[BUCKET_COUNT];
static volatile LONG object_count;
static SRWLOCK store_lock = SRWLOCK_INIT;

static Sites contains_sites;
static Sites begin_sites;
static const uint8_t* save_functions[MAX_SAVE_FUNCTIONS];
static int save_function_count;
static void** root_pointer;

static ContainsObjectFunction original_contains_object;
static BeginChildrenFunction original_begin_children;
static RttiSaveFunction original_rtti_save;
static RttiLoadFunction original_rtti_load;
static DestructorFunction original_destructor;
static DestructorFunction original_save_context_end;
static DestructorFunction original_load_context_end;
static RttiRecordSaveFunction original_rtti_record_save;
static RttiRecordLoadFunction original_rtti_record_load;

static WriterNextFunction writer_next;
static WriterSetLabelFunction writer_set_label;
static WriterSetTokenCountFunction writer_set_token_count;
static WriterGetTokenFunction writer_get_token;
static TokenSetStringFunction token_set_string;
static ReaderGetFunction reader_get;
static TokenAsStringFunction token_as_string;
static RecordGetTokenFunction record_get_token;
static AllocateFunction game_allocate;
static RecordMakeFunction record_make;
static RecordSetLabelFunction record_set_label;
static RecordSetTokenCountFunction record_set_token_count;
static RecordAddChildFunction record_add_child;

static _Thread_local const void* pending_object;
static _Thread_local const uint8_t* pending_start;
static _Thread_local Promise promises[MAX_SAVING_DEPTH];
static _Thread_local int promise_count;
static _Thread_local int objects_written;
static _Thread_local int objects_read;


static size_t bucket_of(const void* object) {
    uintptr_t value = (uintptr_t)object;

    return (size_t)(((value >> 4) * 0x9e3779b97f4a7c15ull) >> 52) % BUCKET_COUNT;
}

static ObjectData* find_data(const void* object) {
    for (ObjectData* data = buckets[bucket_of(object)]; data != NULL; data = data->next) {
        if (data->object == object) {
            return data;
        }
    }

    return NULL;
}

static ObjectData* find_or_add_data(const void* object) {
    ObjectData* data = find_data(object);

    if (data != NULL) {
        return data;
    }

    data = calloc(1, sizeof *data);

    if (data == NULL) {
        return NULL;
    }

    size_t bucket = bucket_of(object);
    data->object = object;
    data->next = buckets[bucket];
    buckets[bucket] = data;
    InterlockedIncrement(&object_count);

    return data;
}

static void free_data(ObjectData* data) {
    for (int index = 0; index < data->count; index++) {
        free(data->entries[index].key);
        free(data->entries[index].value);
    }

    free(data->entries);
    free(data);
}

static void remove_data(const void* object) {
    ObjectData** link = &buckets[bucket_of(object)];

    while (*link != NULL && (*link)->object != object) {
        link = &(*link)->next;
    }

    if (*link == NULL) {
        return;
    }

    ObjectData* data = *link;
    *link = data->next;
    free_data(data);
    InterlockedDecrement(&object_count);
}

static int find_entry(const ObjectData* data, const char* key) {
    for (int index = 0; index < data->count; index++) {
        if (strcmp(data->entries[index].key, key) == 0) {
            return index;
        }
    }

    return -1;
}

static bool put_entry(ObjectData* data, const char* key, const char* value) {
    int index = find_entry(data, key);
    char* copy = _strdup(value);

    if (copy == NULL) {
        return false;
    }

    if (index >= 0) {
        free(data->entries[index].value);
        data->entries[index].value = copy;

        return true;
    }

    if (data->count == data->capacity) {
        int capacity = data->capacity == 0 ? 4 : data->capacity * 2;
        Entry* grown = realloc(data->entries, (size_t)capacity * sizeof *grown);

        if (grown == NULL) {
            free(copy);

            return false;
        }

        data->entries = grown;
        data->capacity = capacity;
    }

    data->entries[data->count].key = _strdup(key);
    data->entries[data->count].value = copy;

    if (data->entries[data->count].key == NULL) {
        free(copy);

        return false;
    }

    data->count++;

    return true;
}

static void remove_entry(const void* object, const char* key) {
    ObjectData* data = find_data(object);
    int index = data != NULL ? find_entry(data, key) : -1;

    if (index < 0) {
        return;
    }

    free(data->entries[index].key);
    free(data->entries[index].value);
    data->entries[index] = data->entries[data->count - 1];
    data->count--;

    if (data->count == 0) {
        remove_data(object);
    }
}


static void add_site(Sites* sites, const uint8_t* returns_to, const uint8_t* start) {
    if (sites->count < MAX_SITES) {
        sites->items[sites->count++] = (Site){ returns_to, start };
    }
}

static int compare_sites(const void* left, const void* right) {
    const Site* a = left;
    const Site* b = right;

    if (a->returns_to != b->returns_to) {
        return a->returns_to < b->returns_to ? -1 : 1;
    }

    return a->start < b->start ? -1 : a->start > b->start ? 1 : 0;
}

// Runs for every object the game saves, so the sites are sorted once and searched by halves.
static const Site* site_at(const Sites* sites, const void* returns_to, const uint8_t* start) {
    Site wanted = { returns_to, start };

    return bsearch(&wanted, sites->items, (size_t)sites->count, sizeof wanted, compare_sites);
}

static bool any_site_within(const Sites* sites, const uint8_t* start, uint32_t size) {
    int low = 0;
    int high = sites->count;

    while (low < high) {
        int middle = (low + high) / 2;

        if (sites->items[middle].returns_to < start) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }

    return low < sites->count && sites->items[low].returns_to < start + size;
}


typedef struct SaveCalls {
    const void* contains;
    const void* begin;
    const void* rtti_save;
    const void* unwind;
} SaveCalls;

typedef struct Branch {
    const uint8_t* at;
    bool begun;
} Branch;

// Every way through the code from one ContainsObject call, within its function.
typedef struct Walk {
    const uint8_t* function_start;
    const uint8_t* function_end;
    Branch pending[MAX_WALK_BRANCHES];
    int pending_count;
    Branch seen[MAX_WALK_BRANCHES];
    int seen_count;
    const uint8_t* begins[MAX_OBJECT_BEGINS];
    int begin_count;
    const uint8_t* save_call;
    int steps;
} Walk;

static Walk walk;

static const uint8_t* after_prefixes(const uint8_t* at) {
    while ((*at >= 0x40 && *at <= 0x4f) || *at == 0x66 || *at == 0x2e || *at == 0x3e || *at == 0xf2 || *at == 0xf3) {
        at++;
    }

    return at;
}

static bool follow(const uint8_t* at, bool begun) {
    if (at < walk.function_start || at >= walk.function_end) {
        return false;
    }

    for (int index = 0; index < walk.seen_count; index++) {
        if (walk.seen[index].at == at && walk.seen[index].begun == begun) {
            return true;
        }
    }

    if (walk.seen_count == MAX_WALK_BRANCHES || walk.pending_count == MAX_WALK_BRANCHES) {
        return false;
    }

    walk.seen[walk.seen_count++] = (Branch){ at, begun };
    walk.pending[walk.pending_count++] = (Branch){ at, begun };

    return true;
}

static bool add_begin(const uint8_t* returns_to) {
    for (int index = 0; index < walk.begin_count; index++) {
        if (walk.begins[index] == returns_to) {
            return true;
        }
    }

    if (walk.begin_count == MAX_OBJECT_BEGINS) {
        return false;
    }

    walk.begins[walk.begin_count++] = returns_to;

    return true;
}

// One way through the code, until it reaches vsRTTI::SaveStream or jumps (where it jumps to is walked later).
static bool walk_branch(const SaveCalls* calls, const uint8_t* at, bool begun) {
    while (walk.steps++ < MAX_WALK_STEPS) {
        Instruction instruction;

        if (at < walk.function_start || at >= walk.function_end || !code_decode(at, &instruction) || instruction.length == 0) {
            return false;
        }

        const uint8_t* next = at + instruction.length;
        const void* target = instruction.reference;

        if (instruction.is_return) {
            return false;
        }

        if (instruction.is_call && target == calls->contains) {
            return false;
        } else if (instruction.is_call && target == calls->unwind) {
            return true;
        } else if (instruction.is_call && target == calls->rtti_save) {
            if (!begun || (walk.save_call != NULL && walk.save_call != at)) {
                return false;
            }

            walk.save_call = at;

            return true;
        } else if (instruction.is_call && target == calls->begin) {
            if (begun || !add_begin(next)) {
                return false;
            }

            begun = true;
        } else if (instruction.is_jump) {
            const uint8_t* opcode = after_prefixes(at);

            if (target == NULL || *opcode == OPCODE_INDIRECT || !follow(target, begun)) {
                return false;
            }

            if (*opcode == OPCODE_JMP || *opcode == OPCODE_JMP_SHORT) {
                return true;
            }
        }

        at = next;
    }

    return false;
}

// vsObject<T, Base>::SaveValuesToStream, wherever the game's compiler put it (its own function, or inlined into
// the list that saves the object): ContainsObject, then BeginChildren on one of a few paths (a plain object, one
// with a linkId, ...), then the one vsRTTI::SaveStream that writes the object's fields. Only when every way from
// the ContainsObject call goes through exactly that are its BeginChildren calls trusted to count the object's
// fields, so the extra field can be written right after them.
static bool walk_object_save(const SaveCalls* calls, const uint8_t* contains_return, const uint8_t* function_start, uint32_t function_size) {
    walk.function_start = function_start;
    walk.function_end = function_start + function_size;
    walk.pending_count = 0;
    walk.seen_count = 0;
    walk.begin_count = 0;
    walk.save_call = NULL;
    walk.steps = 0;

    if (!follow(contains_return, false)) {
        return false;
    }

    while (walk.pending_count > 0) {
        Branch branch = walk.pending[--walk.pending_count];

        if (!walk_branch(calls, branch.at, branch.begun)) {
            return false;
        }
    }

    return walk.save_call != NULL && walk.begin_count > 0;
}

static bool starts_instruction(const uint8_t* function_start, const uint8_t* address) {
    const uint8_t* at = function_start;

    while (at < address) {
        size_t length = code_instruction_length(at);

        if (length == 0) {
            return false;
        }

        at += length;
    }

    return at == address;
}

// Every call to ContainsObject in the game's code that starts saving an object's fields.
static int find_object_saves(uint8_t* game_base, const SaveCalls* calls) {
    IMAGE_SECTION_HEADER* text = pe_find_section((HMODULE)game_base, ".text");
    int found = 0;

    if (text == NULL) {
        return 0;
    }

    const uint8_t* code = game_base + text->VirtualAddress;
    size_t code_size = text->Misc.VirtualSize;

    for (size_t offset = 0; offset + CALL_SIZE <= code_size; offset++) {
        int32_t relative = 0;

        if (code[offset] != OPCODE_CALL) {
            continue;
        }

        memcpy(&relative, code + offset + 1, sizeof relative);
        const uint8_t* call = code + offset;
        uint32_t function_rva = 0;
        uint32_t function_size = 0;

        if (call + CALL_SIZE + relative != calls->contains) {
            continue;
        }

        bool in_function = symbols_extent_at((uint32_t)(call - game_base), &function_rva, &function_size) && function_size > 0;

        if (!in_function || !starts_instruction(game_base + function_rva, call)) {
            continue;
        }

        const uint8_t* contains_return = call + CALL_SIZE;

        if (!walk_object_save(calls, contains_return, game_base + function_rva, function_size)) {
            continue;
        }

        add_site(&contains_sites, contains_return, contains_return);

        for (int index = 0; index < walk.begin_count; index++) {
            add_site(&begin_sites, walk.begins[index], contains_return);
        }

        found++;
    }

    return found;
}

static bool ends_with(const char* text, const char* suffix) {
    size_t text_length = strlen(text);
    size_t suffix_length = strlen(suffix);

    return text_length >= suffix_length && strcmp(text + text_length - suffix_length, suffix) == 0;
}

// The vsObject<...> save functions that save their object the checked way. An object whose vtable has one of
// them is one the game saves like that.
static void find_save_functions(uint8_t* game_base) {
    for (size_t index = 0; index < symbols_count() && save_function_count < MAX_SAVE_FUNCTIONS; index++) {
        const char* raw = symbols_raw_name_of(index);
        uint32_t start = 0;
        uint32_t size = 0;

        bool is_save = ends_with(raw, SAVE_TO_STREAM_SUFFIX) || ends_with(raw, SAVE_VALUES_SUFFIX);

        if (strncmp(raw, SAVE_TEMPLATE_PREFIX, strlen(SAVE_TEMPLATE_PREFIX)) != 0 || !is_save) {
            continue;
        }

        if (symbols_extent_at(symbols_rva_of(index), &start, &size) && size > 0 && any_site_within(&contains_sites, game_base + start, size)) {
            save_functions[save_function_count++] = game_base + start;
        }
    }
}

static bool is_save_function(const void* address) {
    for (int index = 0; index < save_function_count; index++) {
        if (save_functions[index] == address) {
            return true;
        }
    }

    return false;
}

// Values are only kept for objects the game saves through a checked vsObject<...> function.
static bool saved_the_checked_way(const void* object) {
    void** vtable = NULL;

    if (!memory_read(object, &vtable, sizeof vtable) || vtable == NULL) {
        return false;
    }

    for (int slot = 0; slot < VTABLE_SLOTS_CHECKED; slot++) {
        void* function = NULL;

        if (memory_read(&vtable[slot], &function, sizeof function) && is_save_function(function)) {
            return true;
        }
    }

    return false;
}


static GameString view_of(const char* text) {
    GameString string;

    string.data = (char*)text;
    string.length = strlen(text);
    string.capacity = string.length;

    return string;
}

// Copies the object's values as texts (key, value, key, value, ...), so they're written as they were when
// the game counted the object's fields, even if a plugin changes them in the meantime.
static char** snapshot_of(const void* object, int* text_count) {
    char** texts = NULL;
    *text_count = 0;

    AcquireSRWLockShared(&store_lock);
    ObjectData* data = find_data(object);

    if (data != NULL && data->count > 0) {
        texts = calloc((size_t)data->count * 2, sizeof *texts);
    }

    for (int index = 0; texts != NULL && index < data->count; index++) {
        texts[index * 2] = _strdup(data->entries[index].key);
        texts[index * 2 + 1] = _strdup(data->entries[index].value);
    }

    if (texts != NULL) {
        *text_count = data->count * 2;
    }

    ReleaseSRWLockShared(&store_lock);

    for (int index = 0; index < *text_count; index++) {
        if (texts[index] == NULL) {
            texts[index] = _strdup("");
        }
    }

    return texts;
}

static void free_texts(char** texts, int text_count) {
    for (int index = 0; index < text_count; index++) {
        free(texts[index]);
    }

    free(texts);
}

// The same calls the game makes for a text field: Next, SetLabel, then the tokens.
static void write_field(void* writer, char** texts, int text_count) {
    GameString label = view_of(FIELD_NAME);

    writer_next(writer);
    writer_set_label(writer, &label);
    writer_set_token_count(writer, text_count);

    for (int index = 0; index < text_count; index++) {
        GameString text = view_of(texts[index] != NULL ? texts[index] : "");
        token_set_string(writer_get_token(writer, index), &text);
    }
}


static bool contains_object_detour(void* context, const void* object, int* id) {
    const void* returns_to = __builtin_return_address(0);
    bool contained = original_contains_object(context, object, id);
    const Site* site = site_at(&contains_sites, returns_to, returns_to);

    pending_object = site != NULL ? object : NULL;
    pending_start = site != NULL ? site->start : NULL;

    return contained;
}

static void begin_children_detour(void* writer, int count) {
    const void* returns_to = __builtin_return_address(0);
    const Site* site = pending_object != NULL && object_count > 0 ? site_at(&begin_sites, returns_to, pending_start) : NULL;

    if (site != NULL && promise_count < MAX_SAVING_DEPTH) {
        int text_count = 0;
        char** texts = snapshot_of(pending_object, &text_count);

        if (texts != NULL) {
            promises[promise_count++] = (Promise){ pending_object, writer, texts, text_count };
            count++;
        }
    }

    pending_object = NULL;
    pending_start = NULL;
    original_begin_children(writer, count);
}

static uintptr_t rtti_save_detour(const void* rtti, const void* object, void* writer, void* context) {
    uintptr_t result = original_rtti_save(rtti, object, writer, context);

    if (promise_count > 0 && promises[promise_count - 1].object == object && promises[promise_count - 1].writer == writer) {
        Promise* promise = &promises[--promise_count];

        write_field(writer, promise->texts, promise->text_count);
        free_texts(promise->texts, promise->text_count);
        objects_written++;
    }

    return result;
}

static void read_field(void* object, const void* record) {
    int token_count = 0;
    memory_read((const uint8_t*)record + RECORD_TOKEN_COUNT_OFFSET, &token_count, sizeof token_count);

    AcquireSRWLockExclusive(&store_lock);
    ObjectData* data = find_or_add_data(object);

    for (int index = 0; data != NULL && index + 1 < token_count; index += 2) {
        const GameString* key = token_as_string(record_get_token(record, index));
        const GameString* value = token_as_string(record_get_token(record, index + 1));

        if (key != NULL && value != NULL && key->length > 0 && key->length < SAVED_KEY_CAPACITY * 2) {
            put_entry(data, key->data, value->data);
        }
    }

    if (data != NULL && data->count == 0) {
        remove_data(object);
    }

    ReleaseSRWLockExclusive(&store_lock);
    objects_read++;
}

// The same child record vsRTTI::Save makes for each field, allocated by the game, which frees it with the record.
static void add_record_field(void* record, char** texts, int text_count) {
    void* child = game_allocate(RECORD_SIZE);

    if (child == NULL) {
        return;
    }

    GameString label = view_of(FIELD_NAME);
    record_make(child);
    record_set_label(child, &label);
    record_set_token_count(child, text_count);

    for (int index = 0; index < text_count; index++) {
        GameString text = view_of(texts[index] != NULL ? texts[index] : "");
        token_set_string((void*)record_get_token(child, index), &text);
    }

    record_add_child(record, child);
}

static uintptr_t rtti_record_save_detour(const void* rtti, const void* object, void* record, void* context) {
    uintptr_t result = original_rtti_record_save(rtti, object, record, context);

    if (object_count == 0) {
        return result;
    }

    int text_count = 0;
    char** texts = snapshot_of(object, &text_count);

    if (texts != NULL) {
        add_record_field(record, texts, text_count);
        free_texts(texts, text_count);
        objects_written++;
    }

    return result;
}

static bool is_loader_field(const void* record) {
    const GameString* label = record != NULL ? token_as_string(record) : NULL;

    return label != NULL && label->length == strlen(FIELD_NAME) && memcmp(label->data, FIELD_NAME, label->length) == 0;
}

// Text files (rules.vrt and the like) load one child record per field, and the game skips one it doesn't know.
static uintptr_t rtti_record_load_detour(const void* rtti, void* object, const void* record, void* context) {
    uintptr_t result = original_rtti_record_load(rtti, object, record, context);

    if ((result & 0xff) == 0 && is_loader_field(record)) {
        read_field(object, record);
    }

    return result;
}

static uintptr_t rtti_load_detour(const void* rtti, void* object, void* reader, void* context) {
    uintptr_t result = original_rtti_load(rtti, object, reader, context);

    if ((result & 0xff) != 0) {
        return result;
    }

    const void* record = reader_get(reader);

    if (is_loader_field(record)) {
        read_field(object, record);
    }

    return result;
}

static void destructor_detour(void* object) {
    if (object_count > 0) {
        AcquireSRWLockExclusive(&store_lock);
        remove_data(object);
        ReleaseSRWLockExclusive(&store_lock);
    }

    original_destructor(object);
}

static void save_context_end_detour(void* context) {
    if (objects_written > 0) {
        log_line("Saved plugin values on %d game objects", objects_written);
    }

    objects_written = 0;

    // A save that stopped half way leaves promises behind.
    while (promise_count > 0) {
        Promise* promise = &promises[--promise_count];
        free_texts(promise->texts, promise->text_count);
    }

    original_save_context_end(context);
}

static void load_context_end_detour(void* context) {
    if (objects_read > 0) {
        log_line("Loaded plugin values on %d game objects", objects_read);
    }

    objects_read = 0;
    original_load_context_end(context);
}


static void* find_function(uint8_t* game_base, const char* name, char* problem, size_t problem_size) {
    SymbolMatch match = symbols_find(name);

    if (match.result != SYMBOL_FOUND) {
        snprintf(problem, problem_size, "the game has no single function called %s", name);

        return NULL;
    }

    return game_base + match.rva;
}

// vsNullObject's own destructor has no name in the game's symbols; its deleting destructor calls it first.
static void* base_destructor(uint8_t* game_base, char* problem, size_t problem_size) {
    uint8_t* deleting = find_function(game_base, "_ZN12vsNullObjectD0Ev", problem, problem_size);

    for (uint32_t offset = 0; deleting != NULL && offset < 64;) {
        Instruction instruction;

        if (!code_decode(deleting + offset, &instruction) || instruction.length == 0) {
            break;
        }

        if (instruction.is_call) {
            return instruction.reference;
        }

        offset += instruction.length;
    }

    snprintf(problem, problem_size, "vsNullObject's destructor wasn't found");

    return NULL;
}

typedef struct Needed {
    const char* name;
    void** address;
} Needed;

static bool find_functions(uint8_t* game_base, char* problem, size_t problem_size) {
    Needed needed[] = {
        { "vsRecordWriter::Next()", (void**)&writer_next },
        { "vsRecordWriter::SetLabel(std::string const&)", (void**)&writer_set_label },
        { "vsRecordWriter::SetTokenCount(int)", (void**)&writer_set_token_count },
        { "vsRecordWriter::GetToken(int)", (void**)&writer_get_token },
        { "vsToken::SetString(std::string const&)", (void**)&token_set_string },
        { "vsRecordReader::Get()", (void**)&reader_get },
        { "vsToken::AsString() const", (void**)&token_as_string },
        { "vsRecord::GetToken(int) const", (void**)&record_get_token },
    };

    for (size_t index = 0; index < sizeof needed / sizeof needed[0]; index++) {
        *needed[index].address = find_function(game_base, needed[index].name, problem, problem_size);

        if (*needed[index].address == NULL) {
            return false;
        }
    }

    SymbolMatch root = symbols_find("vsSingleton<mmoGameState>::s_instance");
    root_pointer = root.result == SYMBOL_FOUND ? (void**)(game_base + root.rva) : NULL;

    return true;
}

// Only for the log lines that count what a save or a load carried; values work without them.
static void hook_counting(uint8_t* game_base, const char* name, void* detour, void** original) {
    char problem[HOOK_PROBLEM_CAPACITY];
    void* target = find_function(game_base, name, problem, sizeof problem);

    if (target == NULL || !hooks_add(OWNER, target, detour, original, problem, sizeof problem)) {
        log_line("Saved games: no count of plugin values in the log (%s)", problem);
    }
}

// Values in the game's text files too (a saved game's rules.vrt): optional, the saved games work without it.
static bool start_text_files(uint8_t* game_base, char* problem, size_t problem_size) {
    Needed needed[] = {
        { "operator new(unsigned long long)", (void**)&game_allocate },
        { "vsRecord::vsRecord()", (void**)&record_make },
        { "vsRecord::SetLabel(std::string const&)", (void**)&record_set_label },
        { "vsRecord::SetTokenCount(int)", (void**)&record_set_token_count },
        { "vsRecord::AddChild(vsRecord*)", (void**)&record_add_child },
    };

    for (size_t index = 0; index < sizeof needed / sizeof needed[0]; index++) {
        *needed[index].address = find_function(game_base, needed[index].name, problem, problem_size);

        if (*needed[index].address == NULL) {
            return false;
        }
    }

    void* save = find_function(game_base, "vsRTTI::Save(vsNullObject const*, vsRecord*, vsSaveObjectContext*) const", problem, problem_size);
    void* load = save != NULL ? find_function(game_base, "vsRTTI::Load(vsNullObject*, vsRecord*, vsObjectContext*) const", problem, problem_size) : NULL;

    if (load == NULL || !hooks_add(OWNER, save, (void*)rtti_record_save_detour, (void**)&original_rtti_record_save, problem, problem_size)) {
        return false;
    }

    if (!hooks_add(OWNER, load, (void*)rtti_record_load_detour, (void**)&original_rtti_record_load, problem, problem_size)) {
        char undo_problem[HOOK_PROBLEM_CAPACITY];
        hooks_remove(OWNER, (void*)rtti_record_save_detour, undo_problem, sizeof undo_problem);

        return false;
    }

    return true;
}

typedef struct HookPlan {
    void* target;
    void* detour;
    void** original;
    const char* name;
} HookPlan;

bool saved_data_start(uint8_t* game_base, size_t game_size, SavedDataLog log, char* problem, size_t problem_size) {
    (void)game_size;
    log_line = log;

    if (started) {
        return true;
    }

    if (!symbols_readable_names_ready()) {
        symbols_prepare_readable_names();
    }

    void* contains = find_function(game_base, "vsSaveObjectContext::ContainsObject(vsNullObject const*, int*)", problem, problem_size);
    void* begin = contains != NULL ? find_function(game_base, "vsRecordWriter::BeginChildren(int)", problem, problem_size) : NULL;
    void* rtti_save = begin != NULL ? find_function(game_base, "vsRTTI::SaveStream(vsNullObject const*, vsRecordWriter*, vsSaveObjectContext*) const", problem, problem_size) : NULL;
    void* rtti_load = rtti_save != NULL ? find_function(game_base, "vsRTTI::LoadStream(vsNullObject*, vsRecordReader*, vsObjectContext*) const", problem, problem_size) : NULL;
    void* destructor = rtti_load != NULL ? base_destructor(game_base, problem, problem_size) : NULL;

    if (destructor == NULL || !find_functions(game_base, problem, problem_size)) {
        return false;
    }

    SymbolMatch unwind = symbols_find("_Unwind_Resume");
    SaveCalls calls = { contains, begin, rtti_save, unwind.result == SYMBOL_FOUND ? game_base + unwind.rva : NULL };

    int object_saves = find_object_saves(game_base, &calls);
    qsort(contains_sites.items, (size_t)contains_sites.count, sizeof(Site), compare_sites);
    qsort(begin_sites.items, (size_t)begin_sites.count, sizeof(Site), compare_sites);
    find_save_functions(game_base);

    if (object_saves == 0 || save_function_count == 0) {
        snprintf(problem, problem_size, "none of the game's code saves objects the way the loader expects");

        return false;
    }

    HookPlan plans[] = {
        { contains, (void*)contains_object_detour, (void**)&original_contains_object, "ContainsObject" },
        { begin, (void*)begin_children_detour, (void**)&original_begin_children, "BeginChildren" },
        { rtti_save, (void*)rtti_save_detour, (void**)&original_rtti_save, "vsRTTI::SaveStream" },
        { rtti_load, (void*)rtti_load_detour, (void**)&original_rtti_load, "vsRTTI::LoadStream" },
        { destructor, (void*)destructor_detour, (void**)&original_destructor, "~vsNullObject" },
    };

    for (size_t index = 0; index < sizeof plans / sizeof plans[0]; index++) {
        char hook_problem[HOOK_PROBLEM_CAPACITY];

        if (!hooks_add(OWNER, plans[index].target, plans[index].detour, plans[index].original, hook_problem, sizeof hook_problem)) {
            for (size_t undo = 0; undo < index; undo++) {
                hooks_remove(OWNER, plans[undo].detour, hook_problem, sizeof hook_problem);
            }

            snprintf(problem, problem_size, "couldn't hook %s: %s", plans[index].name, hook_problem);

            return false;
        }
    }

    hook_counting(game_base, "vsSaveObjectContext::~vsSaveObjectContext()", (void*)save_context_end_detour, (void**)&original_save_context_end);
    hook_counting(game_base, "vsObjectContext::~vsObjectContext()", (void*)load_context_end_detour, (void**)&original_load_context_end);

    char text_problem[HOOK_PROBLEM_CAPACITY];
    bool text_files = start_text_files(game_base, text_problem, sizeof text_problem);

    started = true;
    log_line("Plugin values in saved games: ready (%d places in the game's code save objects, %d kinds of object)", object_saves, save_function_count);

    if (text_files) {
        log_line("Plugin values in the game's text files (like a save's rules.vrt): ready");
    } else {
        log_line("Plugin values in the game's text files: not available (%s)", text_problem);
    }

    return true;
}

bool saved_data_started(void) {
    return started;
}


bool saved_data_set(const void* object, const char* key, const char* value, char* problem, size_t problem_size) {
    if (!started) {
        snprintf(problem, problem_size, "values in saved games aren't available (see the start of the log)");

        return false;
    }

    if (object == NULL) {
        snprintf(problem, problem_size, "there's no game object (null)");

        return false;
    }

    if (value != NULL && strlen(value) >= SAVED_VALUE_CAPACITY) {
        snprintf(problem, problem_size, "the value is longer than %d characters", SAVED_VALUE_CAPACITY - 1);

        return false;
    }

    if (value != NULL && !saved_the_checked_way(object)) {
        snprintf(problem, problem_size, "the game doesn't save this object the usual way, so a value on it wouldn't be kept");

        return false;
    }

    AcquireSRWLockExclusive(&store_lock);
    bool stored = true;

    if (value == NULL) {
        remove_entry(object, key);
    } else {
        ObjectData* data = find_or_add_data(object);
        stored = data != NULL && put_entry(data, key, value);
    }

    ReleaseSRWLockExclusive(&store_lock);

    if (!stored) {
        snprintf(problem, problem_size, "out of memory");
    }

    return stored;
}

bool saved_data_get(const void* object, const char* key, char* value, size_t value_size, size_t* length) {
    bool found = false;

    AcquireSRWLockShared(&store_lock);
    ObjectData* data = object != NULL ? find_data(object) : NULL;
    int index = data != NULL ? find_entry(data, key) : -1;

    if (index >= 0) {
        const char* stored = data->entries[index].value;
        size_t stored_length = strlen(stored);

        if (length != NULL) {
            *length = stored_length;
        }

        if (value != NULL && value_size > stored_length) {
            memcpy(value, stored, stored_length + 1);
        }

        found = true;
    }

    ReleaseSRWLockShared(&store_lock);

    return found;
}

size_t saved_data_keys(const void* object, const char* prefix, char* keys, size_t keys_size) {
    size_t prefix_length = strlen(prefix);
    size_t needed = 1;

    if (keys != NULL && keys_size > 0) {
        keys[0] = '\0';
    }

    AcquireSRWLockShared(&store_lock);
    ObjectData* data = object != NULL ? find_data(object) : NULL;

    for (int index = 0; data != NULL && index < data->count; index++) {
        const char* key = data->entries[index].key;

        if (strncmp(key, prefix, prefix_length) != 0) {
            continue;
        }

        size_t length = strlen(key + prefix_length);

        if (keys != NULL && needed + length + 1 <= keys_size) {
            memcpy(keys + needed - 1, key + prefix_length, length);
            keys[needed - 1 + length] = '\n';
            keys[needed + length] = '\0';
        }

        needed += length + 1;
    }

    ReleaseSRWLockShared(&store_lock);

    return needed;
}

void* saved_data_root(void) {
    void* root = NULL;

    if (root_pointer != NULL) {
        memory_read(root_pointer, &root, sizeof root);
    }

    return root;
}
