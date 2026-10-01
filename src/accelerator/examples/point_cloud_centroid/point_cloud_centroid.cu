#include "point_cloud_centroid.hpp"

#include "accelerator/cuda/tensor_accessor.cuh"
#include "accelerator/cuda/transform_reduce.cuh"

#include <cuda_runtime.h>

#include <cstddef>
#include <memory>

namespace accelerator_examples::point_cloud_centroid
{

namespace
{

using points_accessor_t =
  accelerator::cuda::tensor_accessor_c<const scalar_t, 2U>;

std::unique_ptr<accelerator::operation_provider_i> make_point_sum_provider(
  const accelerator::cuda::operation_context_s & context)
{
  return accelerator::cuda::transform_reduce<point_sum_s, points_accessor_t>(
    accelerator::cuda::leading_dimensions(0U, 1U),
    point_sum_s{},
    [] __device__(
      const std::size_t point_index,
      const points_accessor_t points) {
      return point_sum_s{
        points.at(point_index, 0U),
        points.at(point_index, 1U),
        points.at(point_index, 2U),
        1U};
    },
    [] __device__(const point_sum_s left, const point_sum_s right) {
      return point_sum_s{
        left.x + right.x,
        left.y + right.y,
        left.z + right.z,
        left.count + right.count};
    })(context);
}

}  // namespace

point_cloud_centroid_c::point_cloud_centroid_c(
  const std::size_t point_count)
: accelerator_c(accelerator::accelerator_config_s{}),
  m_point_sum(make_operation(make_point_sum_provider)),
  m_points(make_buffer<scalar_t>(
      points_shape_t{{point_count, POINT_DIMENSION}})),
  m_workspace(make_buffer<point_sum_s>(
      accumulator_shape_t{{WORKSPACE_CAPACITY}})),
  m_result(make_buffer<point_sum_s>(accumulator_shape_t{{1U}}))
{
}

}  // namespace accelerator_examples::point_cloud_centroid
