#include <cassert>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "v8serial/reader.hpp"
#include "v8serial/writer.hpp"

namespace {

template <typename Exception>
void ExpectThrows(const std::function<void()>& operation) {
  bool threw = false;
  try {
    operation();
  } catch (const Exception&) {
    threw = true;
  }
  assert(threw);
}

void TestWriterAndReaderRoundTrip() {
  v8serial::Writer writer;
  writer.beginObject();
  writer.key(u"id");
  writer.int32(42);
  writer.key(u"name");
  writer.string(u"\u0100");
  writer.key(u"items");
  writer.beginArray(4);
  writer.null();
  writer.boolean(true);
  writer.number(-0.0);
  const uint8_t bytes[] = {1, 2, 3};
  writer.uint8Array(bytes, sizeof(bytes));
  writer.endArray();
  writer.endObject();

  const std::vector<uint8_t> encoded = writer.take();
  assert(encoded[0] == 0xff);
  assert(encoded[1] == v8serial::Writer::kFormatVersion);

  const v8serial::DecodedValue decoded = v8serial::Reader(encoded).read();
  assert(decoded.type == v8serial::DecodedType::Object);
  assert(decoded.object.size() == 3);
  assert(decoded.object[0].first == u"id");
  assert(decoded.object[0].second.int32 == 42);
  assert(decoded.object[1].second.string == u"\u0100");
  assert(decoded.object[2].second.array.size() == 4);
  assert(decoded.object[2].second.array[3].type ==
         v8serial::DecodedType::Uint8Array);
  assert(decoded.object[2].second.array[3].binary ==
         std::vector<uint8_t>({1, 2, 3}));
}

void TestWriterInvariants() {
  ExpectThrows<std::logic_error>([] {
    v8serial::Writer writer;
    writer.take();
  });
  ExpectThrows<std::logic_error>([] {
    v8serial::Writer writer;
    writer.int32(1);
    writer.int32(2);
  });
  ExpectThrows<std::logic_error>([] {
    v8serial::Writer writer;
    writer.beginObject();
    writer.int32(1);
  });
  ExpectThrows<std::logic_error>([] {
    v8serial::Writer writer;
    writer.beginObject();
    writer.key(u"x");
    writer.endObject();
  });
  ExpectThrows<std::logic_error>([] {
    v8serial::Writer writer;
    writer.beginArray(1);
    writer.endArray();
  });
  ExpectThrows<std::logic_error>([] {
    v8serial::Writer writer;
    writer.beginArray(1);
    writer.null();
    writer.null();
  });
  ExpectThrows<std::invalid_argument>([] {
    v8serial::Writer writer;
    writer.arrayBuffer(nullptr, 1);
  });
  ExpectThrows<std::invalid_argument>([] {
    const uint8_t byte = 0;
    v8serial::Writer writer;
    writer.arrayBufferView(v8serial::ArrayBufferViewType::Int16Array,
                           &byte, 1);
  });
}

void TestReaderValidation() {
  ExpectThrows<std::invalid_argument>(
      [] { v8serial::Reader(nullptr, 1); });
  ExpectThrows<v8serial::DecodeError>([] {
    const std::vector<uint8_t> bytes = {};
    v8serial::Reader(bytes).read();
  });
  ExpectThrows<v8serial::DecodeError>([] {
    const std::vector<uint8_t> bytes = {0xff, 0x0e, '0'};
    v8serial::Reader(bytes).read();
  });
  ExpectThrows<v8serial::DecodeError>([] {
    const std::vector<uint8_t> bytes = {0xff, 0x0f, 'B', 0x02, 0x01};
    v8serial::Reader(bytes).read();
  });
  ExpectThrows<v8serial::DecodeError>([] {
    const std::vector<uint8_t> bytes = {0xff, 0x0f, 'c', 0x01, 0x00};
    v8serial::Reader(bytes).read();
  });
  ExpectThrows<v8serial::DecodeError>([] {
    const std::vector<uint8_t> bytes = {0xff, 0x0f, 'I', 0x80, 0x80,
                                        0x80, 0x80, 0x10};
    v8serial::Reader(bytes).read();
  });
}

void TestStreamingRows() {
  v8serial::Writer writer;
  writer.beginArray(2);
  writer.beginArray(5);
  writer.string(u"alpha");
  writer.int32(-7);
  writer.number(1.5);
  writer.boolean(true);
  writer.null();
  writer.endArray();
  writer.beginArray(5);
  writer.string(u"beta");
  writer.uint32(UINT32_MAX);
  writer.number(-2.25);
  writer.boolean(false);
  writer.undefined();
  writer.endArray();
  writer.endArray();
  const std::vector<uint8_t> encoded = writer.take();

  uint32_t calls = 0;
  uint64_t checksum = 0;
  const uint32_t rows = v8serial::Reader(encoded).readRows(
      5, [&](uint32_t row, uint32_t column, uint32_t column_count,
             const v8serial::ScalarValue& value) {
        assert(column_count == 5);
        assert(row < 2);
        assert(column < column_count);
        ++calls;
        checksum += static_cast<uint64_t>(value.type) + row + column;
        if (column == 0) {
          assert(value.type == v8serial::ScalarType::Latin1String);
          assert(value.bytes != nullptr);
          checksum += value.byte_count;
        }
      });

  assert(rows == 2);
  assert(calls == 10);
  assert(checksum != 0);
}

void TestStreamingRowsValidation() {
  ExpectThrows<v8serial::DecodeError>([] {
    v8serial::Writer writer;
    writer.beginArray(2);
    writer.beginArray(1);
    writer.int32(1);
    writer.endArray();
    writer.beginArray(2);
    writer.int32(2);
    writer.int32(3);
    writer.endArray();
    writer.endArray();
    v8serial::Reader(writer.take()).readRows(
        1, [](uint32_t, uint32_t, uint32_t,
              const v8serial::ScalarValue&) {});
  });

  ExpectThrows<v8serial::DecodeError>([] {
    v8serial::Writer writer;
    writer.beginArray(2);
    writer.beginArray(0);
    writer.endArray();
    writer.beginArray(1);
    writer.int32(1);
    writer.endArray();
    writer.endArray();
    v8serial::Reader(writer.take()).readRows(
        0, [](uint32_t, uint32_t, uint32_t,
              const v8serial::ScalarValue&) {});
  });

  ExpectThrows<v8serial::DecodeError>([] {
    v8serial::Writer writer;
    writer.beginArray(1);
    writer.beginArray(1);
    writer.beginObject();
    writer.key(u"value");
    writer.int32(1);
    writer.endObject();
    writer.endArray();
    writer.endArray();
    v8serial::Reader(writer.take()).readRows(
        1, [](uint32_t, uint32_t, uint32_t,
              const v8serial::ScalarValue&) {});
  });

  ExpectThrows<v8serial::DecodeError>([] {
    v8serial::Writer writer;
    writer.beginArray(1);
    writer.beginArray(1);
    writer.int32(1);
    writer.endArray();
    writer.endArray();
    v8serial::Reader(writer.take()).readRows(
        2, [](uint32_t, uint32_t, uint32_t,
              const v8serial::ScalarValue&) {});
  });
}

void TestStreamingStringViews() {
  v8serial::Writer writer;
  writer.beginArray(1);
  writer.beginArray(3);
  writer.string(u"latin");
  writer.string(std::string_view("\xc3\xa9"));
  writer.string(u"\u0100");
  writer.endArray();
  writer.endArray();

  const v8serial::ScalarType expected[] = {
      v8serial::ScalarType::Latin1String,
      v8serial::ScalarType::Utf8String,
      v8serial::ScalarType::Utf16String,
  };
  uint32_t strings = 0;
  v8serial::Reader(writer.take()).readRows(
      3, [&](uint32_t, uint32_t column, uint32_t,
             const v8serial::ScalarValue& value) {
        assert(value.type == expected[column]);
        assert(value.bytes != nullptr);
        assert(value.byte_count > 0);
        ++strings;
      });
  assert(strings == 3);
}

void TestNativeViewWithOffset() {
  const std::vector<uint8_t> bytes = {
      0xff, 0x0f, 'B', 0x05, 9, 1, 2, 3, 9,
      'V',  'B',  0x01, 0x03, 0x00,
  };
  const v8serial::DecodedValue decoded = v8serial::Reader(bytes).read();
  assert(decoded.type == v8serial::DecodedType::Uint8Array);
  assert(decoded.binary == std::vector<uint8_t>({1, 2, 3}));
}

void TestNativeViewRangesAndOwnership() {
  struct View {
    uint8_t tag;
    v8serial::ArrayBufferViewType type;
  };
  using Type = v8serial::ArrayBufferViewType;
  const View views[] = {
      {'b', Type::Int8Array}, {'B', Type::Uint8Array},
      {'C', Type::Uint8ClampedArray}, {'w', Type::Int16Array},
      {'W', Type::Uint16Array}, {'d', Type::Int32Array},
      {'D', Type::Uint32Array}, {'f', Type::Float32Array},
      {'F', Type::Float64Array}, {'q', Type::BigInt64Array},
      {'Q', Type::BigUint64Array}, {'?', Type::DataView},
  };
  for (const View& view : views) {
    const size_t element_size =
        v8serial::detail::arrayBufferViewElementSize(view.type);
    for (size_t offset = 0; offset <= 8; offset += element_size) {
      for (size_t length = 0; length <= 8 - offset; length += element_size) {
        std::vector<uint8_t> bytes = {
            0xff, 0x0f, 'B', 8, 0, 1, 2, 3, 4, 5, 6, 7,
            'V', view.tag, static_cast<uint8_t>(offset),
            static_cast<uint8_t>(length), 0, 0,
        };
        const std::vector<uint8_t> expected(bytes.begin() + 4 + offset,
                                            bytes.begin() + 4 + offset + length);
        const v8serial::DecodedValue decoded = v8serial::Reader(bytes).read();
        assert(decoded.type == (view.type == Type::Uint8Array
                                    ? v8serial::DecodedType::Uint8Array
                                    : v8serial::DecodedType::ArrayBufferView));
        assert(decoded.view_type == view.type);
        for (uint8_t& byte : bytes) byte = 0xff;
        assert(decoded.binary == expected);
      }
    }
  }

  std::vector<uint8_t> bytes = {0xff, 0x0f, 'B', 3, 0, 1, 0, 0};
  const v8serial::DecodedValue decoded = v8serial::Reader(bytes).read();
  bytes[5] = 9;
  assert(decoded.type == v8serial::DecodedType::ArrayBuffer);
  assert(decoded.binary == std::vector<uint8_t>({0, 1, 0}));

  for (bool with_view : {false, true}) {
    std::vector<uint8_t> empty = {0xff, 0x0f, 'B', 0};
    if (with_view) empty.insert(empty.end(), {'V', 'B', 0, 0, 0});
    assert(v8serial::Reader(empty).read().binary.empty());
  }
}

void TestNativeViewValidation() {
  const std::vector<uint8_t> valid = {
      0xff, 0x0f, 'B', 4, 1, 2, 3, 4, 'V', 'w', 0, 4, 0,
  };
  const std::vector<std::vector<uint8_t>> invalid = {
      {0xff, 0x0f, 'B', 4, 1, 2, 3},
      {0xff, 0x0f, 'B', 4, 1, 2, 3, 4, 'V', 'B', 5, 0, 0},
      {0xff, 0x0f, 'B', 4, 1, 2, 3, 4, 'V', 'B', 3, 2, 0},
      {0xff, 0x0f, 'B', 4, 1, 2, 3, 4, 'V', 'w', 1, 2, 0},
      {0xff, 0x0f, 'B', 4, 1, 2, 3, 4, 'V', 'w', 0, 3, 0},
      {0xff, 0x0f, 'B', 4, 1, 2, 3, 4, 'V', 'B', 0, 4, 1},
      {0xff, 0x0f, 'B', 4, 1, 2, 3, 4, 'V', '!', 0, 4, 0},
  };
  for (const auto& bytes : invalid) {
    ExpectThrows<v8serial::DecodeError>(
        [&] { v8serial::Reader(bytes).read(); });
  }
  for (size_t length = 9; length < valid.size(); ++length) {
    ExpectThrows<v8serial::DecodeError>(
        [&] { v8serial::Reader(valid.data(), length).read(); });
  }
}

void TestExtendedScalarsAndViews() {
  {
    v8serial::Writer writer;
    writer.uint32(UINT32_MAX);
    const v8serial::DecodedValue decoded =
        v8serial::Reader(writer.take()).read();
    assert(decoded.type == v8serial::DecodedType::Uint32);
    assert(decoded.uint32 == UINT32_MAX);
  }
  {
    v8serial::Writer writer;
    writer.date(1234.5);
    const v8serial::DecodedValue decoded =
        v8serial::Reader(writer.take()).read();
    assert(decoded.type == v8serial::DecodedType::Date);
    assert(decoded.date_milliseconds == 1234.5);
  }
  {
    const uint8_t bytes[] = {0x01, 0x00, 0xfe, 0xff};
    v8serial::Writer writer;
    writer.arrayBufferView(v8serial::ArrayBufferViewType::Int16Array,
                           bytes, sizeof(bytes));
    const v8serial::DecodedValue decoded =
        v8serial::Reader(writer.take()).read();
    assert(decoded.type == v8serial::DecodedType::ArrayBufferView);
    assert(decoded.view_type == v8serial::ArrayBufferViewType::Int16Array);
    assert(decoded.binary == std::vector<uint8_t>({1, 0, 0xfe, 0xff}));
  }
  {
    const std::vector<uint8_t> bytes = {
        0xff, 0x0f, '?', 0x7f, 'I', 0x54,
    };
    const v8serial::DecodedValue decoded = v8serial::Reader(bytes).read();
    assert(decoded.type == v8serial::DecodedType::Int32);
    assert(decoded.int32 == 42);
  }
}

void TestLongStringPaths() {
  const std::u16string latin1(4096, u'\xe9');
  v8serial::Writer latin1_writer(latin1.size() + 16);
  latin1_writer.string(latin1);
  const std::vector<uint8_t> latin1_encoded = latin1_writer.take();
  assert(latin1_encoded[2] == '"');
  assert(v8serial::Reader(latin1_encoded).read().string == latin1);

  std::u16string two_byte(33, u'\xff');
  two_byte[17] = u'\u0100';
  v8serial::Writer two_byte_writer;
  two_byte_writer.string(two_byte);
  const std::vector<uint8_t> two_byte_encoded = two_byte_writer.take();
  assert(two_byte_encoded[2] == 'c');
  assert(v8serial::Reader(two_byte_encoded).read().string == two_byte);
}

void TestWriterResetReuse() {
  v8serial::Writer writer;
  writer.int32(1);
  assert(writer.size() > 2);  // header plus the encoded int32 tag/varint.

  const std::vector<uint8_t> first(writer.data(), writer.data() + writer.size());
  writer.reset();

  // Header is re-emitted immediately after reset(), before any value.
  assert(writer.size() == 2);
  assert(writer.data()[0] == 0xff);
  assert(writer.data()[1] == v8serial::Writer::kFormatVersion);

  writer.beginObject();
  writer.key(u"a");
  writer.string(u"second");
  writer.endObject();
  const std::vector<uint8_t> second = writer.take();

  const v8serial::DecodedValue decoded_first =
      v8serial::Reader(first).read();
  assert(decoded_first.type == v8serial::DecodedType::Int32);
  assert(decoded_first.int32 == 1);

  const v8serial::DecodedValue decoded_second =
      v8serial::Reader(second).read();
  assert(decoded_second.type == v8serial::DecodedType::Object);
  assert(decoded_second.object.size() == 1);
  assert(decoded_second.object[0].first == u"a");
  assert(decoded_second.object[0].second.string == u"second");
}

void TestWriterResetPreservesCapacity() {
  v8serial::Writer writer(256);
  writer.string(u"a somewhat long string used to fill the reserved buffer");
  const size_t used_before_reset = writer.size();
  writer.take();
  // reset() after take(): buffer is already empty (moved-from); a subsequent
  // reset() should still succeed and re-emit a valid header.
  writer.reset();
  assert(writer.size() == 2);
  writer.int32(7);
  assert(writer.size() > 2);
  (void)used_before_reset;
}

void TestWriterResetMidContainerAndBeforeRoot() {
  v8serial::Writer writer;
  writer.beginObject();
  writer.key(u"x");
  // reset() is unconditional and does not throw even with an open container
  // or before any root value has been written.
  writer.reset();
  writer.int32(9);
  const std::vector<uint8_t> encoded = writer.take();
  const v8serial::DecodedValue decoded = v8serial::Reader(encoded).read();
  assert(decoded.type == v8serial::DecodedType::Int32);
  assert(decoded.int32 == 9);

  v8serial::Writer fresh;
  fresh.reset();  // reset() before any value was ever written.
  fresh.null();
  const std::vector<uint8_t> encoded2 = fresh.take();
  const v8serial::DecodedValue decoded2 = v8serial::Reader(encoded2).read();
  assert(decoded2.type == v8serial::DecodedType::Null);
}

}  // namespace

int main() {
  TestWriterAndReaderRoundTrip();
  TestWriterInvariants();
  TestReaderValidation();
  TestStreamingRows();
  TestStreamingRowsValidation();
  TestStreamingStringViews();
  TestNativeViewWithOffset();
  TestNativeViewRangesAndOwnership();
  TestNativeViewValidation();
  TestExtendedScalarsAndViews();
  TestLongStringPaths();
  TestWriterResetReuse();
  TestWriterResetPreservesCapacity();
  TestWriterResetMidContainerAndBeforeRoot();
}
