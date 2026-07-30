#pragma once
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "core/Types.hpp"

// Endian-aware binary reader/writer. The backbone for parsing GRF archives and
// RO asset formats (v1/v2) and for (de)serialising network packets (v4).
// RO formats are little-endian, so that is the default everywhere.
namespace uaro {

enum class Endian { Little, Big };

namespace detail {
template <class T>
inline T byteswap(T v) {
    static_assert(std::is_trivially_copyable_v<T>, "byteswap requires POD");
    u8 b[sizeof(T)];
    std::memcpy(b, &v, sizeof(T));
    for (usize i = 0; i < sizeof(T) / 2; ++i) {
        u8 t = b[i];
        b[i] = b[sizeof(T) - 1 - i];
        b[sizeof(T) - 1 - i] = t;
    }
    std::memcpy(&v, b, sizeof(T));
    return v;
}
} // namespace detail

// Non-owning view over a byte range. Throws std::out_of_range on overflow.
class ByteReader {
public:
    ByteReader(const u8* data, usize size) : data_(data), size_(size) {}
    explicit ByteReader(const std::vector<u8>& v) : data_(v.data()), size_(v.size()) {}

    usize pos() const { return pos_; }
    usize size() const { return size_; }
    usize remaining() const { return size_ - pos_; }
    bool eof() const { return pos_ >= size_; }
    const u8* ptr() const { return data_ + pos_; }

    void seek(usize p) {
        if (p > size_) throw std::out_of_range("ByteReader::seek");
        pos_ = p;
    }
    void skip(usize n) {
        ensure(n);
        pos_ += n;
    }

    template <class T>
    T read(Endian e = Endian::Little) {
        static_assert(std::is_trivially_copyable_v<T>, "read<T> requires POD");
        ensure(sizeof(T));
        T v;
        std::memcpy(&v, data_ + pos_, sizeof(T));
        pos_ += sizeof(T);
        if (e == Endian::Big) v = detail::byteswap(v);
        return v;
    }

    u8  u8v()   { return read<u8>(); }
    u16 u16le() { return read<u16>(Endian::Little); }
    u32 u32le() { return read<u32>(Endian::Little); }
    u64 u64le() { return read<u64>(Endian::Little); }
    i16 i16le() { return read<i16>(Endian::Little); }
    i32 i32le() { return read<i32>(Endian::Little); }
    f32 f32le() { return read<f32>(Endian::Little); }

    // Read exactly `n` raw bytes.
    void read_bytes(void* dst, usize n) {
        ensure(n);
        std::memcpy(dst, data_ + pos_, n);
        pos_ += n;
    }

    // Read a fixed-width string field; the logical string ends at the first NUL.
    // Always advances the full `field` width (typical for RO name fields).
    std::string read_cstring(usize field) {
        ensure(field);
        const char* p = reinterpret_cast<const char*>(data_ + pos_);
        usize n = 0;
        while (n < field && p[n] != '\0') ++n;
        std::string s(p, n);
        pos_ += field;
        return s;
    }

    // Read exactly `n` bytes as a string (no NUL handling).
    std::string read_string(usize n) {
        ensure(n);
        std::string s(reinterpret_cast<const char*>(data_ + pos_), n);
        pos_ += n;
        return s;
    }

    // Read a variable-length NUL-terminated string; consumes the terminator.
    // Stops at end-of-buffer if no NUL is found. Used for GRF table entries.
    std::string read_cstr() {
        std::string s;
        while (pos_ < size_) {
            char c = static_cast<char>(data_[pos_++]);
            if (c == '\0') break;
            s.push_back(c);
        }
        return s;
    }

private:
    void ensure(usize n) const {
        if (pos_ + n > size_) throw std::out_of_range("ByteReader overflow");
    }
    const u8* data_;
    usize size_;
    usize pos_ = 0;
};

// Growable little-endian-by-default writer.
class ByteWriter {
public:
    template <class T>
    void write(T v, Endian e = Endian::Little) {
        static_assert(std::is_trivially_copyable_v<T>, "write<T> requires POD");
        if (e == Endian::Big) v = detail::byteswap(v);
        const u8* p = reinterpret_cast<const u8*>(&v);
        buf_.insert(buf_.end(), p, p + sizeof(T));
    }

    void u8v(u8 v)     { write<u8>(v); }
    void u16le(u16 v)  { write<u16>(v, Endian::Little); }
    void u32le(u32 v)  { write<u32>(v, Endian::Little); }

    void write_bytes(const void* src, usize n) {
        const u8* p = static_cast<const u8*>(src);
        buf_.insert(buf_.end(), p, p + n);
    }
    void write_string(std::string_view s) { write_bytes(s.data(), s.size()); }

    const std::vector<u8>& data() const { return buf_; }
    usize size() const { return buf_.size(); }
    void clear() { buf_.clear(); }

private:
    std::vector<u8> buf_;
};

} // namespace uaro
