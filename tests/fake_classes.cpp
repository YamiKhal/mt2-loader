// C++ code in the fake game, so its symbol table has mangled names and methods for plugins to find and hook.
#include <stdio.h>
#include <string>

struct FakeVector2 {
    float x;
    float y;
};

struct FakeVector3 {
    float x;
    float y;
    float z;
};

class FakeCharacter {
public:
    int weapons = 1;

    __attribute__((noinline)) int EquipWeaponModel(bool show);
    __attribute__((noinline)) int Overloaded(int value);
    __attribute__((noinline)) int Overloaded(float value);
    __attribute__((noinline)) int RigCheck(const std::string& rig);
    __attribute__((noipa)) std::string Title() const;
    __attribute__((noipa)) std::string Greet(const std::string& name) const;
    __attribute__((noipa)) float Scale(float value) const;
    __attribute__((noipa)) FakeVector2 Position2() const;
    __attribute__((noipa)) FakeVector3 Position3(float scale) const;
    __attribute__((noipa)) int Reach(float scale) const;
    __attribute__((noipa)) int Weapons(const std::string& rig) const;
    __attribute__((noipa)) int Limit() const;
    __attribute__((noipa)) const char* Motto() const;
    __attribute__((noipa)) int Sum6(int a, float b, int c, double d, int e, int f) const;
    __attribute__((noipa)) int First() const;
    __attribute__((noipa)) int Second() const;
    __attribute__((noipa)) void Count();
    __attribute__((noipa)) int Doubled(int value) const;
};

int FakeCharacter::EquipWeaponModel(bool show) {
    volatile int result = show ? weapons : 0;

    return result;
}

int FakeCharacter::Overloaded(int value) {
    volatile int result = value;

    return result;
}

int FakeCharacter::Overloaded(float value) {
    volatile int result = (int)value + 1;

    return result;
}

// Out of line, as in the game (std::operator==(std::string const&, char const*)), so the text stays a reference.
__attribute__((noipa)) static bool same_text(const std::string& text, const char* other) {
    return text == other;
}

// A template, as the game's std::operator== is: its readable name has a return type and template arguments.
template<class Text>
__attribute__((noipa)) bool fake_equal(const Text& text, const char* other) {
    return text == other;
}

// Like the game's rig check: a compare against a text, for find_text_reference to find.
int FakeCharacter::RigCheck(const std::string& rig) {
    return same_text(rig, "fake_humanoid") ? weapons : 0;
}

// Returns a std::string by value, longer than 15 characters so it's on the heap: plugins call it with the
// result pointer first and the object second, then free it with free_string.
std::string FakeCharacter::Title() const {
    return "Fake character with " + std::to_string(weapons) + " weapons";
}

// Takes a std::string by reference and returns one: the C++ wrapper's game::String both ways.
std::string FakeCharacter::Greet(const std::string& name) const {
    return "Hello, " + name + "!";
}

float FakeCharacter::Scale(float value) const {
    return value * (float)weapons;
}

// 8 bytes of plain data: GCC returns it in rax, not through a result pointer.
FakeVector2 FakeCharacter::Position2() const {
    return { (float)weapons * 1.5f, 2.5f };
}

// 12 bytes: returned through a result pointer, before the object.
FakeVector3 FakeCharacter::Position3(float scale) const {
    return { scale, scale * 2.0f, (float)weapons };
}

// Calls a function that returns 12 bytes, for a hook on that call.
int FakeCharacter::Reach(float scale) const {
    FakeVector3 point = Position3(scale);

    return (int)(point.x + point.y + point.z);
}

// Two calls to one function, told apart by the text each passes.
int FakeCharacter::Weapons(const std::string& rig) const {
    if (fake_equal(rig, "fake_humanoid")) {
        return 2;
    }

    if (fake_equal(rig, "fake_quadruped")) {
        return 1;
    }

    return 0;
}

// A number written in the code.
int FakeCharacter::Limit() const {
    volatile int limit = 777;

    return limit;
}

const char* FakeCharacter::Motto() const {
    return "fake motto";
}

// Six arguments: two of them floating point, two on the stack.
int FakeCharacter::Sum6(int a, float b, int c, double d, int e, int f) const {
    return (int)((double)a + (double)b + (double)c + d + (double)e + (double)f);
}

int FakeCharacter::First() const {
    return weapons;
}

int FakeCharacter::Second() const {
    return weapons + 1;
}

static volatile int counted = 0;

// Plugins' failing code after these must not run them a second time.
void FakeCharacter::Count() {
    counted = counted + 1;
}

int FakeCharacter::Doubled(int value) const {
    counted = counted + 1;

    return value * 2;
}

extern "C" {
    void fake_reflection_results(FILE* file);
    void fake_saves_results(FILE* file);

    int fake_gold = 5;
    int fake_seen = 0;

    // Writes what the game computed, after plugins had their chance to change it, next to the exe.
    void fake_write_results(const char* path) {
        FakeCharacter character;
        FILE* file = fopen(path, "w");

        if (file == nullptr) {
            return;
        }

        FakeVector2 position2 = character.Position2();
        FakeVector3 position3 = character.Position3(2.0f);

        fprintf(file, "equip=%d\n", character.EquipWeaponModel(true));
        fprintf(file, "overloaded=%d\n", character.Overloaded(7));
        fprintf(file, "gold=%d\n", fake_gold);
        fprintf(file, "rig=%d\n", character.RigCheck("fake_humanoid"));
        fprintf(file, "title=%s\n", character.Title().c_str());
        fprintf(file, "greet=%s\n", character.Greet("Player").c_str());
        fprintf(file, "scale=%g\n", character.Scale(2.0f));
        fprintf(file, "position2=%g,%g\n", position2.x, position2.y);
        fprintf(file, "position3=%g,%g,%g\n", position3.x, position3.y, position3.z);
        fprintf(file, "reach=%d\n", character.Reach(2.0f));
        fprintf(file, "weapons_humanoid=%d\n", character.Weapons("fake_humanoid"));
        fprintf(file, "weapons_dragon=%d\n", character.Weapons("fake_dragon"));
        fprintf(file, "limit=%d\n", character.Limit());
        fprintf(file, "motto=%s\n", character.Motto());
        fprintf(file, "sum6=%d\n", character.Sum6(1, 2.5f, 3, 4.5, 5, 6));
        fprintf(file, "seen=%d\n", fake_seen);
        fprintf(file, "first=%d\n", character.First());
        fprintf(file, "second=%d\n", character.Second());
        character.Count();
        fprintf(file, "counted_once=%d\n", counted);
        fprintf(file, "doubled=%d\n", character.Doubled(21));
        fprintf(file, "counted_twice=%d\n", counted);
        fake_reflection_results(file);
        fake_saves_results(file);
        fclose(file);
    }
}
