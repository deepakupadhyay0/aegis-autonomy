#include "accelerator/cuda/operations.hpp"
#include "accelerator/cuda/operation_validation.hpp"
#include "common/numeric_types.hpp"

#include <cublas_v2.h>
#include <cuda_runtime.h>

#include <cstddef>
#include <limits>
#include <memory>

namespace accelerator::cuda_operations
{

namespace
{

template<typename value_t>
struct cublas_gemm_c;

template<>
struct cublas_gemm_c<common::float32_t>
{
  static cublasStatus_t execute(
    cublasHandle_t handle,
    const int rows,
    const int columns,
    const int inner_dimension,
    const common::float32_t * const left,
    const common::float32_t * const right,
    common::float32_t * const result) noexcept
  {
    const common::float32_t alpha = 1.0F;
    const common::float32_t beta = 0.0F;
    return cublasSgemm(
      handle,
      CUBLAS_OP_N,
      CUBLAS_OP_N,
      rows,
      columns,
      inner_dimension,
      &alpha,
      right,
      rows,
      left,
      inner_dimension,
      &beta,
      result,
      rows);
  }
};

template<>
struct cublas_gemm_c<common::float64_t>
{
  static cublasStatus_t execute(
    cublasHandle_t handle,
    const int rows,
    const int columns,
    const int inner_dimension,
    const common::float64_t * const left,
    const common::float64_t * const right,
    common::float64_t * const result) noexcept
  {
    const common::float64_t alpha = 1.0;
    const common::float64_t beta = 0.0;
    return cublasDgemm(
      handle,
      CUBLAS_OP_N,
      CUBLAS_OP_N,
      rows,
      columns,
      inner_dimension,
      &alpha,
      right,
      rows,
      left,
      inner_dimension,
      &beta,
      result,
      rows);
  }
};

template<typename value_t>
status_e enqueue_typed(
  const operation_argument_s & left,
  const operation_argument_s & right,
  const operation_argument_s & result,
  const cuda::operation_context_s & context) noexcept
{
  const std::size_t left_rows = left.extents[0U];
  const std::size_t inner_dimension = left.extents[1U];
  const std::size_t right_columns = right.extents[1U];
  if (left_rows == 0U || right_columns == 0U) {
    return status_e::success;
  }

  cudaStream_t const stream = reinterpret_cast<cudaStream_t>(context.stream);
  if (inner_dimension == 0U) {
    const cudaError_t error = cudaMemsetAsync(
      result.data,
      0,
      result.element_count * result.element_size,
      stream);
    return error == cudaSuccess ? status_e::success : status_e::backend_error;
  }

  const std::size_t maximum_dimension =
    static_cast<std::size_t>(std::numeric_limits<int>::max());
  if (left_rows > maximum_dimension || inner_dimension > maximum_dimension ||
    right_columns > maximum_dimension)
  {
    return status_e::dimension_out_of_range;
  }

  // Row-major C = A * B has the same storage as the column-major
  // operation C^T = B^T * A^T expected by cuBLAS.
  const int operation_rows = static_cast<int>(right_columns);
  const int operation_columns = static_cast<int>(left_rows);
  const int operation_inner_dimension = static_cast<int>(inner_dimension);
  cublasHandle_t const handle =
    reinterpret_cast<cublasHandle_t>(context.linear_algebra_handle);
  const cublasStatus_t cublas_status = cublas_gemm_c<value_t>::execute(
    handle,
    operation_rows,
    operation_columns,
    operation_inner_dimension,
    static_cast<const value_t *>(left.data),
    static_cast<const value_t *>(right.data),
    static_cast<value_t *>(result.data));
  return cublas_status == CUBLAS_STATUS_SUCCESS ?
         status_e::success : status_e::backend_error;
}

class cuda_matrix_multiply_provider_c final : public operation_provider_i
{
public:
  explicit cuda_matrix_multiply_provider_c(
    const cuda::operation_context_s & context) noexcept
  : m_context(context)
  {
  }

  ~cuda_matrix_multiply_provider_c() noexcept override = default;

  status_e enqueue(
    const std::span<const operation_argument_s> arguments) noexcept override
  {
    const status_e validation =
      cuda::detail::validate_numeric_arguments<3U>(arguments, m_context.device_index);
    if (validation != status_e::success) {
      return validation;
    }

    const operation_argument_s & left = arguments[0U];
    const operation_argument_s & right = arguments[1U];
    const operation_argument_s & result = arguments[2U];
    if (!result.writable) {
      return status_e::invalid_argument;
    }
    if (!cuda::detail::rank_is(arguments, 2U)) {
      return status_e::invalid_argument;
    }
    if (!cuda::detail::all_contiguous(arguments)) {
      return status_e::non_contiguous_device_view;
    }
    if (left.extents[1U] != right.extents[0U] ||
      result.extents[0U] != left.extents[0U] ||
      result.extents[1U] != right.extents[1U])
    {
      return status_e::shape_mismatch;
    }
    if (result.element_count == 0U) {
      return status_e::success;
    }
    if (cuda::detail::same_allocation(result, left) ||
      cuda::detail::same_allocation(result, right))
    {
      return status_e::overlapping_buffers;
    }
    if (m_context.stream == nullptr || m_context.linear_algebra_handle == nullptr) {
      return status_e::backend_error;
    }
    if (cudaSetDevice(static_cast<int>(m_context.device_index)) != cudaSuccess) {
      return status_e::backend_error;
    }

    return cuda::detail::dispatch_floating(
      result.value_type,
      [&]<typename value_t>() noexcept {
        return enqueue_typed<value_t>(left, right, result, m_context);
      });
  }

private:
  cuda::operation_context_s m_context;
};

}  // namespace

std::unique_ptr<operation_provider_i> make_matrix_multiply_provider(
  const cuda::operation_context_s & context)
{
  return std::make_unique<cuda_matrix_multiply_provider_c>(context);
}

}  // namespace accelerator::cuda_operations
