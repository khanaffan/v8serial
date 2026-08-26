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

void TestNativeViewWithOffset() {
  const std::vector<uint8_t> bytes = {
      0xff, 0x0f, 'B', 0x05, 9, 1, 2, 3, 9,
      'V',  'B',  0x01, 0x03, 0x00,
  };
  const v8serial::DecodedValue decoded = v8serial::Reader(bytes).read();
  assert(decoded.type == v8serial::DecodedType::Uint8Array);
  assert(decoded.binary == std::vector<uint8_t>({1, 2, 3}));
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

}  // namespace

int main() {
  TestWriterAndReaderRoundTrip();
  TestWriterInvariants();
  TestReaderValidation();
  TestNativeViewWithOffset();
  TestExtendedScalarsAndViews();
  TestLongStringPaths();
}
