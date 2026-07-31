#pragma once

#include "base_core/visibility_control.hpp"
#include "common/fixed_string.hpp"

#include <string_view>

namespace base_core
{
namespace execution
{

/// Linux thread names contain at most 15 visible characters.
BASE_CORE_PUBLIC void set_current_thread_name(std::string_view name) noexcept;
BASE_CORE_PUBLIC common::string16_t get_current_thread_name() noexcept;

}  // namespace execution
}  // namespace base_core
