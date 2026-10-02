#pragma once

#include <concepts>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace common::validation
{

template<std::integral value_t>
void require_in_closed_range(
  const value_t value,
  const std::type_identity_t<value_t> minimum,
  const std::type_identity_t<value_t> maximum,
  const char * const error_message)
{
  if (value < minimum || value > maximum) {
    throw std::invalid_argument(error_message);
  }
}

template<std::integral target_t, std::integral value_t>
void require_representable(
  const value_t value,
  const char * const error_message)
{
  if (!std::in_range<target_t>(value)) {
    throw std::invalid_argument(error_message);
  }
}

template<std::integral target_t, std::integral value_t>
void require_positive_representable(
  const value_t value,
  const char * const error_message)
{
  if (value <= 0 || !std::in_range<target_t>(value)) {
    throw std::invalid_argument(error_message);
  }
}

}  // namespace common::validation
