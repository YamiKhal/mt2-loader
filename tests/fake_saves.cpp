// The game's saving and loading in miniature, so the loader's values in saved games are tested the way the game
// writes and reads them: objects write a record with their fields as children (vsObject<T, Base>::
// SaveValuesToStream: ContainsObject, BeginChildren, vsRTTI::SaveStream, EndChildren), and loading skips any
// field it doesn't know. Text files (a saved game's rules.vrt) are records with a child per field (vsRTTI::Save),
// read back one child at a time (vsRTTI::Load). Every function the loader looks for keeps its call, as in the game.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "fake_records.h"

#include <cstddef>

static_assert(sizeof(vsToken) == 0x28, "a token is 0x28 bytes, as in the game");
static_assert(offsetof(vsRecord, token_count) == 0x40, "the loader reads a record's token count at +0x40, as in the game");

class vsRecordWriter;
class vsRecordReader;
class vsSaveObjectContext;
class vsObjectContext;

__attribute__((noipa)) void vsToken::SetString(const std::string& value) {
    type = 1;
    text = value;
}

__attribute__((noipa)) const std::string& vsToken::AsString() const {
    return text;
}

__attribute__((noipa)) const vsToken& vsRecord::GetToken(int index) const {
    static const vsToken empty;

    return index >= 0 && index < token_count ? tokens[index] : empty;
}

__attribute__((noipa)) void vsRecord::SetTokenCount(int count) {
    tokens.resize(count);
    token_count = count;
}

__attribute__((noipa)) vsRecord::vsRecord() = default;

vsRecord::~vsRecord() {
    for (vsRecord* child : children) {
        delete child;
    }
}

__attribute__((noipa)) void vsRecord::SetLabel(const std::string& text) {
    label.text = text;
}

__attribute__((noipa)) void vsRecord::AddChild(vsRecord* child) {
    children.push_back(child);
    child_count = static_cast<int>(children.size());
}


class vsNullObject {
public:
    vsNullObject() = default;
    virtual ~vsNullObject();

    virtual void SaveToStream(vsRecordWriter*, vsSaveObjectContext*) const {}
    virtual void SaveValuesToStream(vsRecordWriter*, vsSaveObjectContext*) const {}
    virtual void LoadFromStream(vsRecordReader*, vsObjectContext*) {}
};

static volatile int objects_destroyed = 0;

// Out of line and not inlined, as in the game: every object's destructor ends here.
__attribute__((noipa)) vsNullObject::~vsNullObject() {
    objects_destroyed = objects_destroyed + 1;
}


class vsSaveObjectContext {
public:
    ~vsSaveObjectContext();

    // Objects others link to are saved with a linkId.
    bool ContainsObject(const vsNullObject* object, int* id);

    const vsNullObject* linked = nullptr;
};

static volatile int saves_finished = 0;

__attribute__((noipa)) vsSaveObjectContext::~vsSaveObjectContext() {
    saves_finished = saves_finished + 1;
    linked = nullptr;
}

__attribute__((noipa)) bool vsSaveObjectContext::ContainsObject(const vsNullObject* object, int* id) {
    *id = object == linked ? 7 : 0;

    return object == linked;
}

static volatile int objects_loaded = 0;

class vsObjectContext {
public:
    ~vsObjectContext();

    int objects = 0;
};

__attribute__((noipa)) vsObjectContext::~vsObjectContext() {
    objects_loaded = objects_loaded + objects;
    objects = -1;
}


// Records one after another, each followed by its children; a level checks it got as many children as it said.
class vsRecordWriter {
public:
    void Next();
    void SetLabel(const std::string& label);
    void SetInt(int value);
    void SetTokenCount(int count);
    vsToken& GetToken(int index);
    void BeginChildren(int count);
    void EndChildren();

    std::vector<std::unique_ptr<vsRecord>> records;
    bool broken = false;

private:
    struct Level {
        int expected;
        int written;
    };

    vsRecord* current = nullptr;
    std::vector<Level> levels;
};

__attribute__((noipa)) void vsRecordWriter::Next() {
    records.push_back(std::make_unique<vsRecord>());
    current = records.back().get();

    if (!levels.empty()) {
        levels.back().written++;
    }
}

__attribute__((noipa)) void vsRecordWriter::SetLabel(const std::string& label) {
    current->label.text = label;
}

__attribute__((noipa)) void vsRecordWriter::SetInt(int value) {
    SetTokenCount(1);
    current->tokens[0].text = std::to_string(value);
}

__attribute__((noipa)) void vsRecordWriter::SetTokenCount(int count) {
    current->SetTokenCount(count);
}

__attribute__((noipa)) vsToken& vsRecordWriter::GetToken(int index) {
    return current->tokens[index];
}

__attribute__((noipa)) void vsRecordWriter::BeginChildren(int count) {
    current->child_count = count;
    levels.push_back({ count, 0 });
}

__attribute__((noipa)) void vsRecordWriter::EndChildren() {
    broken = broken || levels.back().written != levels.back().expected;
    levels.pop_back();
}


// Next moves to the next record at this level, past the children of the one before if nobody read them.
class vsRecordReader {
public:
    explicit vsRecordReader(const std::vector<std::unique_ptr<vsRecord>>& read) : records(read) {}

    void Next();
    vsRecord* Get();
    const vsRecord* Get() const;
    int BeginChildren();
    void EndChildren();

private:
    std::size_t end_of(std::size_t index) const {
        std::size_t next = index + 1;

        for (int child = 0; child < records[index]->child_count; child++) {
            next = end_of(next);
        }

        return next;
    }

    const std::vector<std::unique_ptr<vsRecord>>& records;
    std::size_t current = 0;
    std::size_t next_index = 0;
    std::vector<std::size_t> resume_at;
};

__attribute__((noipa)) void vsRecordReader::Next() {
    current = next_index;
    next_index = end_of(current);
}

__attribute__((noipa)) vsRecord* vsRecordReader::Get() {
    return records[current].get();
}

__attribute__((noipa)) const vsRecord* vsRecordReader::Get() const {
    return records[current].get();
}

__attribute__((noipa)) int vsRecordReader::BeginChildren() {
    resume_at.push_back(next_index);
    next_index = current + 1;

    return records[current]->child_count;
}

__attribute__((noipa)) void vsRecordReader::EndChildren() {
    next_index = resume_at.back();
    resume_at.pop_back();
}


struct FakeProperty {
    const char* name;
    void (*save)(const vsNullObject* object, vsRecordWriter* writer, vsSaveObjectContext* context);
    void (*load)(vsNullObject* object, vsRecordReader* reader, vsObjectContext* context);
    // In a text file: the field's own child record.
    void (*save_record)(const vsNullObject* object, vsRecord* record) = nullptr;
    void (*load_record)(vsNullObject* object, const vsRecord* record) = nullptr;
};

class vsRTTI {
public:
    explicit vsRTTI(std::vector<FakeProperty> list) : properties(std::move(list)) {}

    int Count() const {
        return static_cast<int>(properties.size());
    }

    bool SaveStream(const vsNullObject* object, vsRecordWriter* writer, vsSaveObjectContext* context) const;
    // False for a field this class doesn't have: the caller goes on with the next one.
    bool LoadStream(vsNullObject* object, vsRecordReader* reader, vsObjectContext* context) const;
    bool Save(const vsNullObject* object, vsRecord* record, vsSaveObjectContext* context) const;
    bool Load(vsNullObject* object, vsRecord* record, vsObjectContext* context) const;

private:
    std::vector<FakeProperty> properties;
};

__attribute__((noipa)) bool vsRTTI::SaveStream(const vsNullObject* object, vsRecordWriter* writer, vsSaveObjectContext* context) const {
    for (const FakeProperty& property : properties) {
        writer->Next();
        writer->SetLabel(property.name);
        property.save(object, writer, context);
    }

    return true;
}

__attribute__((noipa)) bool vsRTTI::LoadStream(vsNullObject* object, vsRecordReader* reader, vsObjectContext* context) const {
    const std::string& label = reader->Get()->label.AsString();

    for (const FakeProperty& property : properties) {
        if (label == property.name) {
            property.load(object, reader, context);

            return true;
        }
    }

    return false;
}


__attribute__((noipa)) bool vsRTTI::Save(const vsNullObject* object, vsRecord* record, vsSaveObjectContext*) const {
    for (const FakeProperty& property : properties) {
        vsRecord* child = new vsRecord();
        child->SetLabel(property.name);
        property.save_record(object, child);
        record->AddChild(child);
    }

    return true;
}

__attribute__((noipa)) bool vsRTTI::Load(vsNullObject* object, vsRecord* record, vsObjectContext*) const {
    for (const FakeProperty& property : properties) {
        if (record->label.AsString() == property.name) {
            property.load_record(object, record);

            return true;
        }
    }

    return false;
}


template<class T, class Base>
class vsObject : public Base {
public:
    void SaveToStream(vsRecordWriter* writer, vsSaveObjectContext* context) const override;
    void SaveValuesToStream(vsRecordWriter* writer, vsSaveObjectContext* context) const override;
    void LoadFromStream(vsRecordReader* reader, vsObjectContext* context) override;
};

template<class T, class Base>
__attribute__((noipa)) void vsObject<T, Base>::SaveToStream(vsRecordWriter* writer, vsSaveObjectContext* context) const {
    writer->SetLabel(T::class_name);
    this->SaveValuesToStream(writer, context);
}

template<class T, class Base>
__attribute__((noipa)) void vsObject<T, Base>::SaveValuesToStream(vsRecordWriter* writer, vsSaveObjectContext* context) const {
    int id = 0;

    if (context->ContainsObject(this, &id)) {
        writer->BeginChildren(T::s_RTTI.Count() + 1);
        writer->Next();
        writer->SetLabel("linkId");
        writer->SetInt(id);
    } else {
        writer->BeginChildren(T::s_RTTI.Count());
    }

    T::s_RTTI.SaveStream(this, writer, context);
    writer->EndChildren();
}

template<class T, class Base>
__attribute__((noipa)) void vsObject<T, Base>::LoadFromStream(vsRecordReader* reader, vsObjectContext* context) {
    int count = reader->BeginChildren();

    for (int index = 0; index < count; index++) {
        reader->Next();
        T::s_RTTI.LoadStream(this, reader, context);
    }

    reader->EndChildren();
    context->objects++;
}


static int int_of(vsRecordReader* reader) {
    return std::stoi(reader->Get()->GetToken(0).AsString());
}

class FakeNpc : public vsObject<FakeNpc, vsNullObject> {
public:
    // Plugins hook these: a new quest giver, and one whose saved game has loaded.
    void Generate(int kind);
    void PostResolve();
    void ShowMarker(const char* marker);

    int kind = 0;
    std::string marker = "none";

    static constexpr const char* class_name = "FakeNpc";
    static vsRTTI s_RTTI;
};

__attribute__((noipa)) void FakeNpc::Generate(int new_kind) {
    kind = new_kind;
}

static FakeNpc* new_npc(int kind) {
    FakeNpc* npc = new FakeNpc();
    npc->Generate(kind);

    return npc;
}

__attribute__((noipa)) void FakeNpc::PostResolve() {
    marker = "unchanged";
}

__attribute__((noipa)) void FakeNpc::ShowMarker(const char* shown) {
    marker = shown;
}

vsRTTI FakeNpc::s_RTTI{ {
    { "kind",
        [](const vsNullObject* object, vsRecordWriter* writer, vsSaveObjectContext*) { writer->SetInt(static_cast<const FakeNpc*>(object)->kind); },
        [](vsNullObject* object, vsRecordReader* reader, vsObjectContext*) { static_cast<FakeNpc*>(object)->kind = int_of(reader); } },
} };

class mmoGameState : public vsObject<mmoGameState, vsNullObject> {
public:
    ~mmoGameState() override {
        for (FakeNpc* npc : npcs) {
            delete npc;
        }
    }

    int gold = 0;
    std::vector<FakeNpc*> npcs;

    static constexpr const char* class_name = "mmoGameState";
    static vsRTTI s_RTTI;
};

// The game's list of NPCs saves each one with vsObject<mmoNPC, ...>::SaveValuesToStream inlined, as here.
static void save_npcs(const vsNullObject* object, vsRecordWriter* writer, vsSaveObjectContext* context) {
    const std::vector<FakeNpc*>& npcs = static_cast<const mmoGameState*>(object)->npcs;

    writer->BeginChildren(static_cast<int>(npcs.size()));

    for (FakeNpc* npc : npcs) {
        int id = 0;

        writer->Next();
        writer->SetLabel(FakeNpc::class_name);

        if (context->ContainsObject(npc, &id)) {
            writer->BeginChildren(FakeNpc::s_RTTI.Count() + 1);
            writer->Next();
            writer->SetLabel("linkId");
            writer->SetInt(id);
        } else {
            writer->BeginChildren(FakeNpc::s_RTTI.Count());
        }

        FakeNpc::s_RTTI.SaveStream(npc, writer, context);
        writer->EndChildren();
    }

    writer->EndChildren();
}

static void load_npcs(vsNullObject* object, vsRecordReader* reader, vsObjectContext* context) {
    std::vector<FakeNpc*>& npcs = static_cast<mmoGameState*>(object)->npcs;
    int count = reader->BeginChildren();

    for (int index = 0; index < count; index++) {
        reader->Next();
        npcs.push_back(new FakeNpc());
        npcs.back()->LoadFromStream(reader, context);
    }

    reader->EndChildren();
}

vsRTTI mmoGameState::s_RTTI{ {
    { "gold",
        [](const vsNullObject* object, vsRecordWriter* writer, vsSaveObjectContext*) { writer->SetInt(static_cast<const mmoGameState*>(object)->gold); },
        [](vsNullObject* object, vsRecordReader* reader, vsObjectContext*) { static_cast<mmoGameState*>(object)->gold = int_of(reader); } },
    { "npcs", save_npcs, load_npcs },
} };

// A saved game's rules, which the game keeps in a text file beside the save.
class FakeRules : public vsObject<FakeRules, vsNullObject> {
public:
    // Plugins hook these: rules set on the New Game window, and rules read back from the file.
    void Setup();
    void Loaded();
    void Show(const char* shown);

    bool disable_money = false;
    std::string shown = "none";

    static constexpr const char* class_name = "FakeRules";
    static vsRTTI s_RTTI;
};

__attribute__((noipa)) void FakeRules::Setup() {
    disable_money = true;
}

__attribute__((noipa)) void FakeRules::Loaded() {
    shown = "unchanged";
}

__attribute__((noipa)) void FakeRules::Show(const char* text) {
    shown = text;
}

vsRTTI FakeRules::s_RTTI{ {
    { "disable_money",
        [](const vsNullObject* object, vsRecordWriter* writer, vsSaveObjectContext*) { writer->SetInt(static_cast<const FakeRules*>(object)->disable_money); },
        [](vsNullObject* object, vsRecordReader* reader, vsObjectContext*) { static_cast<FakeRules*>(object)->disable_money = int_of(reader) != 0; },
        [](const vsNullObject* object, vsRecord* record) {
            record->SetTokenCount(1);
            record->tokens[0].SetString(static_cast<const FakeRules*>(object)->disable_money ? "true" : "false");
        },
        [](vsNullObject* object, const vsRecord* record) { static_cast<FakeRules*>(object)->disable_money = record->GetToken(0).AsString() == "true"; } },
} };

template class vsObject<FakeNpc, vsNullObject>;
template class vsObject<mmoGameState, vsNullObject>;
template class vsObject<FakeRules, vsNullObject>;

template<class T>
class vsSingleton {
public:
    static T* s_instance;
};

template<>
__attribute__((used)) mmoGameState* vsSingleton<mmoGameState>::s_instance = nullptr;


static int count_children_labeled(const vsRecord& record, const char* label) {
    int count = 0;

    for (const vsRecord* child : record.children) {
        count += child->label.text == label ? 1 : 0;
    }

    return count;
}

// Rules written to a text file, closed, and read back into new rules, one child record per field.
static void save_and_load_rules(FILE* file) {
    FakeRules* rules = new FakeRules();
    rules->Setup();

    vsRecord text_file;
    text_file.SetLabel(FakeRules::class_name);
    {
        vsSaveObjectContext context;
        FakeRules::s_RTTI.Save(rules, &text_file, &context);
    }

    std::fprintf(file, "rules_fields=%d\n", static_cast<int>(text_file.children.size()));
    std::fprintf(file, "rules_plugin_fields=%d\n", count_children_labeled(text_file, "mt2loader"));
    delete rules;

    FakeRules* loaded = new FakeRules();
    int skipped = 0;
    {
        vsObjectContext context;

        for (vsRecord* child : text_file.children) {
            skipped += FakeRules::s_RTTI.Load(loaded, child, &context) ? 0 : 1;
        }
    }

    loaded->Loaded();
    std::fprintf(file, "rules_skipped=%d\n", skipped);
    std::fprintf(file, "rules_loaded=%d %s\n", loaded->disable_money ? 1 : 0, loaded->shown.c_str());
    delete loaded;
}

static int count_labeled(const vsRecordWriter& writer, const char* label) {
    int count = 0;

    for (const std::unique_ptr<vsRecord>& record : writer.records) {
        count += record->label.text == label ? 1 : 0;
    }

    return count;
}

// A game with two quest givers is saved, closed, and loaded again; plugins keep values on the quest givers and on
// the game as a whole.
extern "C" void fake_saves_results(FILE* file) {
    mmoGameState* state = new mmoGameState();
    vsSingleton<mmoGameState>::s_instance = state;
    state->gold = 250;
    state->npcs.push_back(new_npc(1));
    state->npcs.push_back(new_npc(2));

    vsRecordWriter writer;
    {
        vsSaveObjectContext context;
        context.linked = state->npcs[0];
        writer.Next();
        state->SaveToStream(&writer, &context);
    }

    std::fprintf(file, "save_intact=%d\n", writer.broken ? 0 : 1);
    std::fprintf(file, "save_plugin_fields=%d\n", count_labeled(writer, "mt2loader"));
    std::fprintf(file, "save_link_ids=%d\n", count_labeled(writer, "linkId"));

    delete state;
    vsSingleton<mmoGameState>::s_instance = nullptr;

    // Often at the address of one just deleted: it mustn't inherit that one's values.
    FakeNpc* fresh = new_npc(0);
    fresh->PostResolve();
    std::fprintf(file, "fresh_marker=%s\n", fresh->marker.c_str());
    delete fresh;

    vsRecordReader reader(writer.records);
    state = new mmoGameState();
    vsSingleton<mmoGameState>::s_instance = state;
    {
        vsObjectContext context;
        reader.Next();
        state->LoadFromStream(&reader, &context);
    }

    std::fprintf(file, "loaded_gold=%d\n", state->gold);
    std::fprintf(file, "loaded_npcs=%zu\n", state->npcs.size());

    for (std::size_t index = 0; index < state->npcs.size(); index++) {
        FakeNpc* npc = state->npcs[index];
        npc->PostResolve();
        std::fprintf(file, "loaded_npc%zu=%d %s\n", index, npc->kind, npc->marker.c_str());
    }

    delete state;
    vsSingleton<mmoGameState>::s_instance = nullptr;

    save_and_load_rules(file);
}
