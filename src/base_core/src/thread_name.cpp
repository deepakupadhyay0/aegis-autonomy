#include "base_core/execution/thread_name.hpp"

#include <array>
#include <pthread.h>

namespace
{

thread_local common::string16_t current_thread_name;
thread_local bool8_t current_thread_name_initialized = false;

}  // namespace

namespace base_core
{
namespace execution
{

void set_current_thread_name(const std::string_view name) noexcept
{
  constexpr std::size_t LINUX_THREAD_NAME_CAPACITY = 15U;
  const std::size_t name_size =
    name.size() < LINUX_THREAD_NAME_CAPACITY ?
    name.size() :
    LINUX_THREAD_NAME_CAPACITY;
  current_thread_name.assign(name.substr(0U, name_size));
  current_thread_name_initialized = true;

#if defined(__linux__)
  static_cast<void>(
    ::pthread_setname_np(::pthread_self(), current_thread_name.c_str()));
#endif
}

common::string16_t get_current_thread_name() noexcept
{
  if (current_thread_name_initialized) {
    return current_thread_name;
  }

  std::array<char, 16U> name{};
#if defined(__linux__)
  if (::pthread_getname_np(::pthread_self(), name.data(), name.size()) == 0) {
    current_thread_name.assign(name.data());
  }
#endif
  if (current_thread_name.empty()) {
    current_thread_name = "unnamed";
  }
  current_thread_name_initialized = true;
  return current_thread_name;
}

}  // namespace execution
}  // namespace base_core
