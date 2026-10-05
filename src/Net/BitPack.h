#pragma once

// Shared cursor for NNet local and system events.
// Bit 0 of a byte is written first. A field that crosses a byte boundary writes
// its high slice at the earlier bit. A 0-bit field writes nothing. 64-bit
// fields are allowed. Callers subtract the schema minimum before write().

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kitsune::net {

class BitWriter {
public:
    explicit BitWriter(std::span<std::byte> out) : out_(out) {}

    [[nodiscard]] bool write(std::uint64_t value, int bits) {
        if (bits == 0) {
            return true;
        }
        if (bits < 0 || bits > 64) {
            return false;
        }
        if (bits < 64 && value >= (std::uint64_t{1} << bits)) {
            return false;
        }
        auto remaining = bits;
        while (remaining > 0) {
            const auto bitpos = static_cast<int>(cursor_ & 7);
            const auto room = 8 - bitpos;
            const auto take = room < remaining ? room : remaining;
            const auto shift = remaining - take;
            const auto chunk =
                static_cast<unsigned>((value >> shift) & ((std::uint64_t{1} << take) - 1));
            const auto index = cursor_ >> 3;
            if (index >= out_.size()) {
                return false;
            }
            const auto mask = ((1u << take) - 1u) << bitpos;
            auto byte = static_cast<unsigned>(out_[index]);
            byte = (byte & ~mask) | (chunk << bitpos);
            out_[index] = static_cast<std::byte>(byte);
            cursor_ += static_cast<std::size_t>(take);
            remaining -= take;
        }
        return true;
    }

    // String and blob payloads start on a byte. The pad bits are zero.
    [[nodiscard]] bool align() {
        const auto used = static_cast<int>(cursor_ & 7);
        if (used == 0) {
            return true;
        }
        return write(0, 8 - used);
    }

    [[nodiscard]] std::size_t bytes() const noexcept { return (cursor_ + 7) / 8; }

private:
    std::span<std::byte> out_;
    std::size_t cursor_ = 0;
};

class BitReader {
public:
    explicit BitReader(std::span<const std::byte> in) : in_(in) {}

    [[nodiscard]] bool read(std::uint64_t& value, int bits) {
        if (bits == 0) {
            value = 0;
            return true;
        }
        if (bits < 0 || bits > 64) {
            return false;
        }
        value = 0;
        auto remaining = bits;
        while (remaining > 0) {
            const auto index = cursor_ >> 3;
            if (index >= in_.size()) {
                exhausted_ = true;
                return false;
            }
            const auto bitpos = static_cast<int>(cursor_ & 7);
            const auto room = 8 - bitpos;
            const auto take = room < remaining ? room : remaining;
            const auto shift = remaining - take;
            const auto byte = static_cast<unsigned>(in_[index]);
            const auto chunk = (byte >> bitpos) & ((1u << take) - 1u);
            value |= static_cast<std::uint64_t>(chunk) << shift;
            cursor_ += static_cast<std::size_t>(take);
            remaining -= take;
        }
        return true;
    }

    [[nodiscard]] bool align() {
        const auto used = static_cast<int>(cursor_ & 7);
        if (used == 0) {
            return true;
        }
        std::uint64_t pad = 0;
        return read(pad, 8 - used) && pad == 0;
    }

    // Unused bits of the last byte are padding and must be zero.
    [[nodiscard]] bool finish() const {
        if (in_.empty() || ((cursor_ + 7) / 8) != in_.size()) {
            return false;
        }
        const auto used = static_cast<unsigned>(cursor_ & 7);
        if (used == 0) {
            return true;
        }
        const auto last = static_cast<unsigned>(in_.back());
        return (last >> used) == 0;
    }

    // Stream use: whole bytes read so far, and whether a read ran past the data.
    [[nodiscard]] std::size_t consumedBytes() const noexcept { return (cursor_ + 7) / 8; }
    [[nodiscard]] bool exhausted() const noexcept { return exhausted_; }

    // The unused bits of the last byte read are zero.
    [[nodiscard]] bool paddingClean() const {
        const auto used = static_cast<unsigned>(cursor_ & 7);
        if (used == 0) {
            return true;
        }
        const auto last = static_cast<unsigned>(in_[(cursor_ >> 3)]);
        return (last >> used) == 0;
    }

private:
    std::span<const std::byte> in_;
    std::size_t cursor_ = 0;
    bool exhausted_ = false;
};

[[nodiscard]] inline bool writeUnsigned(BitWriter& bits, std::uint64_t value, std::uint64_t min,
                                        std::uint64_t max, int width) {
    if (value < min || value > max) {
        return false;
    }
    return bits.write(value - min, width);
}

[[nodiscard]] inline bool readUnsigned(BitReader& bits, std::uint64_t& value, std::uint64_t min,
                                       std::uint64_t max, int width) {
    if (max < min) {
        return false;
    }
    std::uint64_t wire = 0;
    if (!bits.read(wire, width) || wire > max - min) {
        return false;
    }
    value = wire + min;
    return true;
}

[[nodiscard]] inline bool writeSigned(BitWriter& bits, std::int64_t value, std::int64_t min,
                                      std::int64_t max, int width) {
    if (value < min || value > max) {
        return false;
    }
    const auto wire = static_cast<std::uint64_t>(value) - static_cast<std::uint64_t>(min);
    return bits.write(wire, width);
}

[[nodiscard]] inline bool readSigned(BitReader& bits, std::int64_t& value, std::int64_t min,
                                     std::int64_t max, int width) {
    std::uint64_t wire = 0;
    if (!bits.read(wire, width)) {
        return false;
    }
    const auto decoded = static_cast<std::int64_t>(wire + static_cast<std::uint64_t>(min));
    if (decoded < min || decoded > max) {
        return false;
    }
    value = decoded;
    return true;
}

// An enum is its value in `width` bits. `isValid` (Enums.h) is true for the members the client accepts.
template <typename E>
[[nodiscard]] bool writeEnum(BitWriter& bits, E value, int width) {
    return isValid(value) && bits.write(static_cast<std::uint64_t>(value), width);
}

template <typename E>
[[nodiscard]] bool readEnum(BitReader& bits, E& value, int width) {
    std::uint64_t raw = 0;
    if (!bits.read(raw, width)) {
        return false;
    }
    const auto candidate = static_cast<E>(raw);
    if (!isValid(candidate)) {
        return false;
    }
    value = candidate;
    return true;
}

[[nodiscard]] inline bool writeBool(BitWriter& bits, bool value) {
    return bits.write(value ? 1 : 0, 1);
}

[[nodiscard]] inline bool readBool(BitReader& bits, bool& value) {
    std::uint64_t raw = 0;
    if (!bits.read(raw, 1) || raw > 1) {
        return false;
    }
    value = raw == 1;
    return true;
}

// String length uses schemaBits + 2. The byte count may run up to schemaMax << 2.
// The length is an integer, then the cursor is aligned, then that many bytes.
[[nodiscard]] inline bool writeString(BitWriter& bits, std::string_view text, int schemaBits,
                                      std::uint64_t schemaMax) {
    const auto wireMax = schemaMax << 2;
    if (!writeUnsigned(bits, text.size(), 0, wireMax, schemaBits + 2) || !bits.align()) {
        return false;
    }
    for (const unsigned char unit : text) {
        if (!bits.write(unit, 8)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool readString(BitReader& bits, std::string& text, int schemaBits,
                                     std::uint64_t schemaMax) {
    std::uint64_t length = 0;
    if (!readUnsigned(bits, length, 0, schemaMax << 2, schemaBits + 2) || !bits.align()) {
        return false;
    }
    text.assign(static_cast<std::size_t>(length), '\0');
    for (std::size_t i = 0; i < length; ++i) {
        std::uint64_t unit = 0;
        if (!bits.read(unit, 8)) {
            return false;
        }
        text[i] = static_cast<char>(unit);
    }
    return true;
}

[[nodiscard]] inline bool writeBlob(BitWriter& bits, std::span<const std::byte> data, int width,
                                    std::uint64_t max) {
    if (!writeUnsigned(bits, data.size(), 0, max, width) || !bits.align()) {
        return false;
    }
    for (const auto unit : data) {
        if (!bits.write(static_cast<unsigned>(unit), 8)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool readBlob(BitReader& bits, std::vector<std::byte>& data, int width,
                                   std::uint64_t max) {
    std::uint64_t length = 0;
    if (!readUnsigned(bits, length, 0, max, width) || !bits.align()) {
        return false;
    }
    data.assign(static_cast<std::size_t>(length), std::byte{0});
    for (auto& unit : data) {
        std::uint64_t raw = 0;
        if (!bits.read(raw, 8)) {
            return false;
        }
        unit = static_cast<std::byte>(raw);
    }
    return true;
}

// A high-bit blob has no length prefix. Cache handles and clan logos are 40 bytes.
[[nodiscard]] inline bool writeFixedBlob(BitWriter& bits, std::span<const std::byte> data,
                                         std::size_t size) {
    if (data.size() != size || !bits.align()) {
        return false;
    }
    for (const auto unit : data) {
        if (!bits.write(static_cast<unsigned>(unit), 8)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool readFixedBlob(BitReader& bits, std::span<std::byte> data) {
    if (!bits.align()) {
        return false;
    }
    for (auto& unit : data) {
        std::uint64_t raw = 0;
        if (!bits.read(raw, 8)) {
            return false;
        }
        unit = static_cast<std::byte>(raw);
    }
    return true;
}

// Length integer, then that many raw bits. No byte alignment.
[[nodiscard]] inline bool writeBitArray(BitWriter& bits, std::span<const std::uint8_t> flags,
                                        int lengthBits, std::uint64_t maxLength) {
    if (!writeUnsigned(bits, flags.size(), 0, maxLength, lengthBits)) {
        return false;
    }
    for (const auto flag : flags) {
        if (flag > 1 || !bits.write(flag, 1)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool readBitArray(BitReader& bits, std::vector<std::uint8_t>& flags,
                                       int lengthBits, std::uint64_t maxLength) {
    std::uint64_t length = 0;
    if (!readUnsigned(bits, length, 0, maxLength, lengthBits)) {
        return false;
    }
    flags.assign(static_cast<std::size_t>(length), 0);
    for (auto& flag : flags) {
        std::uint64_t raw = 0;
        if (!bits.read(raw, 1) || raw > 1) {
            return false;
        }
        flag = static_cast<std::uint8_t>(raw);
    }
    return true;
}

template <typename T, typename WriteOne>
[[nodiscard]] bool writeArray(BitWriter& bits, const std::vector<T>& values, int countBits,
                              std::uint64_t maxCount, WriteOne&& writeOne) {
    if (!writeUnsigned(bits, values.size(), 0, maxCount, countBits)) {
        return false;
    }
    for (const auto& value : values) {
        if (!writeOne(value)) {
            return false;
        }
    }
    return true;
}

template <typename T, typename ReadOne>
[[nodiscard]] bool readArray(BitReader& bits, std::vector<T>& values, int countBits,
                             std::uint64_t maxCount, ReadOne&& readOne) {
    std::uint64_t count = 0;
    if (!readUnsigned(bits, count, 0, maxCount, countBits)) {
        return false;
    }
    values.resize(static_cast<std::size_t>(count));
    for (auto& value : values) {
        if (!readOne(value)) {
            return false;
        }
    }
    return true;
}

} // namespace kitsune::net
