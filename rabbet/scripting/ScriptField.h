#pragma once

#include <string>

namespace rb {

// One script-exposed, inspector-editable value. A script declares its tunables in a
// `fields` table; the engine mirrors each scalar here so it serializes with the
// component and is written back into the script environment before on_start/on_update.
struct ScriptField {
    enum class Type { Number, Boolean, String };

    std::string name;
    Type type = Type::Number;
    double number = 0.0;
    bool boolean = false;
    std::string text;
};

} // namespace rb
