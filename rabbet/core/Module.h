#pragma once

#include <stdexcept>
#include <string_view>
#include <vector>

namespace rb {

class Runtime;

class ModuleError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Module {
public:
    Module() = default;
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    Module(Module&&) = delete;
    Module& operator=(Module&&) = delete;
    virtual ~Module() = default;

    [[nodiscard]] virtual std::string_view name() const = 0;
    [[nodiscard]] virtual std::vector<std::string_view> dependencies() const { return {}; }
    virtual void configure(Runtime& runtime) = 0;
};

} // namespace rb
