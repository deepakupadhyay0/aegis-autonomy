#pragma once

#include <cstdbool>
#include <cstdint>
#include <cmath>

#if defined _WIN32
  #define CORE_WINDOWS
#elif defined __APPLE__
  #define CORE_APPLE
#elif defined __QNX__
  #define CORE_QNX
#else
  #define CORE_LINUX
#endif

#ifdef CORE_WINDOWS
#define _USE_MATH_DEFINES
#define CORE_LLD_FMT "%lld"
#define CORE_LLU_FMT "%llu"
#ifndef NOMINMAX
#define NOMINMAX  
#endif
#else
#define CORE_LLD_FMT "%ld"
#define CORE_LLU_FMT "%lu"
#endif

#define CORE_STRING_SIZE (256U)
#define CORE_MILLION (1000000)
#define CORE_BILLION (1000000000)

enum class core_ret_e : int32_t {
  ok = 0,
  error = 1,
  bad_arg = 2,
  too_large = 3,
  timeout = 4,
  locked = 5,
  resource_leak = 6,
  object_is_active = 7,
  object_exist = 8,
  wrong_address = 9,
  overflow = 10,
  underflow = 11,
  object_missing = 12,
  wrong_object = 13,
  object_is_inactive = 14
};

#define OBJ_EXISTS_INDICATOR (0x15U)

using bool8_t = bool;
using char8_t = char;
using float32_t = float;
using float64_t = double;

#ifdef CORE_QNX
#include <sys/types.h>
#else
using size64_t = uint64_t;
#endif

#ifdef CORE_APPLE
#define DARWIN_SIZE64_T size_t
#else
#define DARWIN_SIZE64_T size64_t
#endif

using core_ret_t = core_ret_e;

struct core_string2_t { char8_t c_str[2U]; };
struct core_string4_t { char8_t c_str[4U]; };
struct core_string8_t { char8_t c_str[8U]; };
struct core_string16_t { char8_t c_str[16U]; };
struct core_string32_t { char8_t c_str[32U]; };
struct core_string64_t { char8_t c_str[64U]; };
struct core_string128_t { char8_t c_str[128U]; };
struct core_string256_t { char8_t c_str[CORE_STRING_SIZE]; };
