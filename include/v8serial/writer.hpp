#ifndef V8SERIAL_WRITER_HPP
#define V8SERIAL_WRITER_HPP

#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace v8serial {

/// Writes one value using the supported subset of V8 wire format version 15.
///
/// The writer owns its byte buffer, has no V8 or N-API dependency, and may be
/// used on any thread. A single instance is not safe for concurrent access.
class Writer {
 public:
  /// Wire-format version emitted in every message header.
  static constexpr uint32_t kFormatVersion = 15;

  /// Creates a writer and emits the version header.
  ///
  /// @param initial_capacity Initial output-vector capacity. This is only a
  /// performance hint and does not limit the final message size.
  explicit Writer(size_t initial_capacity = 256) {
    bytes_.reserve(initial_capacity < 2 ? 2 : initial_capacity);
    stack_.reserve(8);
    bytes_.push_back(0xff);
    bytes_.push_back(static_cast<uint8_t>(kFormatVersion));
  }

  /// Writes JavaScript `undefined`.
  void undefined() { scalar('_'); }

  /// Writes JavaScript `null`.
  void null() { scalar('0'); }

  /// Writes a Boolean value.
  void boolean(bool value) { scalar(value ? 'T' : 'F'); }

  /// Writes a signed 32-bit integer using ZigZag varint encoding.
  void int32(int32_t value) {
    beginValue();
    byte('I');
    varint((static_cast<uint32_t>(value) << 1) ^
           static_cast<uint32_t>(value >> 31));
  }

  /// Writes an IEEE-754 double in the version-15 native representation.
  ///
  /// Use this method for non-integral numbers, NaN, infinities, and negative
  /// zero. The implementation emits little-endian bytes.
  void number(double value) {
    beginValue();
    byte('N');
    uint64_t bits;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    for (unsigned shift = 0; shift < 64; shift += 8) {
      byte(static_cast<uint8_t>(bits >> shift));
    }
  }

  /// Writes a UTF-8 string.
  ///
  /// The caller must provide valid UTF-8. The bytes are copied immediately.
  void string(std::string_view utf8) {
    beginValue();
    rawUtf8(utf8);
  }

  /// Writes a UTF-16 string, selecting Latin-1 when every code unit fits.
  void string(std::u16string_view value) {
    beginValue();
    rawUtf16(value);
  }

  /// Writes an object property key.
  ///
  /// This is valid only after beginObject() or after completing the preceding
  /// property value. Exactly one value must follow each key.
  ///
  /// @throws std::logic_error if no object key is currently expected.
  void key(std::u16string_view value) {
    if (stack_.empty() || stack_.back().kind != Kind::Object ||
        !stack_.back().expecting_key) {
      throw std::logic_error("key() is only valid where an object key is expected");
    }
    rawUtf16(value);
    stack_.back().expecting_key = false;
  }

  /// Starts a plain string-keyed object.
  void beginObject() {
    beginValue();
    byte('o');
    stack_.push_back({Kind::Object, 0, 0, true});
  }

  /// Ends the current object and writes its property count.
  ///
  /// @throws std::logic_error for a mismatched container or key without value.
  void endObject() {
    requireContainer(Kind::Object, "endObject()");
    const Frame frame = stack_.back();
    if (!frame.expecting_key) {
      throw std::logic_error("object key has no value");
    }
    stack_.pop_back();
    byte('{');
    varint(frame.count);
  }

  /// Starts a dense array with a known number of elements.
  ///
  /// Exactly @p length values must be written before endArray().
  void beginArray(uint32_t length) {
    beginValue();
    byte('A');
    varint(length);
    stack_.push_back({Kind::Array, 0, length, false});
  }

  /// Ends the current dense array.
  ///
  /// @throws std::logic_error for a mismatched container or element-count
  /// mismatch.
  void endArray() {
    requireContainer(Kind::Array, "endArray()");
    const Frame frame = stack_.back();
    if (frame.count != frame.expected) {
      throw std::logic_error("array element count does not match declared length");
    }
    stack_.pop_back();
    byte('$');
    varint(0);  // No named properties.
    varint(frame.expected);
  }

  /// Writes an ordinary ArrayBuffer by copying @p size bytes from @p data.
  ///
  /// @throws std::invalid_argument if data is null and size is nonzero.
  /// @throws std::length_error if size exceeds UINT32_MAX.
  void arrayBuffer(const uint8_t* data, size_t size) {
    beginValue();
    rawArrayBuffer(data, size);
  }

  /// Writes a Uint8Array with a new backing buffer and byte offset zero.
  ///
  /// The input bytes are copied immediately. This emits V8's native
  /// ArrayBuffer-plus-view form rather than Node's host-object form.
  ///
  /// @throws std::invalid_argument if data is null and size is nonzero.
  /// @throws std::length_error if size exceeds UINT32_MAX.
  void uint8Array(const uint8_t* data, size_t size) {
    beginValue();
    rawArrayBuffer(data, size);
    byte('V');
    byte('B');  // V8's ArrayBufferViewTag::kUint8Array.
    varint(0);  // byteOffset
    checkedVarint(size);
    varint(0);  // version >= 14 view flags
  }

  /// Moves out the completed version-15 message.
  ///
  /// @throws std::logic_error if no root exists or a container remains open.
  /// The writer should not be reused after this operation.
  std::vector<uint8_t> take() {
    if (!stack_.empty()) {
      throw std::logic_error("cannot take bytes with an open container");
    }
    if (!has_root_) {
      throw std::logic_error("cannot take bytes before writing a value");
    }
    return std::move(bytes_);
  }

 private:
  enum class Kind { Object, Array };

  struct Frame {
    Kind kind;
    uint32_t count;
    uint32_t expected;
    bool expecting_key;
  };

  std::vector<uint8_t> bytes_;
  std::vector<Frame> stack_;
  bool has_root_ = false;

  void byte(uint8_t value) { bytes_.push_back(value); }

  void varint(uint32_t value) {
    do {
      uint8_t next = static_cast<uint8_t>(value & 0x7f);
      value >>= 7;
      if (value != 0) next |= 0x80;
      byte(next);
    } while (value != 0);
  }

  static size_t varintSize(uint32_t value) {
    size_t size = 1;
    while (value >= 0x80) {
      value >>= 7;
      ++size;
    }
    return size;
  }

  void checkedVarint(size_t value) {
    if (value > std::numeric_limits<uint32_t>::max()) {
      throw std::length_error("value exceeds V8 wire-format uint32 limit");
    }
    varint(static_cast<uint32_t>(value));
  }

  void beginValue() {
    if (stack_.empty()) {
      if (has_root_) throw std::logic_error("writer accepts exactly one root value");
      has_root_ = true;
      return;
    }

    Frame& parent = stack_.back();
    if (parent.kind == Kind::Object) {
      if (parent.expecting_key) {
        throw std::logic_error("object value must be preceded by key()");
      }
      parent.expecting_key = true;
      ++parent.count;
    } else {
      if (parent.count == parent.expected) {
        throw std::logic_error("array has more values than its declared length");
      }
      ++parent.count;
    }
  }

  void scalar(uint8_t tag) {
    beginValue();
    byte(tag);
  }

  void rawUtf8(std::string_view value) {
    byte('S');
    checkedVarint(value.size());
    bytes_.insert(bytes_.end(), value.begin(), value.end());
  }

  void rawUtf16(std::u16string_view value) {
    bool one_byte = true;
    for (char16_t code_unit : value) {
      if (code_unit > 0xff) {
        one_byte = false;
        break;
      }
    }

    if (one_byte) {
      byte('"');
      checkedVarint(value.size());
      for (char16_t code_unit : value) byte(static_cast<uint8_t>(code_unit));
      return;
    }

    if (value.size() > std::numeric_limits<uint32_t>::max() / 2U) {
      throw std::length_error("UTF-16 string exceeds V8 wire-format limit");
    }
    const uint32_t byte_length = static_cast<uint32_t>(value.size() * 2U);
    if (((bytes_.size() + 1U + varintSize(byte_length)) & 1U) != 0) byte(0);
    byte('c');
    varint(byte_length);
    for (char16_t code_unit : value) {
      byte(static_cast<uint8_t>(code_unit));
      byte(static_cast<uint8_t>(code_unit >> 8));
    }
  }

  void rawArrayBuffer(const uint8_t* data, size_t size) {
    if (size != 0 && data == nullptr) {
      throw std::invalid_argument("non-empty binary value has null data");
    }
    byte('B');
    checkedVarint(size);
    if (size != 0) bytes_.insert(bytes_.end(), data, data + size);
  }

  void requireContainer(Kind expected, const char* operation) const {
    if (stack_.empty() || stack_.back().kind != expected) {
      throw std::logic_error(std::string(operation) +
                             " does not match the open container");
    }
  }
};

}  // namespace v8serial

#endif
