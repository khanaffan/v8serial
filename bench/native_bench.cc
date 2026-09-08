#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "v8serial/reader.hpp"
#include "v8serial/writer.hpp"

namespace {

using Clock = std::chrono::steady_clock;
using Encode = std::vector<uint8_t> (*)();
using ConsumeBytes = void (*)(const uint8_t*, size_t);

volatile uint64_t sink = 0;

void ConsumeEncodedBytes(const uint8_t* data, size_t size) {
  sink += size;
  if (size == 0) return;
  sink += data[0];
  sink += data[size / 2];
  sink += data[size - 1];
}

void WriteNumber(v8serial::Writer& writer, double value) {
  if (std::isfinite(value) &&
      value >= std::numeric_limits<int32_t>::min() &&
      value <= std::numeric_limits<int32_t>::max() &&
      std::trunc(value) == value &&
      !(value == 0.0 && std::signbit(value))) {
    writer.int32(static_cast<int32_t>(value));
  } else {
    writer.number(value);
  }
}

void WritePoint3d(v8serial::Writer& writer, double x, double y, double z) {
  writer.beginObject();
  writer.key(u"x");
  WriteNumber(writer, x);
  writer.key(u"y");
  WriteNumber(writer, y);
  writer.key(u"z");
  WriteNumber(writer, z);
  writer.endObject();
}

void WriteGeometry(v8serial::Writer& writer, const uint8_t* blob,
                   size_t blob_size, bool include_blob) {
  writer.beginObject();
  writer.key(u"id");
  writer.int32(42);
  writer.key(u"name");
  writer.string(u"structural-member-42");
  writer.key(u"category");
  writer.string(u"beam");
  writer.key(u"material");
  writer.string(u"steel");
  writer.key(u"origin2d");
  writer.beginObject();
  writer.key(u"x");
  WriteNumber(writer, 12.25);
  writer.key(u"y");
  WriteNumber(writer, -8.5);
  writer.endObject();
  writer.key(u"position3d");
  WritePoint3d(writer, 12.25, -8.5, 104.75);
  writer.key(u"rect");
  writer.beginObject();
  writer.key(u"left");
  writer.int32(10);
  writer.key(u"top");
  writer.int32(20);
  writer.key(u"right");
  writer.int32(310);
  writer.key(u"bottom");
  writer.int32(220);
  writer.endObject();
  writer.key(u"bounds");
  writer.beginObject();
  writer.key(u"low");
  WritePoint3d(writer, -5.5, -6.25, 0);
  writer.key(u"high");
  WritePoint3d(writer, 125.75, 80.5, 210.25);
  writer.endObject();
  writer.key(u"description");
  writer.string(u"Exterior frame member with connection metadata");
  if (include_blob) {
    writer.key(u"blob");
    writer.uint8Array(blob, blob_size);
  }
  writer.endObject();
}

const std::vector<uint8_t>& Blob(size_t size) {
  static const std::vector<uint8_t> blob_64 = [] {
    std::vector<uint8_t> output(64);
    for (size_t index = 0; index < output.size(); ++index) {
      output[index] = static_cast<uint8_t>((index * 17U) & 0xffU);
    }
    return output;
  }();
  static const std::vector<uint8_t> blob_1k = [] {
    std::vector<uint8_t> output(1024);
    for (size_t index = 0; index < output.size(); ++index) {
      output[index] = static_cast<uint8_t>((index * 17U) & 0xffU);
    }
    return output;
  }();
  static const std::vector<uint8_t> blob_1m = [] {
    std::vector<uint8_t> output(1024 * 1024);
    for (size_t index = 0; index < output.size(); ++index) {
      output[index] = static_cast<uint8_t>((index * 17U) & 0xffU);
    }
    return output;
  }();
  if (size == 64) return blob_64;
  if (size == 1024) return blob_1k;
  return blob_1m;
}

std::vector<uint8_t> EncodeScalar() {
  v8serial::Writer writer(16);
  writer.int32(42);
  return writer.take();
}

std::vector<uint8_t> EncodePoint3d() {
  v8serial::Writer writer(64);
  WritePoint3d(writer, 12.25, -8.5, 104.75);
  return writer.take();
}

std::vector<uint8_t> EncodeGeometry() {
  v8serial::Writer writer(512);
  WriteGeometry(writer, nullptr, 0, false);
  return writer.take();
}

std::vector<uint8_t> EncodeGeometry64() {
  const std::vector<uint8_t>& blob = Blob(64);
  v8serial::Writer writer(512);
  WriteGeometry(writer, blob.data(), blob.size(), true);
  return writer.take();
}

std::vector<uint8_t> EncodePoints100() {
  v8serial::Writer writer(8192);
  writer.beginArray(100);
  for (int32_t index = 0; index < 100; ++index) {
    WritePoint3d(writer, index * 1.25, index * -0.5, index * 2.0);
  }
  writer.endArray();
  return writer.take();
}

std::vector<uint8_t> EncodeLatin1() {
  static const std::u16string text(4096, u'\xe9');
  v8serial::Writer writer(text.size() + 16);
  writer.string(text);
  return writer.take();
}

template <size_t Length, size_t NonLatin1 = Length>
std::vector<uint8_t> EncodeString() {
  static const std::u16string text = [] {
    std::u16string value(Length, u'\xe9');
    if (NonLatin1 < Length) value[NonLatin1] = u'\u0100';
    return value;
  }();
  v8serial::Writer writer(text.size() + 16);
  writer.string(text);
  return writer.take();
}

std::vector<uint8_t> EncodeBlob(size_t size) {
  const std::vector<uint8_t>& blob = Blob(size);
  v8serial::Writer writer(size + 32);
  writer.beginObject();
  writer.key(u"id");
  writer.int32(42);
  writer.key(u"blob");
  writer.uint8Array(blob.data(), blob.size());
  writer.endObject();
  return writer.take();
}

std::vector<uint8_t> EncodeBlob1k() { return EncodeBlob(1024); }
std::vector<uint8_t> EncodeBlob1m() { return EncodeBlob(1024 * 1024); }

struct Scenario {
  const char* name;
  Encode encode;
  size_t iterations;
};

template <typename Operation>
double MedianNanoseconds(size_t iterations, Operation operation) {
  constexpr size_t kRounds = 7;
  std::vector<double> samples;
  samples.reserve(kRounds);
  for (size_t round = 0; round < kRounds; ++round) {
    const auto start = Clock::now();
    for (size_t index = 0; index < iterations; ++index) operation();
    const auto elapsed = Clock::now() - start;
    samples.push_back(
        std::chrono::duration<double, std::nano>(elapsed).count() / iterations);
  }
  std::sort(samples.begin(), samples.end());
  return samples[kRounds / 2];
}

struct WriterReuseResult {
  double fresh_ns;
  double reused_ns;
  size_t wire_bytes;
};

template <typename Write>
WriterReuseResult BenchmarkWriterReuse(Encode fresh_encode,
                                       size_t initial_capacity,
                                       size_t iterations, Write write) {
  constexpr size_t kWarmupIterations = 1000;
  // Prevent release builds from scalar-replacing the one-shot vector and
  // eliding the allocation/free lifecycle this benchmark is intended to time.
  Encode volatile opaque_fresh_encode = fresh_encode;
  ConsumeBytes volatile opaque_consume = ConsumeEncodedBytes;
  for (size_t index = 0; index < kWarmupIterations; ++index) {
    const std::vector<uint8_t> bytes = opaque_fresh_encode();
    opaque_consume(bytes.data(), bytes.size());
  }

  v8serial::Writer reused_writer(initial_capacity);
  for (size_t index = 0; index < kWarmupIterations; ++index) {
    reused_writer.reset();
    write(reused_writer);
    opaque_consume(reused_writer.data(), reused_writer.size());
  }

  const double fresh_ns = MedianNanoseconds(iterations, [&] {
    const std::vector<uint8_t> bytes = opaque_fresh_encode();
    opaque_consume(bytes.data(), bytes.size());
  });
  const double reused_ns = MedianNanoseconds(iterations, [&] {
    reused_writer.reset();
    write(reused_writer);
    opaque_consume(reused_writer.data(), reused_writer.size());
  });
  return {fresh_ns, reused_ns, reused_writer.size()};
}

void Run(const Scenario& scenario) {
  const std::vector<uint8_t> encoded = scenario.encode();
  for (size_t index = 0; index < 1000; ++index) {
    sink += scenario.encode().size();
    sink += v8serial::Reader(encoded).read().binary.size();
  }

  const double encode_ns = MedianNanoseconds(scenario.iterations, [&] {
    const std::vector<uint8_t> bytes = scenario.encode();
    sink += bytes.size();
  });
  const double decode_ns = MedianNanoseconds(scenario.iterations, [&] {
    const v8serial::DecodedValue value = v8serial::Reader(encoded).read();
    sink += static_cast<uint64_t>(value.type);
  });

  std::cout << scenario.name << '\t' << encode_ns << '\t' << decode_ns << '\t'
            << encoded.size() << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--strings") {
    const Scenario strings[] = {
        {"latin1-0", EncodeString<0>, 100000},
        {"latin1-1", EncodeString<1>, 100000},
        {"latin1-7", EncodeString<7>, 100000},
        {"latin1-8", EncodeString<8>, 100000},
        {"latin1-15", EncodeString<15>, 100000},
        {"latin1-16", EncodeString<16>, 100000},
        {"latin1-31", EncodeString<31>, 100000},
        {"latin1-32", EncodeString<32>, 100000},
        {"latin1-33", EncodeString<33>, 100000},
        {"latin1-64", EncodeString<64>, 100000},
        {"latin1-4k", EncodeString<4096>, 50000},
        {"latin1-64k", EncodeString<65536>, 5000},
        {"utf16-first-33", EncodeString<33, 0>, 100000},
        {"utf16-last-33", EncodeString<33, 32>, 100000},
        {"utf16-first-4k", EncodeString<4096, 0>, 50000},
        {"utf16-last-4k", EncodeString<4096, 4095>, 50000},
    };
    for (const Scenario& scenario : strings) Run(scenario);
    return sink == 0 ? 1 : 0;
  }
  if (argc != 1) {
    std::cerr << "usage: v8serial_native_bench [--strings]\n";
    return 1;
  }
  const Scenario scenarios[] = {
      {"scalar", EncodeScalar, 500000},
      {"point3d", EncodePoint3d, 200000},
      {"geometry", EncodeGeometry, 30000},
      {"geometry-64b", EncodeGeometry64, 30000},
      {"points-100", EncodePoints100, 2000},
      {"latin1-4k", EncodeLatin1, 50000},
      {"blob-1k", EncodeBlob1k, 20000},
      {"blob-1m", EncodeBlob1m, 50},
  };

  for (const Scenario& scenario : scenarios) Run(scenario);

  const WriterReuseResult scalar_reuse =
      BenchmarkWriterReuse(EncodeScalar, 16, 500000,
                           [](v8serial::Writer& writer) { writer.int32(42); });
  const WriterReuseResult geometry_reuse =
      BenchmarkWriterReuse(EncodeGeometry, 512, 30000,
                           [](v8serial::Writer& writer) {
                             WriteGeometry(writer, nullptr, 0, false);
                           });
  std::cout << "writer-reuse-scalar\t" << scalar_reuse.fresh_ns << '\t'
            << scalar_reuse.reused_ns << '\t' << scalar_reuse.wire_bytes
            << '\n';
  std::cout << "writer-reuse-geometry\t" << geometry_reuse.fresh_ns << '\t'
            << geometry_reuse.reused_ns << '\t' << geometry_reuse.wire_bytes
            << '\n';

  return sink == 0 ? 1 : 0;
}
