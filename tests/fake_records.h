// The game's records, as the fake game's data files and saves use them: a label, then tokens. Laid out as in the
// game where the loader reads them directly (the token count at +0x40). Defined in fake_saves.cpp.
#ifndef FAKE_RECORDS_H
#define FAKE_RECORDS_H

#include <string>
#include <vector>

class vsToken {
public:
    int type = 0;
    std::string text;

    void SetString(const std::string& value);
    const std::string& AsString() const;
};

// A text file's records keep their children; a saved game's stream only counts them.
class vsRecord {
public:
    vsToken label;
    std::vector<vsToken> tokens;
    int token_count = 0;
    int child_count = 0;
    std::vector<vsRecord*> children;

    vsRecord();
    ~vsRecord();

    const vsToken& GetToken(int index) const;
    void SetTokenCount(int count);
    void SetLabel(const std::string& text);
    void AddChild(vsRecord* child);
};

#endif
