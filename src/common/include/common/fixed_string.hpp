#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace common
{

template<std::size_t max_length_v>
class fixed_string_c final
{
  static_assert(max_length_v > 0U, "fixed_string_c capacity must be positive");

public:
  constexpr fixed_string_c() noexcept = default;

  template<std::size_t literal_size_v>
  consteval fixed_string_c(const char (&value)[literal_size_v])
  {
    static_assert(literal_size_v > 0U, "String literal must include a terminator");
    static_assert(
      literal_size_v - 1U <= max_length_v,
      "String literal exceeds fixed_string_c capacity");

    for (std::size_t index = 0U; index < literal_size_v - 1U; ++index) {
      if (value[index] == '\0') {
        throw "String literal contains an embedded null character";
      }
    }
    this->assign(std::string_view(value, literal_size_v - 1U));
  }

  fixed_string_c(const fixed_string_c &) = default;
  fixed_string_c & operator=(const fixed_string_c &) = default;
  fixed_string_c(fixed_string_c &&) noexcept = default;
  fixed_string_c & operator=(fixed_string_c &&) noexcept = default;
  ~fixed_string_c() = default;

  constexpr void assign(const std::string_view value) noexcept
  {
    std::array<char, max_length_v + 1U> assigned_storage{};
    std::size_t assigned_size = 0U;
    while (assigned_size < value.size() &&
      assigned_size < max_length_v &&
      value[assigned_size] != '\0')
    {
      assigned_storage[assigned_size] = value[assigned_size];
      ++assigned_size;
    }
    m_storage = assigned_storage;
    m_size = assigned_size;
  }

  constexpr void clear() noexcept
  {
    m_storage.fill('\0');
    m_size = 0U;
  }

  constexpr const char * c_str() const noexcept
  {
    return m_storage.data();
  }

  constexpr const char * data() const noexcept
  {
    return m_storage.data();
  }

  constexpr std::string_view view() const noexcept
  {
    return std::string_view(m_storage.data(), m_size);
  }

  constexpr std::size_t size() const noexcept
  {
    return m_size;
  }

  static constexpr std::size_t capacity() noexcept
  {
    return max_length_v;
  }

  constexpr bool empty() const noexcept
  {
    return m_size == 0U;
  }

  friend constexpr bool operator==(
    const fixed_string_c & left,
    const fixed_string_c & right) noexcept
  {
    return left.view() == right.view();
  }

  friend constexpr bool operator==(
    const fixed_string_c & left,
    const std::string_view right) noexcept
  {
    return left.view() == right;
  }

  friend constexpr bool operator==(
    const std::string_view left,
    const fixed_string_c & right) noexcept
  {
    return left == right.view();
  }

private:
  std::array<char, max_length_v + 1U> m_storage{};
  std::size_t m_size{0U};
};

using string8_t = fixed_string_c<8U>;
using string16_t = fixed_string_c<16U>;
using string32_t = fixed_string_c<32U>;
using string64_t = fixed_string_c<64U>;
using string128_t = fixed_string_c<128U>;
using string256_t = fixed_string_c<256U>;

}  // namespace common
