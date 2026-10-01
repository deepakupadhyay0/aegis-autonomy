#pragma once

#include "accelerator/buffer.hpp"
#include "accelerator/cuda/operation_context.hpp"
#include "accelerator/operation.hpp"
#include "accelerator/status.hpp"
#include "accelerator/visibility_control.hpp"
#include "common/numeric_types.hpp"

#include <concepts>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace accelerator
{

struct accelerator_config_s
{
  common::uint32_t device_index{0U};
};

/// Error raised when an accelerator or device allocation cannot be initialized.
class ACCELERATOR_PUBLIC accelerator_error_c final : public std::runtime_error
{
public:
  explicit accelerator_error_c(const std::string & message);
};

/// Inheritable CUDA accelerator foundation for algorithm implementations.
///
/// Derived algorithms own typed buffers and operations. This base owns one non-blocking execution
/// stream and its CUDA library handles. Operations enqueue work on that stream; host transfers and
/// synchronize() are completion boundaries. One instance must be externally serialized.
class ACCELERATOR_PUBLIC accelerator_c
{
public:
  accelerator_c(const accelerator_c &) = delete;
  accelerator_c & operator=(const accelerator_c &) = delete;
  accelerator_c(accelerator_c &&) = delete;
  accelerator_c & operator=(accelerator_c &&) = delete;

protected:
  explicit accelerator_c(const accelerator_config_s & config);
  ~accelerator_c() noexcept;

  template<accelerator_value value_t, std::size_t rank_v>
  buffer_c<value_t, rank_v> make_buffer(
    const tensor_shape_s<rank_v> & shape)
  {
    const std::optional<std::size_t> element_count = shape.try_element_count();
    const std::optional<tensor_strides_s<rank_v>> strides =
      detail::try_make_contiguous_strides(shape);
    if (!element_count.has_value() || !strides.has_value() ||
      *element_count > std::numeric_limits<std::size_t>::max() / sizeof(value_t))
    {
      throw accelerator_error_c("Invalid or overflowing device-buffer shape");
    }

    const std::size_t size_bytes = *element_count * sizeof(value_t);
    device_memory_s memory = allocate_device_memory(size_bytes);
    return buffer_c<value_t, rank_v>(
      std::move(memory.owner),
      static_cast<value_t *>(memory.data),
      shape,
      *strides,
      *element_count,
      size_bytes,
      device_index());
  }

  template<accelerator_element source_element_t, accelerator_value value_t,
    std::size_t rank_v>
  status_e upload(
    const host_tensor_view_c<source_element_t, rank_v> & source,
    buffer_c<value_t, rank_v> & destination) noexcept
  requires std::same_as<std::remove_const_t<source_element_t>, value_t>
  {
    if (destination.device_index() != device_index()) {
      return status_e::device_mismatch;
    }
    if (source.shape() != destination.shape()) {
      return status_e::shape_mismatch;
    }
    if (!source.contiguous()) {
      return status_e::non_contiguous_host_view;
    }
    return copy_host_to_device(
      destination.m_data,
      source.data(),
      destination.size_bytes());
  }

  template<accelerator_element source_element_t, accelerator_value value_t,
    std::size_t rank_v>
  status_e upload(
    const host_tensor_view_c<source_element_t, rank_v> & source,
    const device_tensor_view_c<value_t, rank_v> & destination) noexcept
  requires std::same_as<std::remove_const_t<source_element_t>, value_t>
  {
    if (destination.device_index() != device_index()) {
      return status_e::device_mismatch;
    }
    if (source.shape() != destination.shape()) {
      return status_e::shape_mismatch;
    }
    if (!source.contiguous()) {
      return status_e::non_contiguous_host_view;
    }
    if (!detail::is_contiguous(destination.shape(), destination.strides())) {
      return status_e::non_contiguous_device_view;
    }
    return copy_host_to_device(
      destination.m_data,
      source.data(),
      source.element_count() * sizeof(value_t));
  }

  template<accelerator_value value_t, std::size_t rank_v>
  status_e download(
    const buffer_c<value_t, rank_v> & source,
    const host_tensor_view_c<value_t, rank_v> & destination) noexcept
  {
    if (source.device_index() != device_index()) {
      return status_e::device_mismatch;
    }
    if (source.shape() != destination.shape()) {
      return status_e::shape_mismatch;
    }
    if (!destination.contiguous()) {
      return status_e::non_contiguous_host_view;
    }
    return copy_device_to_host(
      destination.data(),
      source.m_data,
      source.size_bytes());
  }

  template<accelerator_value value_t, std::size_t rank_v>
  status_e clear_bytes(buffer_c<value_t, rank_v> & buffer) noexcept
  {
    if (buffer.device_index() != device_index()) {
      return status_e::device_mismatch;
    }
    return zero_device_memory(buffer.m_data, buffer.size_bytes());
  }

  /// Constructs an operation whose provider borrows this accelerator's CUDA resources.
  /// The operation must be destroyed before this accelerator.
  template<typename provider_factory_t>
  requires std::invocable<
    provider_factory_t, const cuda::operation_context_s &>&&
  std::convertible_to<
    std::invoke_result_t<provider_factory_t, const cuda::operation_context_s &>,
    std::unique_ptr<operation_provider_i>>
  operation_c make_operation(provider_factory_t && provider_factory)
  {
    std::unique_ptr<operation_provider_i> provider = std::invoke(
      std::forward<provider_factory_t>(provider_factory),
      cuda_operation_context());
    if (provider == nullptr) {
      throw accelerator_error_c("Accelerator operation provider factory returned null");
    }
    return operation_c{std::move(provider)};
  }

  status_e synchronize() noexcept;
  common::uint32_t device_index() const noexcept;

private:
  struct device_memory_s
  {
    std::shared_ptr<void> owner;
    void * data{nullptr};
  };

  device_memory_s allocate_device_memory(std::size_t size_bytes);
  status_e copy_host_to_device(
    void * destination,
    const void * source,
    std::size_t size_bytes) noexcept;
  status_e copy_device_to_host(
    void * destination,
    const void * source,
    std::size_t size_bytes) noexcept;
  status_e zero_device_memory(
    void * destination,
    std::size_t size_bytes) noexcept;
  cuda::operation_context_s cuda_operation_context() const noexcept;

  class implementation_c;
  std::unique_ptr<implementation_c> m_implementation;
};

}  // namespace accelerator
