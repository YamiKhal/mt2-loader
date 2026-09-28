// The game's reflection in miniature, so plugins find fields by name, singletons, factories and data-file enums the
// way they do in the real game: a property object per saved field that knows the field's offset (at +0x38), and type
// information for every class. Built with RTTI, unlike fake_classes.cpp.
#include <cstddef>
#include <cstdio>
#include <string>

#include "fake_records.h"

class vsPropertyBase {
public:
    vsPropertyBase(const char* property_name, std::size_t field_offset) : name(property_name), offset(field_offset) {}

    virtual ~vsPropertyBase() = default;

    virtual const char* Describe() const {
        return name;
    }

    const char* name;
    char unused[0x28] = {};
    std::size_t offset;
};

static_assert(sizeof(vsPropertyBase) == 0x40, "the offset must be at +0x38, as in the game");

template<class Value, class Owner>
class vsProperty : public vsPropertyBase {
public:
    using vsPropertyBase::vsPropertyBase;

    const char* Describe() const override {
        return "a value";
    }
};

template<class Value, class Owner>
class vsPropertyObject : public vsPropertyBase {
public:
    using vsPropertyBase::vsPropertyBase;

    const char* Describe() const override {
        return "an object";
    }
};

template<class Value, class Owner>
class vsPropertyObjectPointer : public vsPropertyBase {
public:
    using vsPropertyBase::vsPropertyBase;

    const char* Describe() const override {
        return "a pointer";
    }
};

template<class T>
class vsObjectArray {
public:
    virtual ~vsObjectArray() = default;

    T** items = nullptr;
    int count = 0;
};

// As the engine lays out a weak link: its class, the id it loads by, then a weak pointer (its class, the object, and
// the proxy it shares with every link to the object, whose object is nullptr once the object is gone).
struct vsWeakPointerProxy {
    void* object;
    int links;
};

template<class T>
class vsWeakObjectLink {
public:
    virtual ~vsWeakObjectLink() = default;

    char loading[0x18] = {};
    void* pointer_class = nullptr;
    T* object = nullptr;
    vsWeakPointerProxy* proxy = nullptr;
};

template<class T>
class vsSingleton {
public:
    static T* s_instance;
};

template<class T>
class vsRTTIFactory {
public:
    __attribute__((noipa)) void* Create() const {
        return new T();
    }
};

class FakeProp {
public:
    virtual ~FakeProp() = default;

    std::string name = "Fakey";

    static vsProperty<std::string, FakeProp> s_nameProperty;
};

class FakeToon : public FakeProp {
public:
    int level = 12;

    static vsProperty<int, FakeToon> s_levelProperty;
};

static_assert(sizeof(vsWeakObjectLink<FakeToon>) == 0x38, "as the game's vsWeakObjectLink");

class FakeBadge {
public:
    virtual ~FakeBadge() = default;

    int stars = 5;
};

class FakeSubscriber {
public:
    virtual ~FakeSubscriber() = default;

    int spend_other = 0;
    float happiness = 0.5f;
    FakeToon* main = nullptr;
    vsWeakObjectLink<FakeToon> favorite;
    FakeBadge badge;

    static vsProperty<int, FakeSubscriber> s_spend_otherProperty;
    static vsProperty<float, FakeSubscriber> s_happinessProperty;
    // Made at startup, like the game's pointer properties: only the pointer to it has a name.
    static vsPropertyBase* s_main_property;
    static vsPropertyObject<vsWeakObjectLink<FakeToon>, FakeSubscriber> s_favoriteProperty;
    static vsPropertyObject<FakeBadge, FakeSubscriber> s_badgeProperty;
};

class FakeSubscriberManager : public vsSingleton<FakeSubscriberManager> {
public:
    virtual ~FakeSubscriberManager() = default;

    __attribute__((noipa)) int Total() const;

    vsObjectArray<FakeSubscriber> subscriber;

    static vsPropertyObject<vsObjectArray<FakeSubscriber>, FakeSubscriberManager> s_subscriberProperty;
};

extern "C" int fake_boards_destroyed;
int fake_boards_destroyed = 0;

class FakeBoard {
public:
    virtual ~FakeBoard() {
        fake_boards_destroyed++;
    }

    int entries = 7;
};

template class vsRTTIFactory<FakeBoard>;

template<>
__attribute__((used)) FakeSubscriberManager* vsSingleton<FakeSubscriberManager>::s_instance = nullptr;

vsProperty<std::string, FakeProp> FakeProp::s_nameProperty{ "name", offsetof(FakeProp, name) };
vsProperty<int, FakeToon> FakeToon::s_levelProperty{ "level", offsetof(FakeToon, level) };
vsProperty<int, FakeSubscriber> FakeSubscriber::s_spend_otherProperty{ "spend_other", offsetof(FakeSubscriber, spend_other) };
vsProperty<float, FakeSubscriber> FakeSubscriber::s_happinessProperty{ "happiness", offsetof(FakeSubscriber, happiness) };
vsPropertyBase* FakeSubscriber::s_main_property = new vsPropertyObjectPointer<FakeToon, FakeSubscriber>{ "main", offsetof(FakeSubscriber, main) };
vsPropertyObject<vsWeakObjectLink<FakeToon>, FakeSubscriber> FakeSubscriber::s_favoriteProperty{ "favorite", offsetof(FakeSubscriber, favorite) };
vsPropertyObject<FakeBadge, FakeSubscriber> FakeSubscriber::s_badgeProperty{ "badge", offsetof(FakeSubscriber, badge) };
vsPropertyObject<vsObjectArray<FakeSubscriber>, FakeSubscriberManager> FakeSubscriberManager::s_subscriberProperty{
    "subscriber", offsetof(FakeSubscriberManager, subscriber) };

// Plugins replace it with a sum they make from the fields.
int FakeSubscriberManager::Total() const {
    return -1;
}

namespace FakeLeaderboard {

enum Type {
    level,
    gold,
    none,
};

}

class vsObjectContext;

template<class T>
bool ConvertFromString(T* value, const std::string& text);

template<class T>
bool LoadFromRecord(vsRecord* record, T* value, vsObjectContext* context);

template<>
__attribute__((noipa)) bool ConvertFromString<FakeLeaderboard::Type>(FakeLeaderboard::Type* value, const std::string& text) {
    if (text == "level" || text == "gold") {
        *value = text == "level" ? FakeLeaderboard::level : FakeLeaderboard::gold;

        return true;
    }

    return false;
}

// Compares the words itself, as the game's does, rather than calling ConvertFromString.
template<>
__attribute__((noipa)) bool LoadFromRecord<FakeLeaderboard::Type>(vsRecord* record, FakeLeaderboard::Type* value, vsObjectContext*) {
    const std::string& text = record->GetToken(0).AsString();

    if (text == "level" || text == "gold") {
        *value = text == "level" ? FakeLeaderboard::level : FakeLeaderboard::gold;

        return true;
    }

    return false;
}

static const char* const type_words[] = { "level", "gold", "none" };

class vsSaveObjectContext;
class vsNullObject;

template<class T>
std::string ConvertToString(const T& value);

template<class T>
bool WriteToRecord(vsRecord* record, const T& value, vsSaveObjectContext* context);

// A value the game doesn't know becomes an empty word, as in the game.
template<>
__attribute__((noipa)) std::string ConvertToString<FakeLeaderboard::Type>(const FakeLeaderboard::Type& value) {
    return value >= FakeLeaderboard::level && value <= FakeLeaderboard::none ? type_words[value] : "";
}

template<>
__attribute__((noipa)) bool WriteToRecord<FakeLeaderboard::Type>(vsRecord* record, const FakeLeaderboard::Type& value, vsSaveObjectContext*) {
    record->SetTokenCount(1);
    record->tokens[0].SetString(value >= FakeLeaderboard::level && value <= FakeLeaderboard::none ? type_words[value] : "");

    return true;
}

class FakeEntry {
public:
    int rank = 0;
    FakeLeaderboard::Type type = FakeLeaderboard::none;
};

// A saved field of the enum type that converts the word itself, as the game's release demands do.
template<>
class vsProperty<FakeLeaderboard::Type, FakeEntry> : public vsPropertyBase {
public:
    using vsPropertyBase::vsPropertyBase;

    bool Load(vsNullObject* object, vsRecord* record, vsObjectContext* context) const;
    bool Save(const vsNullObject* object, vsRecord* record, vsSaveObjectContext* context) const;
};

__attribute__((noipa)) bool vsProperty<FakeLeaderboard::Type, FakeEntry>::Load(vsNullObject* object, vsRecord* record, vsObjectContext*) const {
    auto* type = reinterpret_cast<FakeLeaderboard::Type*>(reinterpret_cast<char*>(object) + offset);
    const std::string& text = record->GetToken(0).AsString();

    for (int value = FakeLeaderboard::level; value <= FakeLeaderboard::none; value++) {
        if (text == type_words[value]) {
            *type = static_cast<FakeLeaderboard::Type>(value);

            return true;
        }
    }

    return false;
}

__attribute__((noipa)) bool vsProperty<FakeLeaderboard::Type, FakeEntry>::Save(const vsNullObject* object, vsRecord* record, vsSaveObjectContext*) const {
    auto type = *reinterpret_cast<const FakeLeaderboard::Type*>(reinterpret_cast<const char*>(object) + offset);

    record->SetTokenCount(1);
    record->tokens[0].SetString(type >= FakeLeaderboard::level && type <= FakeLeaderboard::none ? type_words[type] : "");

    return true;
}

static vsProperty<FakeLeaderboard::Type, FakeEntry> entry_type_property{ "type", offsetof(FakeEntry, type) };

static std::string saved_entry_word(int value) {
    FakeEntry entry;
    vsRecord record;
    entry.type = static_cast<FakeLeaderboard::Type>(value);

    entry_type_property.Save(reinterpret_cast<const vsNullObject*>(&entry), &record, nullptr);

    return record.GetToken(0).AsString();
}

static int loaded_entry_value(const char* word) {
    FakeEntry entry;
    vsRecord record;
    record.SetTokenCount(1);
    record.tokens[0].text = word;

    bool loaded = entry_type_property.Load(reinterpret_cast<vsNullObject*>(&entry), &record, nullptr);

    return loaded ? static_cast<int>(entry.type) : -1;
}

static std::string written_word(int value) {
    vsRecord record;
    WriteToRecord(&record, static_cast<FakeLeaderboard::Type>(value), nullptr);

    return record.GetToken(0).AsString();
}

static int load_type(const char* word) {
    vsRecord record;
    record.SetTokenCount(1);
    record.tokens[0].text = word;
    FakeLeaderboard::Type type = FakeLeaderboard::none;

    return LoadFromRecord(&record, &type, nullptr) ? static_cast<int>(type) : -1;
}

static int convert_type(const char* word) {
    FakeLeaderboard::Type type = FakeLeaderboard::none;

    return ConvertFromString(&type, std::string(word)) ? static_cast<int>(type) : -1;
}

extern "C" void fake_reflection_results(FILE* file) {
    FakeToon toon;
    FakeSubscriber first;
    FakeSubscriber second;
    FakeSubscriber* subscribers[] = { &first, &second };
    FakeSubscriberManager manager;
    vsWeakPointerProxy toon_proxy{ &toon, 1 };
    vsWeakPointerProxy gone_proxy{ nullptr, 1 };

    first.spend_other = 30;
    first.main = &toon;
    first.favorite.object = &toon;
    first.favorite.proxy = &toon_proxy;
    second.spend_other = 12;
    second.favorite.object = &toon;
    second.favorite.proxy = &gone_proxy;
    manager.subscriber.items = subscribers;
    manager.subscriber.count = 2;
    vsSingleton<FakeSubscriberManager>::s_instance = &manager;

    fprintf(file, "total=%d\n", manager.Total());
    vsSingleton<FakeSubscriberManager>::s_instance = nullptr;

    fprintf(file, "toon_name=%s\n", toon.name.c_str());
    fprintf(file, "boards_destroyed=%d\n", fake_boards_destroyed);
    fprintf(file, "load_level=%d\n", load_type("level"));
    fprintf(file, "load_spenders=%d\n", load_type("spenders"));
    fprintf(file, "load_unknown=%d\n", load_type("unknown"));
    fprintf(file, "convert_spenders=%d\n", convert_type("spenders"));
    fprintf(file, "word_of_spenders=%s\n", ConvertToString(static_cast<FakeLeaderboard::Type>(100)).c_str());
    fprintf(file, "word_of_gold=%s\n", ConvertToString(FakeLeaderboard::gold).c_str());
    fprintf(file, "written_spenders=%s\n", written_word(100).c_str());
    fprintf(file, "saved_field_spenders=%s\n", saved_entry_word(100).c_str());
    fprintf(file, "loaded_field_spenders=%d\n", loaded_entry_value("spenders"));
    fprintf(file, "loaded_field_unknown=%d\n", loaded_entry_value("unknown"));
}
