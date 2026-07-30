#pragma once

#include "common/fixed_string.hpp"
#include "common/numeric_types.hpp"

#include <cstdbool>
#include <cstdint>
#include <cmath>
#include <string>

#if defined _WIN32
  #define CORE_WINDOWS
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

#define CORE_MILLION (1000000)
#define CORE_BILLION (1000000000)

enum class core_ret_e : common::int32_t {
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
using float32_t = common::float32_t;
using float64_t = common::float64_t;

using size64_t = common::uint64_t;

using core_ret_t = core_ret_e;

using core_string2_t = common::fixed_string_c<2U>;
using core_string4_t = common::fixed_string_c<4U>;
using core_string8_t = common::string8_t;
using core_string16_t = common::string16_t;
using core_string32_t = common::string32_t;
using core_string64_t = common::string64_t;
using core_string256_t = common::string256_t;

std::string get_toml_config_directory();
