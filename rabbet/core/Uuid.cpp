#include "rabbet/core/Uuid.h"

#include <array>
#include <random>

namespace rb {
namespace {

char hexDigit(unsigned value) {
    return static_cast<char>(value < 10 ? '0' + value : 'a' + (value - 10));
}

void appendHex64(std::string& out, std::uint64_t value) {
    for (int shift = 60; shift >= 0; shift -= 4) {
        out.push_back(hexDigit(static_cast<unsigned>((value >> shift) & 0xFu)));
    }
}

} // namespace

Uuid Uuid::generate() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    Uuid id;
    id.high = rng();
    id.low = rng();
    if (!id.valid()) {
        id.low = 1u; // never hand out the null uuid
    }
    return id;
}

std::string Uuid::toString() const {
    std::string out;
    out.reserve(32);
    appendHex64(out, high);
    appendHex64(out, low);
    return out;
}

Uuid Uuid::fromString(std::string_view text) {
    std::array<std::uint8_t, 32> nibbles{};
    std::size_t count = 0;
    for (const char c : text) {
        if (c == '-') {
            continue;
        }
        std::uint8_t nibble;
        if (c >= '0' && c <= '9') {
            nibble = static_cast<std::uint8_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            nibble = static_cast<std::uint8_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            nibble = static_cast<std::uint8_t>(c - 'A' + 10);
        } else {
            return Uuid{};
        }
        if (count >= nibbles.size()) {
            return Uuid{};
        }
        nibbles[count++] = nibble;
    }
    if (count != nibbles.size()) {
        return Uuid{};
    }

    Uuid id;
    for (std::size_t i = 0; i < 16; ++i) {
        id.high |= static_cast<std::uint64_t>(nibbles[i]) << ((15 - i) * 4);
    }
    for (std::size_t i = 0; i < 16; ++i) {
        id.low |= static_cast<std::uint64_t>(nibbles[16 + i]) << ((15 - i) * 4);
    }
    return id;
}

} // namespace rb
