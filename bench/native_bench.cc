#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "v8serial/reader.hpp"
#include "v8serial/writer.hpp"

namespace {

using Clock = std::chrono::steady_clock;
using Encode = std::vector<uint8_t> (*)();

volatile uint64_t sink = 0;

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

double BenchGeometryFreshWriter(size_t iterations) {
  std::vector<double> samples;
  constexpr size_t kRounds = 7;
  samples.reserve(kRounds);
  for (size_t round = 0; round < kRounds; ++round) {
    const auto start = Clock::now();
    for (size_t index = 0; index < iterations; ++index) {
      v8serial::Writer writer(512);
      WriteGeometry(writer, nullptr, 0, false);
      const std::vector<uint8_t> bytes = writer.take();
      sink += bytes.size();
    }
    const auto elapsed = Clock::now() - start;
    samples.push_back(
        std::chrono::duration<double, std::nano>(elapsed).count() / iterations);
  }
  std::sort(samples.begin(), samples.end());
  return samples[kRounds / 2];
}

double BenchGeometryReusedWriter(size_t iterations) {
  std::vector<double> samples;
  constexpr size_t kRounds = 7;
  samples.reserve(kRounds);
  v8serial::Writer writer(512);
  for (size_t round = 0; round < kRounds; ++round) {
    const auto start = Clock::now();
    for (size_t index = 0; index < iterations; ++index) {
      writer.reset();
      WriteGeometry(writer, nullptr, 0, false);
      sink += writer.size();
    }
    const auto elapsed = Clock::now() - start;
    samples.push_back(
        std::chrono::duration<double, std::nano>(elapsed).count() / iterations);
  }
  std::sort(samples.begin(), samples.end());
  return samples[kRounds / 2];
}

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

int main() {
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

  constexpr size_t kReuseIterations = 30000;
  const double fresh_ns = BenchGeometryFreshWriter(kReuseIterations);
  const double reused_ns = BenchGeometryReusedWriter(kReuseIterations);
  std::cout << "geometry-writer-fresh\t" << fresh_ns << "\t-\t-\n";
  std::cout << "geometry-writer-reused\t" << reused_ns << "\t-\t-\n";

  return sink == 0 ? 1 : 0;
}
