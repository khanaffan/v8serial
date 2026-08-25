#include <napi.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "v8serial/writer.hpp"
#include "v8serial/reader.hpp"

namespace {

constexpr size_t kMaxNestingDepth = 512;

enum class Type {
  Undefined,
  Null,
  Boolean,
  Int32,
  Double,
  String,
  Array,
  Object,
  ArrayBuffer,
  Uint8Array,
};

struct Value {
  Type type = Type::Undefined;
  bool boolean = false;
  int32_t int32 = 0;
  double number = 0;
  std::u16string string;
  std::vector<Value> array;
  std::vector<std::pair<std::u16string, Value>> object;
  std::vector<uint8_t> binary;
};

bool IsAncestor(Napi::Env env, Napi::Value candidate,
                const std::vector<Napi::Object>& ancestors) {
  for (const Napi::Object& ancestor : ancestors) {
    bool equal = false;
    napi_status status =
        napi_strict_equals(env, candidate, ancestor, &equal);
    if (status != napi_ok) {
      throw Napi::Error::New(env, "failed to compare object identity");
    }
    if (equal) return true;
  }
  return false;
}

std::u16string ToUtf16(const Napi::String& value) {
  return value.Utf16Value();
}

Napi::Array OwnEnumerableNames(Napi::Env env, Napi::Object object) {
  napi_value names_value;
  napi_status status = napi_get_all_property_names(
      env, object, napi_key_own_only, napi_key_enumerable,
      napi_key_numbers_to_strings, &names_value);
  if (status != napi_ok) {
    throw Napi::Error::New(env, "failed to enumerate object properties");
  }
  return Napi::Array(env, names_value);
}

Napi::Value PlainObjectPrototype(Napi::Env env) {
  return env.Global()
      .Get("Object")
      .As<Napi::Object>()
      .Get("prototype");
}

bool HasPlainPrototype(Napi::Env env, Napi::Object object,
                       Napi::Value plain_prototype) {
  napi_value prototype_value;
  if (napi_get_prototype(env, object, &prototype_value) != napi_ok) {
    throw Napi::Error::New(env, "failed to inspect object prototype");
  }
  Napi::Value prototype(env, prototype_value);
  if (prototype.IsNull()) return true;

  bool equal = false;
  if (napi_strict_equals(env, prototype, plain_prototype, &equal) != napi_ok) {
    throw Napi::Error::New(env, "failed to compare object prototype");
  }
  return equal;
}

Value CopyValue(Napi::Env env, Napi::Value input,
                std::vector<Napi::Object>& ancestors,
                Napi::Value plain_prototype, size_t depth) {
  if (depth > kMaxNestingDepth) {
    throw Napi::RangeError::New(env, "maximum nesting depth exceeded");
  }

  Value output;
  if (input.IsUndefined()) return output;
  if (input.IsNull()) {
    output.type = Type::Null;
    return output;
  }
  if (input.IsBoolean()) {
    output.type = Type::Boolean;
    output.boolean = input.As<Napi::Boolean>().Value();
    return output;
  }
  if (input.IsNumber()) {
    output.number = input.As<Napi::Number>().DoubleValue();
    if (std::isfinite(output.number) &&
        output.number >= std::numeric_limits<int32_t>::min() &&
        output.number <= std::numeric_limits<int32_t>::max() &&
        std::trunc(output.number) == output.number &&
        !(output.number == 0.0 && std::signbit(output.number))) {
      output.type = Type::Int32;
      output.int32 = static_cast<int32_t>(output.number);
    } else {
      output.type = Type::Double;
    }
    return output;
  }
  if (input.IsString()) {
    output.type = Type::String;
    output.string = ToUtf16(input.As<Napi::String>());
    return output;
  }

  if (!input.IsObject()) {
    throw Napi::TypeError::New(
        env, "unsupported value; expected core scalar, array, plain object, "
             "ArrayBuffer, or Uint8Array");
  }
  if (IsAncestor(env, input, ancestors)) {
    throw Napi::TypeError::New(env, "cyclic values are not supported");
  }

  Napi::Object object = input.As<Napi::Object>();
  ancestors.push_back(object);

  if (input.IsArrayBuffer()) {
    Napi::ArrayBuffer buffer = input.As<Napi::ArrayBuffer>();
    const auto* data = static_cast<const uint8_t*>(buffer.Data());
    output.type = Type::ArrayBuffer;
    output.binary.assign(data, data + buffer.ByteLength());
  } else if (input.IsTypedArray()) {
    Napi::TypedArray typed = input.As<Napi::TypedArray>();
    if (typed.TypedArrayType() != napi_uint8_array) {
      ancestors.pop_back();
      throw Napi::TypeError::New(env, "only Uint8Array is supported");
    }
    Napi::Uint8Array bytes = input.As<Napi::Uint8Array>();
    output.type = Type::Uint8Array;
    output.binary.assign(bytes.Data(), bytes.Data() + bytes.ElementLength());
  } else if (input.IsArray()) {
    Napi::Array array = input.As<Napi::Array>();
    output.type = Type::Array;
    output.array.reserve(array.Length());
    for (uint32_t index = 0; index < array.Length(); ++index) {
      if (!array.Has(index)) {
        ancestors.pop_back();
        throw Napi::TypeError::New(env, "sparse arrays are not supported");
      }
      output.array.push_back(
          CopyValue(env, array.Get(index), ancestors, plain_prototype,
                    depth + 1));
    }
    if (OwnEnumerableNames(env, array).Length() != array.Length()) {
      ancestors.pop_back();
      throw Napi::TypeError::New(
          env, "named or symbol array properties are not supported");
    }
  } else {
    if (!HasPlainPrototype(env, object, plain_prototype)) {
      ancestors.pop_back();
      throw Napi::TypeError::New(env, "only plain objects are supported");
    }
    Napi::Array names = OwnEnumerableNames(env, object);
    output.type = Type::Object;
    output.object.reserve(names.Length());
    for (uint32_t index = 0; index < names.Length(); ++index) {
      Napi::Value key_value = names.Get(index);
      if (!key_value.IsString()) {
        ancestors.pop_back();
        throw Napi::TypeError::New(env, "symbol properties are not supported");
      }
      Napi::String key = key_value.As<Napi::String>();
      output.object.emplace_back(ToUtf16(key),
                                 CopyValue(env, object.Get(key), ancestors,
                                           plain_prototype, depth + 1));
    }
  }

  ancestors.pop_back();
  return output;
}

void WriteValue(v8serial::Writer& writer, const Value& value, size_t depth) {
  if (depth > kMaxNestingDepth) {
    throw std::length_error("maximum nesting depth exceeded");
  }

  switch (value.type) {
    case Type::Undefined:
      writer.undefined();
      break;
    case Type::Null:
      writer.null();
      break;
    case Type::Boolean:
      writer.boolean(value.boolean);
      break;
    case Type::Int32:
      writer.int32(value.int32);
      break;
    case Type::Double:
      writer.number(value.number);
      break;
    case Type::String:
      writer.string(value.string);
      break;
    case Type::Array:
      writer.beginArray(static_cast<uint32_t>(value.array.size()));
      for (const Value& element : value.array) {
        WriteValue(writer, element, depth + 1);
      }
      writer.endArray();
      break;
    case Type::Object:
      writer.beginObject();
      for (const auto& [key, child] : value.object) {
        writer.key(key);
        WriteValue(writer, child, depth + 1);
      }
      writer.endObject();
      break;
    case Type::ArrayBuffer:
      writer.arrayBuffer(value.binary.data(), value.binary.size());
      break;
    case Type::Uint8Array:
      writer.uint8Array(value.binary.data(), value.binary.size());
      break;
  }
}

std::vector<uint8_t> Encode(const Value& value) {
  v8serial::Writer writer;
  WriteValue(writer, value, 0);
  return writer.take();
}

void WriteJavaScriptValue(Napi::Env env, Napi::Value input,
                          v8serial::Writer& writer,
                          std::vector<Napi::Object>& ancestors,
                          Napi::Value plain_prototype, size_t depth) {
  if (depth > kMaxNestingDepth) {
    throw Napi::RangeError::New(env, "maximum nesting depth exceeded");
  }

  if (input.IsUndefined()) {
    writer.undefined();
    return;
  }
  if (input.IsNull()) {
    writer.null();
    return;
  }
  if (input.IsBoolean()) {
    writer.boolean(input.As<Napi::Boolean>().Value());
    return;
  }
  if (input.IsNumber()) {
    const double number = input.As<Napi::Number>().DoubleValue();
    if (std::isfinite(number) &&
        number >= std::numeric_limits<int32_t>::min() &&
        number <= std::numeric_limits<int32_t>::max() &&
        std::trunc(number) == number &&
        !(number == 0.0 && std::signbit(number))) {
      writer.int32(static_cast<int32_t>(number));
    } else {
      writer.number(number);
    }
    return;
  }
  if (input.IsString()) {
    writer.string(ToUtf16(input.As<Napi::String>()));
    return;
  }
  if (!input.IsObject()) {
    throw Napi::TypeError::New(
        env, "unsupported value; expected core scalar, array, plain object, "
             "ArrayBuffer, or Uint8Array");
  }
  if (IsAncestor(env, input, ancestors)) {
    throw Napi::TypeError::New(env, "cyclic values are not supported");
  }

  Napi::Object object = input.As<Napi::Object>();
  ancestors.push_back(object);

  if (input.IsArrayBuffer()) {
    Napi::ArrayBuffer buffer = input.As<Napi::ArrayBuffer>();
    writer.arrayBuffer(static_cast<const uint8_t*>(buffer.Data()),
                       buffer.ByteLength());
  } else if (input.IsTypedArray()) {
    Napi::TypedArray typed = input.As<Napi::TypedArray>();
    if (typed.TypedArrayType() != napi_uint8_array) {
      throw Napi::TypeError::New(env, "only Uint8Array is supported");
    }
    Napi::Uint8Array bytes = input.As<Napi::Uint8Array>();
    writer.uint8Array(bytes.Data(), bytes.ElementLength());
  } else if (input.IsArray()) {
    Napi::Array array = input.As<Napi::Array>();
    writer.beginArray(array.Length());
    for (uint32_t index = 0; index < array.Length(); ++index) {
      if (!array.Has(index)) {
        throw Napi::TypeError::New(env, "sparse arrays are not supported");
      }
      WriteJavaScriptValue(env, array.Get(index), writer, ancestors,
                           plain_prototype, depth + 1);
    }
    if (OwnEnumerableNames(env, array).Length() != array.Length()) {
      throw Napi::TypeError::New(
          env, "named or symbol array properties are not supported");
    }
    writer.endArray();
  } else {
    if (!HasPlainPrototype(env, object, plain_prototype)) {
      throw Napi::TypeError::New(env, "only plain objects are supported");
    }
    Napi::Array names = OwnEnumerableNames(env, object);
    writer.beginObject();
    for (uint32_t index = 0; index < names.Length(); ++index) {
      Napi::Value key_value = names.Get(index);
      if (!key_value.IsString()) {
        throw Napi::TypeError::New(env, "symbol properties are not supported");
      }
      Napi::String key = key_value.As<Napi::String>();
      writer.key(ToUtf16(key));
      WriteJavaScriptValue(env, object.Get(key), writer, ancestors,
                           plain_prototype, depth + 1);
    }
    writer.endObject();
  }

  ancestors.pop_back();
}

std::vector<uint8_t> EncodeJavaScriptValue(Napi::Env env, Napi::Value input) {
  v8serial::Writer writer;
  std::vector<Napi::Object> ancestors;
  ancestors.reserve(8);
  WriteJavaScriptValue(env, input, writer, ancestors,
                       PlainObjectPrototype(env), 0);
  return writer.take();
}

Value CopyArgument(const Napi::CallbackInfo& info, size_t index) {
  std::vector<Napi::Object> ancestors;
  ancestors.reserve(8);
  return CopyValue(info.Env(), info[index], ancestors,
                   PlainObjectPrototype(info.Env()), 0);
}

void FinalizeExternalBuffer(napi_env, void*, void* hint) {
  delete static_cast<std::vector<uint8_t>*>(hint);
}

Napi::Value AdoptBuffer(Napi::Env env, std::vector<uint8_t>&& encoded) {
  auto* owner = new std::vector<uint8_t>(std::move(encoded));
  napi_value result;
  napi_status status =
      napi_create_external_buffer(env, owner->size(),
                                  reinterpret_cast<char*>(owner->data()),
                                  FinalizeExternalBuffer, owner, &result);
  if (status != napi_ok) {
    delete owner;
    throw Napi::Error::New(env, "Node refused to create an external Buffer");
  }
  return Napi::Value(env, result);
}

Napi::Value EncodeSync(const Napi::CallbackInfo& info) {
  if (info.Length() != 1) {
    throw Napi::TypeError::New(info.Env(), "encodeSync expects one value");
  }
  return AdoptBuffer(info.Env(),
                     EncodeJavaScriptValue(info.Env(), info[0]));
}

Napi::Value ToJavaScript(Napi::Env env, const v8serial::DecodedValue& value,
                         size_t depth) {
  if (depth > kMaxNestingDepth) {
    throw Napi::RangeError::New(env, "maximum nesting depth exceeded");
  }

  switch (value.type) {
    case v8serial::DecodedType::Undefined:
      return env.Undefined();
    case v8serial::DecodedType::Null:
      return env.Null();
    case v8serial::DecodedType::Boolean:
      return Napi::Boolean::New(env, value.boolean);
    case v8serial::DecodedType::Int32:
      return Napi::Number::New(env, value.int32);
    case v8serial::DecodedType::Uint32:
      return Napi::Number::New(env, value.uint32);
    case v8serial::DecodedType::Double:
      return Napi::Number::New(env, value.number);
    case v8serial::DecodedType::String:
      return Napi::String::New(env, value.string.data(), value.string.size());
    case v8serial::DecodedType::Array: {
      Napi::Array output = Napi::Array::New(env, value.array.size());
      for (size_t index = 0; index < value.array.size(); ++index) {
        output.Set(index, ToJavaScript(env, value.array[index], depth + 1));
      }
      return output;
    }
    case v8serial::DecodedType::Object: {
      Napi::Object output = Napi::Object::New(env);
      for (const auto& [key, child] : value.object) {
        Napi::String property_name =
            Napi::String::New(env, key.data(), key.size());
        Napi::Value property_value = ToJavaScript(env, child, depth + 1);
        napi_property_descriptor descriptor = {
            nullptr,
            property_name,
            nullptr,
            nullptr,
            nullptr,
            property_value,
            static_cast<napi_property_attributes>(
                napi_writable | napi_enumerable | napi_configurable),
            nullptr,
        };
        if (napi_define_properties(env, output, 1, &descriptor) != napi_ok) {
          throw Napi::Error::New(env, "failed to define decoded property");
        }
      }
      return output;
    }
    case v8serial::DecodedType::ArrayBuffer: {
      Napi::ArrayBuffer output = Napi::ArrayBuffer::New(env, value.binary.size());
      if (!value.binary.empty()) {
        std::memcpy(output.Data(), value.binary.data(), value.binary.size());
      }
      return output;
    }
    case v8serial::DecodedType::Uint8Array: {
      Napi::ArrayBuffer buffer =
          Napi::ArrayBuffer::New(env, value.binary.size());
      if (!value.binary.empty()) {
        std::memcpy(buffer.Data(), value.binary.data(), value.binary.size());
      }
      return Napi::Uint8Array::New(env, value.binary.size(), buffer, 0);
    }
  }
  throw Napi::Error::New(env, "unknown decoded value type");
}

std::vector<uint8_t> CopySerializedInput(const Napi::CallbackInfo& info,
                                         size_t index) {
  Napi::Env env = info.Env();
  if (index >= info.Length()) {
    throw Napi::TypeError::New(env, "serialized input is required");
  }
  if (info[index].IsArrayBuffer()) {
    Napi::ArrayBuffer buffer = info[index].As<Napi::ArrayBuffer>();
    const auto* data = static_cast<const uint8_t*>(buffer.Data());
    if (buffer.ByteLength() == 0) return {};
    return std::vector<uint8_t>(data, data + buffer.ByteLength());
  }
  if (info[index].IsTypedArray()) {
    Napi::TypedArray typed = info[index].As<Napi::TypedArray>();
    if (typed.TypedArrayType() != napi_uint8_array) {
      throw Napi::TypeError::New(env, "serialized input must contain bytes");
    }
    Napi::Uint8Array bytes = info[index].As<Napi::Uint8Array>();
    if (bytes.ElementLength() == 0) return {};
    return std::vector<uint8_t>(bytes.Data(),
                                bytes.Data() + bytes.ElementLength());
  }
  throw Napi::TypeError::New(
      env, "serialized input must be a Buffer, Uint8Array, or ArrayBuffer");
}

v8serial::DecodedValue Decode(const std::vector<uint8_t>& bytes) {
  return v8serial::Reader(bytes).read();
}

Napi::Value DecodeSync(const Napi::CallbackInfo& info) {
  if (info.Length() != 1) {
    throw Napi::TypeError::New(info.Env(), "decodeSync expects one buffer");
  }
  try {
    return ToJavaScript(info.Env(), Decode(CopySerializedInput(info, 0)), 0);
  } catch (const v8serial::DecodeError& error) {
    throw Napi::Error::New(info.Env(), error.what());
  }
}

struct AsyncResult {
  std::vector<uint8_t> bytes;
  std::string error;
};

struct AsyncDecodeResult {
  v8serial::DecodedValue value;
  std::string error;
};

struct AsyncWork {
  Napi::ThreadSafeFunction tsfn;
  std::thread worker;
};

Napi::Value EncodeAsync(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() != 2 || !info[1].IsFunction()) {
    throw Napi::TypeError::New(env, "encodeAsync expects (value, callback)");
  }

  Value value = CopyArgument(info, 0);
  auto work = std::make_unique<AsyncWork>();
  work->tsfn = Napi::ThreadSafeFunction::New(
      env, info[1].As<Napi::Function>(), "v8serial encode", 0, 1, work.get(),
      [](Napi::Env, AsyncWork* work) {
        if (work->worker.joinable()) work->worker.join();
        delete work;
      });
  AsyncWork* owned_work = work.release();

  try {
    owned_work->worker =
        std::thread([value = std::move(value), owned_work]() mutable {
          auto* result = new AsyncResult;
          try {
            result->bytes = Encode(value);
          } catch (const std::exception& error) {
            result->error = error.what();
          }

          napi_status status = owned_work->tsfn.BlockingCall(
              result, [](Napi::Env env, Napi::Function callback,
                         AsyncResult* result) {
                std::unique_ptr<AsyncResult> owned(result);
                if (!result->error.empty()) {
                  callback.Call({Napi::Error::New(env, result->error).Value(),
                                 env.Undefined()});
                  return;
                }
                callback.Call(
                    {env.Null(), AdoptBuffer(env, std::move(result->bytes))});
              });
          if (status != napi_ok) delete result;
          owned_work->tsfn.Release();
        });
  } catch (const std::system_error& error) {
    owned_work->tsfn.Release();
    throw Napi::Error::New(
        env, std::string("failed to start encoder thread: ") + error.what());
  }

  return env.Undefined();
}

Napi::Value DecodeAsync(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() != 2 || !info[1].IsFunction()) {
    throw Napi::TypeError::New(env, "decodeAsync expects (buffer, callback)");
  }

  std::vector<uint8_t> bytes = CopySerializedInput(info, 0);
  auto work = std::make_unique<AsyncWork>();
  work->tsfn = Napi::ThreadSafeFunction::New(
      env, info[1].As<Napi::Function>(), "v8serial decode", 0, 1, work.get(),
      [](Napi::Env, AsyncWork* work) {
        if (work->worker.joinable()) work->worker.join();
        delete work;
      });
  AsyncWork* owned_work = work.release();

  try {
    owned_work->worker =
        std::thread([bytes = std::move(bytes), owned_work]() mutable {
          auto* result = new AsyncDecodeResult;
          try {
            result->value = Decode(bytes);
          } catch (const std::exception& error) {
            result->error = error.what();
          }

          napi_status status = owned_work->tsfn.BlockingCall(
              result, [](Napi::Env env, Napi::Function callback,
                         AsyncDecodeResult* result) {
                std::unique_ptr<AsyncDecodeResult> owned(result);
                if (!result->error.empty()) {
                  callback.Call({Napi::Error::New(env, result->error).Value(),
                                 env.Undefined()});
                  return;
                }
                callback.Call(
                    {env.Null(), ToJavaScript(env, result->value, 0)});
              });
          if (status != napi_ok) delete result;
          owned_work->tsfn.Release();
        });
  } catch (const std::system_error& error) {
    owned_work->tsfn.Release();
    throw Napi::Error::New(
        env, std::string("failed to start decoder thread: ") + error.what());
  }

  return env.Undefined();
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports.Set("encodeSync", Napi::Function::New(env, EncodeSync));
  exports.Set("encodeAsync", Napi::Function::New(env, EncodeAsync));
  exports.Set("decodeSync", Napi::Function::New(env, DecodeSync));
  exports.Set("decodeAsync", Napi::Function::New(env, DecodeAsync));
  exports.Set("formatVersion",
              Napi::Number::New(env, v8serial::Writer::kFormatVersion));
  return exports;
}

}  // namespace

NODE_API_MODULE(v8serial, Init)
