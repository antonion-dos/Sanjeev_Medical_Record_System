#pragma once

#include <vector>
#include <string>
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace Sanjeev {

class BinaryWriter {
public:
    BinaryWriter() = default;

    // Direct buffer access
    const std::vector<uint8_t>& get_buffer() const { return buffer_; }
    std::vector<uint8_t> release() { return std::move(buffer_); }
    size_t size() const { return buffer_.size(); }
    void clear() { buffer_.clear(); }

    // Primitive unsigned integers (Little-Endian)
    void write_uint8(uint8_t value) {
        buffer_.push_back(value);
    }

    void write_uint16(uint16_t value) {
        buffer_.push_back(static_cast<uint8_t>(value & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    }

    void write_uint32(uint32_t value) {
        buffer_.push_back(static_cast<uint8_t>(value & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    }

    void write_uint64(uint64_t value) {
        for (int i = 0; i < 8; ++i) {
            buffer_.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
        }
    }

    // Fixed-size raw bytes
    void write_bytes(const uint8_t* data, size_t length) {
        if (length > 0 && data != nullptr) {
            buffer_.insert(buffer_.end(), data, data + length);
        }
    }

    template <size_t N>
    void write_fixed_array(const std::array<uint8_t, N>& arr) {
        write_bytes(arr.data(), N);
    }

    // Length-prefixed dynamic vectors
    void write_var_bytes(const std::vector<uint8_t>& bytes) {
        write_uint32(static_cast<uint32_t>(bytes.size()));
        write_bytes(bytes.data(), bytes.size());
    }

    // Length-prefixed string
    void write_string(const std::string& str) {
        write_uint32(static_cast<uint32_t>(str.size()));
        if (!str.empty()) {
            write_bytes(reinterpret_cast<const uint8_t*>(str.data()), str.size());
        }
    }

private:
    std::vector<uint8_t> buffer_;
};

class BinaryReader {
public:
    BinaryReader(const uint8_t* data, size_t length)
        : data_(data), length_(length), offset_(0) {}

    explicit BinaryReader(const std::vector<uint8_t>& buffer)
        : data_(buffer.data()), length_(buffer.size()), offset_(0) {}

    size_t remaining() const {
        return (offset_ <= length_) ? (length_ - offset_) : 0;
    }

    size_t offset() const { return offset_; }

    void require(size_t bytes) const {
        if (offset_ + bytes > length_) {
            throw std::out_of_range("BinaryReader: unexpected end of buffer (needed " +
                                    std::to_string(bytes) + ", remaining " +
                                    std::to_string(remaining()) + ")");
        }
    }

    uint8_t read_uint8() {
        require(1);
        return data_[offset_++];
    }

    uint16_t read_uint16() {
        require(2);
        uint16_t v = static_cast<uint16_t>(data_[offset_]) |
                     (static_cast<uint16_t>(data_[offset_ + 1]) << 8);
        offset_ += 2;
        return v;
    }

    uint32_t read_uint32() {
        require(4);
        uint32_t v = static_cast<uint32_t>(data_[offset_]) |
                     (static_cast<uint32_t>(data_[offset_ + 1]) << 8) |
                     (static_cast<uint32_t>(data_[offset_ + 2]) << 16) |
                     (static_cast<uint32_t>(data_[offset_ + 3]) << 24);
        offset_ += 4;
        return v;
    }

    uint64_t read_uint64() {
        require(8);
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= (static_cast<uint64_t>(data_[offset_ + i]) << (i * 8));
        }
        offset_ += 8;
        return v;
    }

    void read_bytes(uint8_t* dest, size_t count) {
        require(count);
        if (count > 0 && dest != nullptr) {
            std::memcpy(dest, data_ + offset_, count);
            offset_ += count;
        }
    }

    template <size_t N>
    std::array<uint8_t, N> read_fixed_array() {
        std::array<uint8_t, N> arr;
        read_bytes(arr.data(), N);
        return arr;
    }

    std::vector<uint8_t> read_var_bytes() {
        uint32_t len = read_uint32();
        require(len);
        std::vector<uint8_t> result(len);
        if (len > 0) {
            std::memcpy(result.data(), data_ + offset_, len);
            offset_ += len;
        }
        return result;
    }

    std::string read_string() {
        uint32_t len = read_uint32();
        require(len);
        if (len == 0) return "";
        std::string s(reinterpret_cast<const char*>(data_ + offset_), len);
        offset_ += len;
        return s;
    }

private:
    const uint8_t* data_;
    size_t length_;
    size_t offset_;
};

} // namespace Sanjeev
