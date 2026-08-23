#pragma once

#include <concepts>
#include <limits>
#include <type_traits>

namespace common
{

template<typename value_t>
concept signed_integer_type =
  std::signed_integral<value_t>&&
  !std::same_as<std::remove_cv_t<value_t>, wchar_t>;

template<typename value_t>
concept integer_type =
  std::integral<value_t>&&
  !std::same_as<std::remove_cv_t<value_t>, bool>&&
  !std::same_as<std::remove_cv_t<value_t>, wchar_t>;

namespace add
{
template<signed_integer_type value_t>
constexpr bool will_overflow(const value_t left, const value_t right) noexcept
{
  return right > static_cast<value_t>(0) &&
         left > std::numeric_limits<value_t>::max() - right;
}

template<signed_integer_type value_t>
constexpr bool will_underflow(const value_t left, const value_t right) noexcept
{
  return right < static_cast<value_t>(0) &&
         left < std::numeric_limits<value_t>::min() - right;
}

template<signed_integer_type value_t>
constexpr bool will_wrap(const value_t left, const value_t right) noexcept
{
  return will_overflow(left, right) || will_underflow(left, right);
}

}  // namespace add

namespace sub
{
template<signed_integer_type value_t>
constexpr bool will_overflow(const value_t left, const value_t right) noexcept
{
  return right < static_cast<value_t>(0) &&
         left > std::numeric_limits<value_t>::max() + right;
}

template<signed_integer_type value_t>
constexpr bool will_underflow(const value_t left, const value_t right) noexcept
{
  return right > static_cast<value_t>(0) &&
         left < std::numeric_limits<value_t>::min() + right;
}

template<signed_integer_type value_t>
constexpr bool will_wrap(const value_t left, const value_t right) noexcept
{
  return will_overflow(left, right) || will_underflow(left, right);
}

}  // namespace sub

namespace mul
{
template<integer_type value_t>
constexpr bool will_overflow(const value_t left, const value_t right) noexcept
{
  if (left == static_cast<value_t>(0) || right == static_cast<value_t>(0)) {
    return false;
  }

  if constexpr (std::numeric_limits<value_t>::is_signed) {
    if (left > static_cast<value_t>(0) && right > static_cast<value_t>(0)) {
      return left > std::numeric_limits<value_t>::max() / right;
    }
    if (left < static_cast<value_t>(0) && right < static_cast<value_t>(0)) {
      return left < std::numeric_limits<value_t>::max() / right;
    }
    return false;
  } else {
    return left > std::numeric_limits<value_t>::max() / right;
  }
}

template<integer_type value_t>
constexpr bool will_underflow(const value_t left, const value_t right) noexcept
{
  if constexpr (!std::numeric_limits<value_t>::is_signed) {
    return false;
  } else {
    if (left == static_cast<value_t>(0) || right == static_cast<value_t>(0)) {
      return false;
    }
    if (left > static_cast<value_t>(0) && right < static_cast<value_t>(0)) {
      return right < std::numeric_limits<value_t>::min() / left;
    }
    if (left < static_cast<value_t>(0) && right > static_cast<value_t>(0)) {
      return left < std::numeric_limits<value_t>::min() / right;
    }
    return false;
  }
}

template<integer_type value_t>
constexpr bool will_wrap(const value_t left, const value_t right) noexcept
{
  return will_overflow(left, right) || will_underflow(left, right);
}

}  // namespace mul
}  // namespace common
