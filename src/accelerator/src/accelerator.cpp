#include "accelerator/accelerator.hpp"

#include <cublas_v2.h>
#include <cuda_runtime_api.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace accelerator
{

namespace
{

std::string cuda_error_message(
  const char * const operation,
  const cudaError_t error)
{
  std::ostringstream stream;
  stream << operation << ": " << cudaGetErrorString(error);
  return stream.str();
}

}  // namespace

class accelerator_c::implementation_c final
{
public:
  explicit implementation_c(const accelerator_config_s & config)
  : m_device_index(config.device_index)
  {
    int device_count = 0;
    cudaError_t error = cudaGetDeviceCount(&device_count);
    if (error != cudaSuccess) {
      throw accelerator_error_c(
              cuda_error_message("Failed to query CUDA devices", error));
    }
    if (m_device_index >= static_cast<common::uint32_t>(device_count)) {
      throw accelerator_error_c("CUDA device index is out of range");
    }

    error = cudaSetDevice(static_cast<int>(m_device_index));
    if (error != cudaSuccess) {
      throw accelerator_error_c(
              cuda_error_message("Failed to select CUDA device", error));
    }

    cudaDeviceProp device_properties{};
    error = cudaGetDeviceProperties(
      &device_properties, static_cast<int>(m_device_index));
    if (error != cudaSuccess) {
      throw accelerator_error_c(
              cuda_error_message("Failed to query CUDA device properties", error));
    }
    if (device_properties.maxThreadsPerBlock <= 0 ||
      device_properties.maxGridSize[0U] <= 0 ||
      device_properties.sharedMemPerBlock == 0U)
    {
      throw accelerator_error_c("CUDA device reported invalid launch limits");
    }
    constexpr int PREFERRED_THREADS_PER_BLOCK = 256;
    m_threads_per_block = static_cast<common::uint32_t>(
      std::min(PREFERRED_THREADS_PER_BLOCK, device_properties.maxThreadsPerBlock));
    m_maximum_block_count = static_cast<common::uint32_t>(
      device_properties.maxGridSize[0U]);
    m_shared_memory_per_block = device_properties.sharedMemPerBlock;

    error = cudaStreamCreateWithFlags(&m_stream, cudaStreamNonBlocking);
    if (error != cudaSuccess) {
      throw accelerator_error_c(
              cuda_error_message("Failed to create CUDA stream", error));
    }

    cublasStatus_t cublas_error = cublasCreate(&m_cublas_handle);
    if (cublas_error != CUBLAS_STATUS_SUCCESS) {
      static_cast<void>(cudaStreamDestroy(m_stream));
      m_stream = nullptr;
      throw accelerator_error_c("Failed to create cuBLAS handle");
    }
    cublas_error = cublasSetStream(m_cublas_handle, m_stream);
    if (cublas_error != CUBLAS_STATUS_SUCCESS) {
      static_cast<void>(cublasDestroy(m_cublas_handle));
      m_cublas_handle = nullptr;
      static_cast<void>(cudaStreamDestroy(m_stream));
      m_stream = nullptr;
      throw accelerator_error_c("Failed to associate cuBLAS with CUDA stream");
    }
  }

  ~implementation_c() noexcept
  {
    if (m_stream == nullptr) {
      return;
    }
    if (cudaSetDevice(static_cast<int>(m_device_index)) == cudaSuccess) {
      static_cast<void>(cudaStreamSynchronize(m_stream));
      if (m_cublas_handle != nullptr) {
        static_cast<void>(cublasDestroy(m_cublas_handle));
      }
      static_cast<void>(cudaStreamDestroy(m_stream));
    }
  }

  implementation_c(const implementation_c &) = delete;
  implementation_c & operator=(const implementation_c &) = delete;
  implementation_c(implementation_c &&) = delete;
  implementation_c & operator=(implementation_c &&) = delete;

  common::uint32_t device_index() const noexcept
  {
    return m_device_index;
  }

  cudaStream_t stream() const noexcept
  {
    return m_stream;
  }

  cuda::operation_context_s operation_context() const noexcept
  {
    return {
      reinterpret_cast<void *>(m_stream),
      reinterpret_cast<void *>(m_cublas_handle),
      m_device_index,
      m_threads_per_block,
      m_maximum_block_count,
      m_shared_memory_per_block};
  }

  bool select_device() const noexcept
  {
    return cudaSetDevice(static_cast<int>(m_device_index)) == cudaSuccess;
  }

private:
  common::uint32_t m_device_index;
  common::uint32_t m_threads_per_block{0U};
  common::uint32_t m_maximum_block_count{0U};
  std::size_t m_shared_memory_per_block{0U};
  cudaStream_t m_stream{nullptr};
  cublasHandle_t m_cublas_handle{nullptr};
};

accelerator_error_c::accelerator_error_c(const std::string & message)
: std::runtime_error(message)
{
}

accelerator_c::accelerator_c(const accelerator_config_s & config)
: m_implementation(std::make_unique<implementation_c>(config))
{
}

accelerator_c::~accelerator_c() noexcept = default;

accelerator_c::device_memory_s accelerator_c::allocate_device_memory(
  const std::size_t size_bytes)
{
  if (size_bytes == 0U) {
    return {};
  }
  if (!m_implementation->select_device()) {
    throw accelerator_error_c("Failed to select CUDA device before allocation");
  }

  void * data = nullptr;
  const cudaError_t error = cudaMalloc(&data, size_bytes);
  if (error != cudaSuccess) {
    throw accelerator_error_c(
            cuda_error_message("Failed to allocate CUDA memory", error));
  }

  const common::uint32_t allocation_device = m_implementation->device_index();
  std::shared_ptr<void> owner(
    data,
    [allocation_device](void * const allocation) noexcept {
      if (allocation == nullptr) {
        return;
      }
      if (cudaSetDevice(static_cast<int>(allocation_device)) == cudaSuccess) {
        static_cast<void>(cudaFree(allocation));
      }
    });
  return {std::move(owner), data};
}

status_e accelerator_c::copy_host_to_device(
  void * const destination,
  const void * const source,
  const std::size_t size_bytes) noexcept
{
  if (size_bytes == 0U) {
    return status_e::success;
  }
  if (destination == nullptr || source == nullptr) {
    return status_e::invalid_argument;
  }
  if (!m_implementation->select_device()) {
    return status_e::backend_error;
  }

  cudaError_t error = cudaMemcpyAsync(
    destination,
    source,
    size_bytes,
    cudaMemcpyHostToDevice,
    m_implementation->stream());
  if (error == cudaSuccess) {
    error = cudaStreamSynchronize(m_implementation->stream());
  }
  return error == cudaSuccess ? status_e::success : status_e::backend_error;
}

status_e accelerator_c::copy_device_to_host(
  void * const destination,
  const void * const source,
  const std::size_t size_bytes) noexcept
{
  if (size_bytes == 0U) {
    return status_e::success;
  }
  if (destination == nullptr || source == nullptr) {
    return status_e::invalid_argument;
  }
  if (!m_implementation->select_device()) {
    return status_e::backend_error;
  }

  cudaError_t error = cudaMemcpyAsync(
    destination,
    source,
    size_bytes,
    cudaMemcpyDeviceToHost,
    m_implementation->stream());
  if (error == cudaSuccess) {
    error = cudaStreamSynchronize(m_implementation->stream());
  }
  return error == cudaSuccess ? status_e::success : status_e::backend_error;
}

status_e accelerator_c::zero_device_memory(
  void * const destination,
  const std::size_t size_bytes) noexcept
{
  if (size_bytes == 0U) {
    return status_e::success;
  }
  if (destination == nullptr) {
    return status_e::invalid_argument;
  }
  if (!m_implementation->select_device()) {
    return status_e::backend_error;
  }

  cudaError_t error = cudaMemsetAsync(
    destination,
    0,
    size_bytes,
    m_implementation->stream());
  if (error == cudaSuccess) {
    error = cudaStreamSynchronize(m_implementation->stream());
  }
  return error == cudaSuccess ? status_e::success : status_e::backend_error;
}

cuda::operation_context_s accelerator_c::cuda_operation_context() const noexcept
{
  return m_implementation->operation_context();
}

status_e accelerator_c::synchronize() noexcept
{
  if (!m_implementation->select_device()) {
    return status_e::backend_error;
  }
  return cudaStreamSynchronize(m_implementation->stream()) == cudaSuccess ?
         status_e::success : status_e::backend_error;
}

common::uint32_t accelerator_c::device_index() const noexcept
{
  return m_implementation->device_index();
}

}  // namespace accelerator
