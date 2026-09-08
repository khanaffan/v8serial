#include <napi.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "v8serial/reader.hpp"

namespace {

volatile uint64_t sink = 0;

void Mix(uint64_t value) {
  sink = (sink * 1099511628211ULL) ^ value;
}

void ConsumeUndefined() {
  Mix(1);
}

void ConsumeNull() {
  Mix(2);
}

void ConsumeBoolean(bool value) {
  Mix(value ? 3 : 4);
}

void ConsumeNumber(double number) {
  Mix(5);
  uint64_t bits;
  std::memcpy(&bits, &number, sizeof(bits));
  Mix(bits);
}

void ConsumeStringUnit(char16_t code_unit) {
  Mix(code_unit);
}

void BeginString(size_t length) {
  Mix(6);
  Mix(length);
}

void ConsumeUtf16(const std::u16string& text) {
  BeginString(text.size());
  for (char16_t code_unit : text) ConsumeStringUnit(code_unit);
}

void ConsumeAscii(const std::string& text) {
  BeginString(text.size());
  for (unsigned char code_unit : text) ConsumeStringUnit(code_unit);
}

void ConsumeNapiScalar(Napi::Env env, Napi::Value value) {
  if (value.IsUndefined()) {
    ConsumeUndefined();
  } else if (value.IsNull()) {
    ConsumeNull();
  } else if (value.IsBoolean()) {
    ConsumeBoolean(value.As<Napi::Boolean>().Value());
  } else if (value.IsNumber()) {
    ConsumeNumber(value.As<Napi::Number>().DoubleValue());
  } else if (value.IsString()) {
    ConsumeUtf16(value.As<Napi::String>().Utf16Value());
  } else {
    throw Napi::TypeError::New(
        env, "benchmark rows may contain only primitive scalar values");
  }
}

void ConsumeUtf8(const uint8_t* bytes, size_t size) {
  std::vector<uint32_t> code_points;
  code_points.reserve(size);
  const uint8_t* current = bytes;
  const uint8_t* end = bytes + size;
  size_t utf16_length = 0;
  while (current != end) {
    const uint8_t first = *current++;
    uint32_t code_point;
    unsigned continuation_count;
    if (first < 0x80U) {
      code_point = first;
      continuation_count = 0;
    } else if ((first & 0xe0U) == 0xc0U) {
      code_point = first & 0x1fU;
      continuation_count = 1;
    } else if ((first & 0xf0U) == 0xe0U) {
      code_point = first & 0x0fU;
      continuation_count = 2;
    } else {
      code_point = first & 0x07U;
      continuation_count = 3;
    }
    for (unsigned index = 0; index < continuation_count; ++index) {
      code_point = (code_point << 6U) | (*current++ & 0x3fU);
    }
    utf16_length += code_point <= 0xffffU ? 1 : 2;
    code_points.push_back(code_point);
  }

  BeginString(utf16_length);
  for (uint32_t code_point : code_points) {
    if (code_point <= 0xffffU) {
      ConsumeStringUnit(static_cast<char16_t>(code_point));
    } else {
      code_point -= 0x10000U;
      ConsumeStringUnit(
          static_cast<char16_t>(0xd800U + (code_point >> 10U)));
      ConsumeStringUnit(
          static_cast<char16_t>(0xdc00U + (code_point & 0x3ffU)));
    }
  }
}

void ConsumeBorrowedScalar(const v8serial::ScalarValue& value) {
  switch (value.type) {
    case v8serial::ScalarType::Undefined:
      ConsumeUndefined();
      break;
    case v8serial::ScalarType::Null:
      ConsumeNull();
      break;
    case v8serial::ScalarType::Boolean:
      ConsumeBoolean(value.boolean);
      break;
    case v8serial::ScalarType::Int32:
      ConsumeNumber(value.int32);
      break;
    case v8serial::ScalarType::Uint32:
      ConsumeNumber(value.uint32);
      break;
    case v8serial::ScalarType::Double:
      ConsumeNumber(value.number);
      break;
    case v8serial::ScalarType::Latin1String:
      BeginString(value.byte_count);
      for (uint32_t index = 0; index < value.byte_count; ++index) {
        ConsumeStringUnit(value.bytes[index]);
      }
      break;
    case v8serial::ScalarType::Utf8String:
      ConsumeUtf8(value.bytes, value.byte_count);
      break;
    case v8serial::ScalarType::Utf16String:
      BeginString(value.byte_count / 2);
      for (uint32_t index = 0; index < value.byte_count; index += 2) {
        char16_t code_unit;
        std::memcpy(&code_unit, value.bytes + index, sizeof(code_unit));
        ConsumeStringUnit(code_unit);
      }
      break;
  }
}

void ConsumeDecodedScalar(const v8serial::DecodedValue& value) {
  switch (value.type) {
    case v8serial::DecodedType::Undefined:
      ConsumeUndefined();
      return;
    case v8serial::DecodedType::Null:
      ConsumeNull();
      return;
    case v8serial::DecodedType::Boolean:
      ConsumeBoolean(value.boolean);
      return;
    case v8serial::DecodedType::Int32:
      ConsumeNumber(value.int32);
      return;
    case v8serial::DecodedType::Uint32:
      ConsumeNumber(value.uint32);
      return;
    case v8serial::DecodedType::Double:
      ConsumeNumber(value.number);
      return;
    case v8serial::DecodedType::String:
      ConsumeUtf16(value.string);
      return;
    case v8serial::DecodedType::Date:
    case v8serial::DecodedType::Array:
    case v8serial::DecodedType::Object:
    case v8serial::DecodedType::ArrayBuffer:
    case v8serial::DecodedType::Uint8Array:
    case v8serial::DecodedType::ArrayBufferView:
      throw std::runtime_error(
          "benchmark rows may contain only primitive scalar values");
  }
}

void ConsumeDecodedRows(const v8serial::DecodedValue& root) {
  if (root.type != v8serial::DecodedType::Array) {
    throw std::runtime_error("serialized benchmark root must be an array");
  }
  size_t expected_columns = 0;
  for (size_t row_index = 0; row_index < root.array.size(); ++row_index) {
    const auto& row = root.array[row_index];
    if (row.type != v8serial::DecodedType::Array) {
      throw std::runtime_error("serialized benchmark row must be an array");
    }
    if (row_index != 0 && row.array.size() != expected_columns) {
      throw std::runtime_error("serialized benchmark rows have inconsistent widths");
    }
    expected_columns = row.array.size();
    for (const auto& value : row.array) ConsumeDecodedScalar(value);
    Mix(0x53544550U);
  }
}

struct ByteView {
  const uint8_t* data;
  size_t size;
};

ByteView GetByteView(const Napi::CallbackInfo& info, size_t index) {
  const Napi::Env env = info.Env();
  if (index >= info.Length()) {
    throw Napi::TypeError::New(env, "serialized input is required");
  }

  const Napi::Value input = info[index];
  if (input.IsBuffer()) {
    auto buffer = input.As<Napi::Buffer<uint8_t>>();
    return {buffer.Data(), buffer.Length()};
  }
  if (input.IsArrayBuffer()) {
    auto buffer = input.As<Napi::ArrayBuffer>();
    if (buffer.IsDetached()) {
      throw Napi::TypeError::New(env, "serialized input is detached");
    }
    return {static_cast<const uint8_t*>(buffer.Data()), buffer.ByteLength()};
  }
  if (input.IsTypedArray()) {
    auto typed = input.As<Napi::TypedArray>();
    if (typed.ArrayBuffer().IsDetached()) {
      throw Napi::TypeError::New(env, "serialized input is detached");
    }
    void* data = nullptr;
    const napi_status status = napi_get_typedarray_info(
        env, input, nullptr, nullptr, &data, nullptr, nullptr);
    if (status != napi_ok) {
      throw Napi::Error::New(env, "failed to inspect serialized input");
    }
    return {static_cast<const uint8_t*>(data), typed.ByteLength()};
  }
  throw Napi::TypeError::New(
      env, "serialized input must be a Buffer, Uint8Array, or ArrayBuffer");
}

Napi::Value ResetSink(const Napi::CallbackInfo& info) {
  sink = 0;
  return info.Env().Undefined();
}

Napi::Value GetSink(const Napi::CallbackInfo& info) {
  return Napi::BigInt::New(info.Env(), static_cast<uint64_t>(sink));
}

Napi::Value ConsumeCell(const Napi::CallbackInfo& info) {
  if (info.Length() != 1) {
    throw Napi::TypeError::New(info.Env(), "consumeCell expects one value");
  }
  ConsumeNapiScalar(info.Env(), info[0]);
  return info.Env().Undefined();
}

Napi::Value ConsumeStep(const Napi::CallbackInfo& info) {
  Mix(0x53544550U);
  return info.Env().Undefined();
}

Napi::Value ConsumeArgs(const Napi::CallbackInfo& info) {
  for (size_t index = 0; index < info.Length(); ++index) {
    ConsumeNapiScalar(info.Env(), info[index]);
  }
  return info.Env().Undefined();
}

Napi::Array RequireRows(const Napi::CallbackInfo& info) {
  if (info.Length() != 1 || !info[0].IsArray()) {
    throw Napi::TypeError::New(info.Env(), "rows must be an array");
  }
  return info[0].As<Napi::Array>();
}

Napi::Value ConsumeRowsGeneric(const Napi::CallbackInfo& info) {
  const Napi::Env env = info.Env();
  const Napi::Array rows = RequireRows(info);
  for (uint32_t row_index = 0; row_index < rows.Length(); ++row_index) {
    if (!rows.Has(row_index)) {
      throw Napi::TypeError::New(env, "rows must not contain holes");
    }
    const Napi::Value row_value = rows.Get(row_index);
    if (!row_value.IsArray()) {
      throw Napi::TypeError::New(env, "each row must be an array");
    }
    const Napi::Array row = row_value.As<Napi::Array>();
    for (uint32_t column_index = 0; column_index < row.Length();
         ++column_index) {
      if (!row.Has(column_index)) {
        throw Napi::TypeError::New(env, "rows must not contain holes");
      }
      ConsumeNapiScalar(env, row.Get(column_index));
    }
    Mix(0x53544550U);
  }
  return env.Undefined();
}

Napi::Value ConsumeRowsOptimized(const Napi::CallbackInfo& info) {
  const Napi::Env env = info.Env();
  const Napi::Array rows = RequireRows(info);
  const uint32_t row_count = rows.Length();
  uint32_t expected_columns = 0;
  for (uint32_t row_index = 0; row_index < row_count; ++row_index) {
    const Napi::Value row_value = rows.Get(row_index);
    if (!row_value.IsArray()) {
      throw Napi::TypeError::New(env, "each row must be an array");
    }
    const Napi::Array row = row_value.As<Napi::Array>();
    const uint32_t column_count = row.Length();
    if (row_index != 0 && column_count != expected_columns) {
      throw Napi::TypeError::New(env, "rows must have consistent widths");
    }
    expected_columns = column_count;
    for (uint32_t column_index = 0; column_index < column_count;
         ++column_index) {
      ConsumeNapiScalar(env, row.Get(column_index));
    }
    Mix(0x53544550U);
  }
  return env.Undefined();
}

Napi::Value ConsumeObjectRows(const Napi::CallbackInfo& info) {
  const Napi::Env env = info.Env();
  const Napi::Array rows = RequireRows(info);
  const uint32_t row_count = rows.Length();
  for (uint32_t row_index = 0; row_index < row_count; ++row_index) {
    const Napi::Value row_value = rows.Get(row_index);
    if (!row_value.IsObject() || row_value.IsArray()) {
      throw Napi::TypeError::New(env, "each row must be an object");
    }
    const Napi::Object row = row_value.As<Napi::Object>();
    const Napi::Array names = row.GetPropertyNames();
    const uint32_t column_count = names.Length();
    for (uint32_t column_index = 0; column_index < column_count;
         ++column_index) {
      const Napi::Value key = names.Get(column_index);
      ConsumeNapiScalar(env, row.Get(key));
    }
    Mix(0x53544550U);
  }
  return env.Undefined();
}

template <typename Operation>
Napi::Value ConsumeSerialized(const Napi::CallbackInfo& info,
                              Operation&& operation) {
  try {
    operation(GetByteView(info, 0));
    return info.Env().Undefined();
  } catch (const v8serial::DecodeError& error) {
    throw Napi::Error::New(info.Env(), error.what());
  } catch (const std::exception& error) {
    throw Napi::Error::New(info.Env(), error.what());
  }
}

Napi::Value ConsumeSerializedRows(const Napi::CallbackInfo& info) {
  return ConsumeSerialized(info, [](const ByteView& input) {
    v8serial::Reader(input.data, input.size)
        .readRows(0, [](uint32_t, uint32_t column, uint32_t column_count,
                        const v8serial::ScalarValue& value) {
          ConsumeBorrowedScalar(value);
          if (column + 1 == column_count) Mix(0x53544550U);
        });
  });
}

Napi::Value ConsumeSerializedRowsCopied(const Napi::CallbackInfo& info) {
  return ConsumeSerialized(info, [](const ByteView& input) {
    const std::vector<uint8_t> copy(input.data, input.data + input.size);
    v8serial::Reader(copy).readRows(
        0, [](uint32_t, uint32_t column, uint32_t column_count,
              const v8serial::ScalarValue& value) {
          ConsumeBorrowedScalar(value);
          if (column + 1 == column_count) Mix(0x53544550U);
        });
  });
}

Napi::Value ConsumeSerializedTree(const Napi::CallbackInfo& info) {
  return ConsumeSerialized(info, [](const ByteView& input) {
    const v8serial::DecodedValue value =
        v8serial::Reader(input.data, input.size).read();
    ConsumeDecodedRows(value);
  });
}

Napi::Value ConsumeNativeRows(const Napi::CallbackInfo& info) {
  const Napi::Env env = info.Env();
  if (info.Length() != 2 || !info[0].IsNumber() || !info[1].IsNumber()) {
    throw Napi::TypeError::New(env, "row and column counts are required");
  }
  const uint32_t rows = info[0].As<Napi::Number>().Uint32Value();
  const uint32_t columns = info[1].As<Napi::Number>().Uint32Value();
  for (uint32_t row = 0; row < rows; ++row) {
    const std::string text = "row-" + std::to_string(row & 1023U);
    for (uint32_t column = 0; column < columns; ++column) {
      switch (column % 5) {
        case 0:
          ConsumeNumber(row);
          break;
        case 1:
          ConsumeAscii(text);
          break;
        case 2:
          ConsumeNumber(row * 0.25 + 0.5);
          break;
        case 3:
          ConsumeBoolean((row & 1U) == 0);
          break;
        default:
          if (row % 17U == 0) {
            ConsumeNull();
          } else {
            ConsumeNumber(row);
          }
          break;
      }
    }
    Mix(0x53544550U);
  }
  return env.Undefined();
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports.Set("resetSink", Napi::Function::New(env, ResetSink));
  exports.Set("getSink", Napi::Function::New(env, GetSink));
  exports.Set("consumeCell", Napi::Function::New(env, ConsumeCell));
  exports.Set("consumeStep", Napi::Function::New(env, ConsumeStep));
  exports.Set("consumeArgs", Napi::Function::New(env, ConsumeArgs));
  exports.Set("consumeRowsGeneric",
              Napi::Function::New(env, ConsumeRowsGeneric));
  exports.Set("consumeRowsOptimized",
              Napi::Function::New(env, ConsumeRowsOptimized));
  exports.Set("consumeObjectRows",
              Napi::Function::New(env, ConsumeObjectRows));
  exports.Set("consumeSerializedRows",
              Napi::Function::New(env, ConsumeSerializedRows));
  exports.Set("consumeSerializedRowsCopied",
              Napi::Function::New(env, ConsumeSerializedRowsCopied));
  exports.Set("consumeSerializedTree",
              Napi::Function::New(env, ConsumeSerializedTree));
  exports.Set("consumeNativeRows",
              Napi::Function::New(env, ConsumeNativeRows));
  return exports;
}

}  // namespace

NODE_API_MODULE(v8serial_bench_napi, Init)
