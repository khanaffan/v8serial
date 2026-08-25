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

}  // namespace

int main() {
  TestWriterAndReaderRoundTrip();
  TestWriterInvariants();
  TestReaderValidation();
  TestNativeViewWithOffset();
}
