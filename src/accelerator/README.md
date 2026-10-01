# Accelerator

`accelerator` separates ordinary C++ algorithms from CUDA execution. An algorithm inherits
`accelerator_c`, allocates typed device buffers, and constructs the operations it needs from CUDA
provider factories. Application-facing C++ headers contain no CUDA runtime types or launch syntax.

```cpp
#include "accelerator/cuda/operations.hpp"
#include "accelerator/types.hpp"

#include <utility>

class algorithm_c final : private accelerator::accelerator_c
{
public:
  algorithm_c()
  : accelerator_c(accelerator::accelerator_config_s{}),
    m_multiply(make_operation(
        accelerator::cuda_operations::make_elementwise_multiply_provider)),
    m_left(make_buffer<common::float32_t>(
        accelerator::tensor_shape_s<2U>{{1000U, 3U}})),
    m_right(make_buffer<common::float32_t>(
        accelerator::tensor_shape_s<2U>{{1000U, 3U}})),
    m_result(make_buffer<common::float32_t>(
        accelerator::tensor_shape_s<2U>{{1000U, 3U}}))
  {
  }

  accelerator::status_e process() noexcept
  {
    return m_multiply.execute(
      std::as_const(m_left), std::as_const(m_right), m_result);
  }

private:
  accelerator::operation_c m_multiply;
  accelerator::buffer_c<common::float32_t, 2U> m_left;
  accelerator::buffer_c<common::float32_t, 2U> m_right;
  accelerator::buffer_c<common::float32_t, 2U> m_result;
};
```

## Responsibilities

The accelerator core owns:

- CUDA device, stream, and library-handle lifetime;
- contiguous device allocations and validated strided views;
- synchronous host upload/download boundaries;
- tensor type, shape, stride, alias, and device metadata;
- construction of owning, type-erased `operation_c` instances.

Each `operation_c` owns its provider. The provider borrows the CUDA handles supplied during
construction, so an algorithm declares operations as members of the derived class. Those members
are destroyed before the accelerator base and its native resources.

Providers enqueue work without synchronizing after each operation. Operations from one accelerator
are ordered on its stream. `download()`, `synchronize()`, and accelerator destruction are host
completion boundaries. `upload()` and `clear_bytes()` are also synchronous completion boundaries in
the current API. One accelerator instance must be externally serialized; it is not safe to enqueue
from multiple host threads concurrently.

Initialization and allocation failures throw `accelerator_error_c`. Once initialized, transfer and
operation methods report runtime failures with `status_e` and do not throw.

## Reusable CUDA execution patterns

CUDA-authoring headers live under `include/accelerator/cuda`. Normal application code does not
include the `.cuh` headers.

`elementwise_operation.cuh` defines the numeric binary elementwise pattern. It validates three
tensors, handles contiguous and strided views, calculates launch geometry, uses a grid-stride loop,
and accepts the scalar computation as a device lambda. Keep the lambda and its provider helper
private to a `.cu` file:

```cpp
#include "accelerator/cuda/elementwise_operation.cuh"

#include <cuda_runtime.h>

#include <memory>

namespace
{

std::unique_ptr<accelerator::operation_provider_i> make_add_provider(
  const accelerator::cuda::operation_context_s & context)
{
  return accelerator::cuda::elementwise_binary(
    [] __device__(const auto left, const auto right) {
      return left + right;
    })(context);
}

}  // namespace

custom_algorithm_c::custom_algorithm_c()
: accelerator_c(accelerator::accelerator_config_s{}),
  m_add(make_operation(make_add_provider))
{
}
```

No operation-specific provider class, identifier, signature, thread calculation, or indexing code
is needed. The launcher does not know whether the lambda adds, multiplies, clamps, or transforms
values; it only knows the elementwise execution pattern. Device lambdas must be compiled in a `.cu`
translation unit. The explicitly typed helper also avoids NVCC's extended-lambda restrictions in
constructors and functions with deduced return types. It has internal linkage and is not part of the
application-facing header.

Two generic scheduling patterns are available now:

- `parallel_for` invokes a device lambda once for every logical work item. It chooses the CUDA grid,
  uses a grid-stride loop, and passes a flattened logical index plus typed tensor accessors to the
  lambda.
- `transform_reduce` maps each logical work item to an accumulator value, performs a block reduction
  in shared memory, and reduces the block results into one output value. The caller supplies a
  reusable rank-one workspace, so execution performs no hidden allocation.

Tensor rank and CUDA grid rank are independent. `leading_dimensions(argument, count)` defines the
logical work space. For an `{N, 3}` point tensor, `leading_dimensions(0U, 1U)` produces `N` work
items; for an `{H, W, C}` image, selecting the first two dimensions produces `H * W` work items.
The accelerator calculates CUDA thread and block indices.

A CUDA translation unit defines an indexed operation like this:

```cpp
using points_t =
  accelerator::cuda::tensor_accessor_c<const common::float32_t, 2U>;
using transformed_t =
  accelerator::cuda::tensor_accessor_c<common::float32_t, 2U>;

std::unique_ptr<accelerator::operation_provider_i> make_transform_provider(
  const accelerator::cuda::operation_context_s & context)
{
  return accelerator::cuda::parallel_for<points_t, transformed_t>(
    accelerator::cuda::leading_dimensions(0U, 1U),
    [] __device__(
      const std::size_t point_index,
      const points_t points,
      const transformed_t transformed) {
      transformed.at(point_index, 0U) = points.at(point_index, 0U);
      transformed.at(point_index, 1U) = points.at(point_index, 1U);
      transformed.at(point_index, 2U) = points.at(point_index, 2U);
    })(context);
}
```

`transform_reduce` receives its input tensors first, followed by the rank-one workspace and
rank-one result buffer when `operation_c::execute()` is called. The transform lambda returns one
accumulator per logical item. The reduction lambda must be associative and must accept the supplied
identity value. The accumulator may be a scalar or a trivially copyable standard-layout struct,
which allows an ICP operation to accumulate error, gradient, and Hessian terms together.

The workspace holds one value per active first-stage block. A smaller non-empty workspace reduces
parallelism but remains correct because the kernels use grid-stride loops. Input, workspace, and
result allocations must be distinct. Both patterns enqueue on the accelerator stream and do not
synchronize.

Matrix multiplication remains a predefined cuBLAS-backed provider because its tiled implementation
and vendor-library selection are different from an indexed elementwise launch.

## Domain operations

Point, pose, point-cloud, image, correspondence, ICP, and NDT semantics stay in their domain
packages. A domain `.cu` file combines a predefined execution pattern with its device lambda or
implements a specialized provider for work such as spatial search. Its ordinary C++ algorithm owns
the resulting generic `operation_c`.

A domain class can add typed methods around `operation_c` to validate semantic types at compile
time. The accelerator itself performs runtime tensor validation because it does not know whether an
operation represents ICP, NDT, image processing, or another algorithm.

The current design uses directly linked provider factories. It deliberately has no identifier
registry or runtime shared-library discovery. A dynamically loaded plugin system would require a
separate versioned ABI layer.

## Examples

Complete vector, point-cloud, and image examples are available in
[examples](examples/README.md). They separate ordinary C++ ownership and pipeline code from the
CUDA provider helpers and device lambdas.

## Build and test

The repository Makefile selects the architecture of the visible GPU by default:

```bash
make
make test
```

Override `CUDA_ARCHITECTURES` only when producing code for a different known target. The CMake
option `ACCELERATOR_BUILD_EXAMPLES` is `ON` by default. The examples README lists the installed
executables and their expected output.

## Current boundaries

Elementwise multiplication supports all fixed-width signed, unsigned, and floating numeric aliases
from `common`. Matrix multiplication supports `float32` and `float64` through cuBLAS. Buffers can
hold any trivially copyable standard-layout type, while each provider validates the types it can
execute.

Host transfers are synchronous to guarantee borrowed host-memory lifetimes. Uploads accept both
complete buffers and validated contiguous device subviews, allowing a bounded allocation to process
variable-size inputs without reallocation. Indexed `parallel_for` and caller-workspace
`transform_reduce` are implemented. Ownership-aware asynchronous
transfers, scan, sort, histogram/scatter, tiled neighborhood search, and reusable domain workspace
planning remain extensions to add when localization or perception algorithms require them.
