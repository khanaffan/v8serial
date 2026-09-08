{
  "targets": [
    {
      "target_name": "v8serial",
      "sources": [ "src/addon.cc" ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "include"
      ],
      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ],
      "defines": [ "NAPI_CPP_EXCEPTIONS" ],
      "cflags_cc": [ "-std=c++17", "-fexceptions" ],
      "xcode_settings": {
        "CLANG_CXX_LANGUAGE_STANDARD": "c++17",
        "GCC_ENABLE_CPP_EXCEPTIONS": "YES"
      },
      "msvs_settings": {
        "VCCLCompilerTool": { "ExceptionHandling": 1 }
      }
    },
    {
      "target_name": "v8serial_native_test",
      "type": "executable",
      "sources": [ "test/writer_test.cc" ],
      "include_dirs": [ "include" ],
      "cflags_cc": [ "-std=c++17", "-fexceptions" ],
      "xcode_settings": {
        "CLANG_CXX_LANGUAGE_STANDARD": "c++17",
        "GCC_ENABLE_CPP_EXCEPTIONS": "YES"
      },
      "msvs_settings": {
        "VCCLCompilerTool": { "ExceptionHandling": 1 }
      }
    },
    {
      "target_name": "v8serial_bench_napi",
      "sources": [ "bench/boundary_addon.cc" ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "include"
      ],
      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ],
      "defines": [ "NAPI_CPP_EXCEPTIONS" ],
      "cflags_cc": [ "-std=c++17", "-O3", "-fexceptions" ],
      "xcode_settings": {
        "CLANG_CXX_LANGUAGE_STANDARD": "c++17",
        "GCC_OPTIMIZATION_LEVEL": "3",
        "GCC_ENABLE_CPP_EXCEPTIONS": "YES"
      },
      "msvs_settings": {
        "VCCLCompilerTool": {
          "ExceptionHandling": 1,
          "Optimization": 2
        }
      }
    },
    {
      "target_name": "v8serial_native_bench",
      "type": "executable",
      "sources": [ "bench/native_bench.cc" ],
      "include_dirs": [ "include" ],
      "cflags_cc": [ "-std=c++17", "-O3", "-fexceptions" ],
      "xcode_settings": {
        "CLANG_CXX_LANGUAGE_STANDARD": "c++17",
        "GCC_OPTIMIZATION_LEVEL": "3",
        "GCC_ENABLE_CPP_EXCEPTIONS": "YES"
      },
      "msvs_settings": {
        "VCCLCompilerTool": {
          "ExceptionHandling": 1,
          "Optimization": 2
        }
      }
    },
    {
      "target_name": "v8serial_native_bench_scalar",
      "type": "executable",
      "sources": [ "bench/native_bench.cc" ],
      "include_dirs": [ "include" ],
      "defines": [ "V8SERIAL_DISABLE_SIMD=1" ],
      "cflags_cc": [ "-std=c++17", "-O3", "-fexceptions" ],
      "xcode_settings": {
        "CLANG_CXX_LANGUAGE_STANDARD": "c++17",
        "GCC_OPTIMIZATION_LEVEL": "3",
        "GCC_ENABLE_CPP_EXCEPTIONS": "YES"
      },
      "msvs_settings": {
        "VCCLCompilerTool": {
          "ExceptionHandling": 1,
          "Optimization": 2
        }
      }
    }
  ]
}
