#pragma once

#include "accelerator/buffer.hpp"
#include "accelerator/status.hpp"
#include "accelerator/visibility_control.hpp"
#include "common/numeric_types.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <memory>
#include <span>
#include <type_traits>

namespace accelerator
{

class accelerator_c;

inline constexpr std::size_t MAX_OPERATION_TENSOR_RANK = 8U;

enum class operation_value_type_e : common::uint8_t
{
  unknown = 0U,
  int8,
  int16,
  int32,
  int64,
  uint8,
  uint16,
  uint32,
  uint64,
  float32,
  float64
};

template<typename value_t>
inline constexpr operation_value_type_e operation_value_type_v = [] () constexpr {
  using plain_value_t = std::remove_cv_t<value_t>;
  if constexpr (std::same_as<plain_value_t, common::int8_t>) {
    return operation_value_type_e::int8;
  } else if constexpr (std::same_as<plain_value_t, common::int16_t>) {
    return operation_value_type_e::int16;
  } else if constexpr (std::same_as<plain_value_t, common::int32_t>) {
    return operation_value_type_e::int32;
  } else if constexpr (std::same_as<plain_value_t, common::int64_t>) {
    return operation_value_type_e::int64;
  } else if constexpr (std::same_as<plain_value_t, common::uint8_t>) {
    return operation_value_type_e::uint8;
  } else if constexpr (std::same_as<plain_value_t, common::uint16_t>) {
    return operation_value_type_e::uint16;
  } else if constexpr (std::same_as<plain_value_t, common::uint32_t>) {
    return operation_value_type_e::uint32;
  } else if constexpr (std::same_as<plain_value_t, common::uint64_t>) {
    return operation_value_type_e::uint64;
  } else if constexpr (std::same_as<plain_value_t, common::float32_t>) {
    return operation_value_type_e::float32;
  } else if constexpr (std::same_as<plain_value_t, common::float64_t>) {
    return operation_value_type_e::float64;
  } else {
    return operation_value_type_e::unknown;
  }
} ();

template<typename value_t>
concept numeric_operation_value =
  accelerator_value<value_t>&&
  operation_value_type_v<value_t>!= operation_value_type_e::unknown;

template<typename value_t>
concept floating_operation_value =
  std::same_as<value_t, common::float32_t>||
  std::same_as<value_t, common::float64_t>;

/// Type-erased tensor metadata copied into a backend invocation. The device allocation remains
/// owned by the originating buffer.
struct ACCELERATOR_PUBLIC operation_argument_s
{
  void * data{nullptr};
  const void * allocation_data{nullptr};
  std::array<std::size_t, MAX_OPERATION_TENSOR_RANK> extents{};
  std::array<std::size_t, MAX_OPERATION_TENSOR_RANK> strides{};
  std::size_t rank{0U};
  std::size_t element_count{0U};
  std::size_t element_size{0U};
  common::uint32_t device_index{0U};
  operation_value_type_e value_type{operation_value_type_e::unknown};
  bool writable{false};

  bool contiguous() const noexcept;
  bool non_overlapping() const noexcept;
};

class ACCELERATOR_PUBLIC operation_provider_i
{
public:
  operation_provider_i() noexcept = default;
  virtual ~operation_provider_i() noexcept = default;

  operation_provider_i(const operation_provider_i &) = delete;
  operation_provider_i & operator=(const operation_provider_i &) = delete;
  operation_provider_i(operation_provider_i &&) = delete;
  operation_provider_i & operator=(operation_provider_i &&) = delete;

  virtual status_e enqueue(
    std::span<const operation_argument_s> arguments) noexcept = 0;
};

namespace detail
{

class device_tensor_view_access_c final
{
public:
  template<accelerator_element element_t, std::size_t rank_v>
  static operation_argument_s make_argument(
    const device_tensor_view_c<element_t, rank_v> & view) noexcept
  {
    operation_argument_s argument{};
    const void * const data = static_cast<const void *>(view.m_data);
    argument.data = const_cast<void *>(data);
    argument.allocation_data = view.m_allocation_data;
    argument.rank = rank_v;
    argument.element_count = view.element_count();
    argument.element_size = sizeof(std::remove_const_t<element_t>);
    argument.device_index = view.device_index();
    argument.value_type = operation_value_type_v<std::remove_const_t<element_t>>;
    argument.writable = !std::is_const_v<element_t>;

    if constexpr (rank_v <= MAX_OPERATION_TENSOR_RANK) {
      for (std::size_t dimension = 0U; dimension < rank_v; ++dimension) {
        argument.extents[dimension] = view.m_shape.extents[dimension];
        argument.strides[dimension] = view.m_strides.elements[dimension];
      }
    }
    return argument;
  }

  template<accelerator_value value_t, std::size_t rank_v>
  static operation_argument_s make_argument(
    buffer_c<value_t, rank_v> & buffer) noexcept
  {
    return make_argument(buffer.view());
  }

  template<accelerator_value value_t, std::size_t rank_v>
  static operation_argument_s make_argument(
    const buffer_c<value_t, rank_v> & buffer) noexcept
  {
    return make_argument(buffer.view());
  }
};

}  // namespace detail

/// Owns one backend operation provider. Declare operations as derived-algorithm members so their
/// providers are destroyed before the accelerator base and its borrowed CUDA resources.
class ACCELERATOR_PUBLIC operation_c final
{
public:
  ~operation_c() noexcept;

  operation_c(const operation_c &) = delete;
  operation_c & operator=(const operation_c &) = delete;
  operation_c(operation_c &&) = delete;
  operation_c & operator=(operation_c &&) = delete;

  template<typename ... argument_t>
  status_e execute(argument_t &... arguments) noexcept
  {
    static_assert(sizeof...(argument_t) > 0U);
    const std::array<operation_argument_s, sizeof...(argument_t)> provider_arguments{
      detail::device_tensor_view_access_c::make_argument(arguments)...};
    return m_provider->enqueue(provider_arguments);
  }

private:
  friend class accelerator_c;

  explicit operation_c(std::unique_ptr<operation_provider_i> provider) noexcept;

  std::unique_ptr<operation_provider_i> m_provider;
};

}  // namespace accelerator
