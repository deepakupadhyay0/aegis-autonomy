#include "localization/cuda_ndt_localizer.hpp"

#include "accelerator/cuda/tensor_accessor.cuh"
#include "accelerator/cuda/transform_reduce.cuh"
#include "common/config_validation.hpp"

#include <cuda_runtime.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace localization
{

namespace
{

using scan_accessor_t =
  accelerator::cuda::tensor_accessor_c<const point_3f_s, 1U>;
using voxel_indices_accessor_t =
  accelerator::cuda::tensor_accessor_c<const common::int64_t, 2U>;
using voxel_means_accessor_t =
  accelerator::cuda::tensor_accessor_c<const common::float64_t, 2U>;
using voxel_information_accessor_t =
  accelerator::cuda::tensor_accessor_c<const common::float64_t, 2U>;
using pose_accessor_t =
  accelerator::cuda::tensor_accessor_c<const common::float64_t, 1U>;

const autonomy_config::Localization::Localizer & checked_localizer_config(
  const autonomy_config::Localization::Localizer & config)
{
  validate_ndt_localizer_config(config);
  return config;
}

accelerator::accelerator_config_s make_accelerator_config(
  const autonomy_config::Localization::Accelerator & config)
{
  const common::int64_t device_index = config.get_device_index();
  common::validation::require_representable<common::uint32_t>(
    device_index,
    "accelerator.device_index must fit in uint32");
  return accelerator::accelerator_config_s{
    static_cast<common::uint32_t>(device_index)};
}

__device__ bool try_voxel_coordinate(
  const common::float64_t coordinate_m,
  const common::float64_t voxel_size_m,
  common::int64_t & index) noexcept
{
  const common::float64_t value = floor(coordinate_m / voxel_size_m);
  constexpr common::float64_t MINIMUM_INDEX = -0x1p63;
  constexpr common::float64_t MAXIMUM_INDEX_EXCLUSIVE = 0x1p63;
  if (!isfinite(value) ||
    value < MINIMUM_INDEX ||
    value >= MAXIMUM_INDEX_EXCLUSIVE)
  {
    return false;
  }
  index = static_cast<common::int64_t>(value);
  return true;
}

__device__ bool try_add_offset(
  const common::int64_t index,
  const common::int64_t offset,
  common::int64_t & result) noexcept
{
  constexpr common::int64_t MINIMUM_INT64 =
    (-9223372036854775807LL - 1LL);
  constexpr common::int64_t MAXIMUM_INT64 = 9223372036854775807LL;
  if ((offset > 0 && index > MAXIMUM_INT64 - offset) ||
    (offset < 0 && index < MINIMUM_INT64 - offset))
  {
    return false;
  }
  result = index + offset;
  return true;
}

__device__ common::int32_t compare_voxel_index(
  const voxel_indices_accessor_t indices,
  const std::size_t voxel,
  const common::int64_t x,
  const common::int64_t y,
  const common::int64_t z) noexcept
{
  const common::int64_t candidate_x = indices.at(voxel, 0U);
  const common::int64_t candidate_y = indices.at(voxel, 1U);
  const common::int64_t candidate_z = indices.at(voxel, 2U);
  if (candidate_x != x) {
    return candidate_x < x ? -1 : 1;
  }
  if (candidate_y != y) {
    return candidate_y < y ? -1 : 1;
  }
  if (candidate_z != z) {
    return candidate_z < z ? -1 : 1;
  }
  return 0;
}

__device__ std::size_t find_voxel(
  const voxel_indices_accessor_t indices,
  const common::int64_t x,
  const common::int64_t y,
  const common::int64_t z) noexcept
{
  std::size_t first = 0U;
  std::size_t last = indices.extent(0U);
  while (first < last) {
    const std::size_t middle = first + ((last - first) / 2U);
    if (compare_voxel_index(indices, middle, x, y, z) < 0) {
      first = middle + 1U;
    } else {
      last = middle;
    }
  }
  if (first < indices.extent(0U) &&
    compare_voxel_index(indices, first, x, y, z) == 0)
  {
    return first;
  }
  return indices.extent(0U);
}

__device__ common::float64_t squared_mahalanobis_distance(
  const voxel_means_accessor_t means,
  const voxel_information_accessor_t information,
  const std::size_t voxel,
  const common::float64_t point_x,
  const common::float64_t point_y,
  const common::float64_t point_z) noexcept
{
  const common::float64_t residual_x = point_x - means.at(voxel, 0U);
  const common::float64_t residual_y = point_y - means.at(voxel, 1U);
  const common::float64_t residual_z = point_z - means.at(voxel, 2U);
  const common::float64_t weighted_x =
    information.at(voxel, 0U) * residual_x +
    information.at(voxel, 3U) * residual_y +
    information.at(voxel, 6U) * residual_z;
  const common::float64_t weighted_y =
    information.at(voxel, 1U) * residual_x +
    information.at(voxel, 4U) * residual_y +
    information.at(voxel, 7U) * residual_z;
  const common::float64_t weighted_z =
    information.at(voxel, 2U) * residual_x +
    information.at(voxel, 5U) * residual_y +
    information.at(voxel, 8U) * residual_z;
  return residual_x * weighted_x +
         residual_y * weighted_y +
         residual_z * weighted_z;
}

}  // namespace

namespace detail
{

class cuda_ndt_evaluation_factory_c final
{
public:
  using accumulator_t = ndt_evaluation_accumulator_s;

  cuda_ndt_evaluation_factory_c(
    const common::float64_t voxel_size_m,
    const common::uint32_t neighbor_radius_voxels,
    const common::float64_t maximum_mahalanobis_distance_squared) noexcept
  : m_voxel_size_m(voxel_size_m),
    m_neighbor_radius_voxels(neighbor_radius_voxels),
    m_maximum_mahalanobis_distance_squared(
      maximum_mahalanobis_distance_squared)
  {
  }

  std::unique_ptr<accelerator::operation_provider_i> operator()(
    const accelerator::cuda::operation_context_s & context) const
  {
    const common::float64_t voxel_size_m = m_voxel_size_m;
    const common::uint32_t neighbor_radius_voxels =
      m_neighbor_radius_voxels;
    const common::float64_t maximum_mahalanobis_distance_squared =
      m_maximum_mahalanobis_distance_squared;

    return accelerator::cuda::transform_reduce<
      accumulator_t,
      scan_accessor_t,
      voxel_indices_accessor_t,
      voxel_means_accessor_t,
      voxel_information_accessor_t,
      pose_accessor_t>(
      accelerator::cuda::leading_dimensions(0U, 1U),
      accumulator_t{},
      [voxel_size_m,
      neighbor_radius_voxels,
      maximum_mahalanobis_distance_squared] __device__(
        const std::size_t point_index,
        const scan_accessor_t scan,
        const voxel_indices_accessor_t voxel_indices,
        const voxel_means_accessor_t voxel_means,
        const voxel_information_accessor_t voxel_information,
        const pose_accessor_t pose) {
        accumulator_t contribution{};
        const point_3f_s point = scan[point_index];
        const common::float64_t point_x =
          pose[0U] * point.x_m + pose[1U] * point.y_m +
          pose[2U] * point.z_m + pose[9U];
        const common::float64_t point_y =
          pose[3U] * point.x_m + pose[4U] * point.y_m +
          pose[5U] * point.z_m + pose[10U];
        const common::float64_t point_z =
          pose[6U] * point.x_m + pose[7U] * point.y_m +
          pose[8U] * point.z_m + pose[11U];

        common::int64_t center_x = 0;
        common::int64_t center_y = 0;
        common::int64_t center_z = 0;
        if (!try_voxel_coordinate(point_x, voxel_size_m, center_x) ||
          !try_voxel_coordinate(point_y, voxel_size_m, center_y) ||
          !try_voxel_coordinate(point_z, voxel_size_m, center_z))
        {
          return contribution;
        }

        const common::int64_t radius =
          static_cast<common::int64_t>(neighbor_radius_voxels);
        std::size_t best_voxel = voxel_indices.extent(0U);
        constexpr common::float64_t MAXIMUM_FINITE_DISTANCE =
          0x1.fffffffffffffp+1023;
        common::float64_t best_distance = MAXIMUM_FINITE_DISTANCE;
        for (common::int64_t x_offset = -radius;
          x_offset <= radius; ++x_offset)
        {
          for (common::int64_t y_offset = -radius;
            y_offset <= radius; ++y_offset)
          {
            for (common::int64_t z_offset = -radius;
              z_offset <= radius; ++z_offset)
            {
              common::int64_t x = 0;
              common::int64_t y = 0;
              common::int64_t z = 0;
              if (!try_add_offset(center_x, x_offset, x) ||
                !try_add_offset(center_y, y_offset, y) ||
                !try_add_offset(center_z, z_offset, z))
              {
                continue;
              }
              const std::size_t voxel = find_voxel(voxel_indices, x, y, z);
              if (voxel == voxel_indices.extent(0U)) {
                continue;
              }
              const common::float64_t distance =
                squared_mahalanobis_distance(
                voxel_means,
                voxel_information,
                voxel,
                point_x,
                point_y,
                point_z);
              if (isfinite(distance) && distance < best_distance) {
                best_distance = distance;
                best_voxel = voxel;
              }
            }
          }
        }

        if (best_voxel == voxel_indices.extent(0U) ||
          best_distance > maximum_mahalanobis_distance_squared)
        {
          return contribution;
        }

        contribution.cost = 0.5 * (best_distance > 0.0 ? best_distance : 0.0);
        contribution.correspondence_count = 1U;
        if (pose[12U] < 0.5) {
          return contribution;
        }

        const common::float64_t residual[3U]{
          point_x - voxel_means.at(best_voxel, 0U),
          point_y - voxel_means.at(best_voxel, 1U),
          point_z - voxel_means.at(best_voxel, 2U)};
        const common::float64_t weighted_residual[3U]{
          voxel_information.at(best_voxel, 0U) * residual[0U] +
          voxel_information.at(best_voxel, 3U) * residual[1U] +
          voxel_information.at(best_voxel, 6U) * residual[2U],
          voxel_information.at(best_voxel, 1U) * residual[0U] +
          voxel_information.at(best_voxel, 4U) * residual[1U] +
          voxel_information.at(best_voxel, 7U) * residual[2U],
          voxel_information.at(best_voxel, 2U) * residual[0U] +
          voxel_information.at(best_voxel, 5U) * residual[1U] +
          voxel_information.at(best_voxel, 8U) * residual[2U]};
        const common::float64_t jacobian[18U]{
          1.0, 0.0, 0.0, 0.0, point_z, -point_y,
          0.0, 1.0, 0.0, -point_z, 0.0, point_x,
          0.0, 0.0, 1.0, point_y, -point_x, 0.0};
        common::float64_t weighted_jacobian[18U]{};
        for (std::size_t parameter = 0U; parameter < 6U; ++parameter) {
          contribution.gradient[parameter] =
            jacobian[parameter] * weighted_residual[0U] +
            jacobian[6U + parameter] * weighted_residual[1U] +
            jacobian[12U + parameter] * weighted_residual[2U];
          for (std::size_t row = 0U; row < 3U; ++row) {
            weighted_jacobian[(row * 6U) + parameter] =
              voxel_information.at(best_voxel, row) * jacobian[parameter] +
              voxel_information.at(best_voxel, 3U + row) *
              jacobian[6U + parameter] +
              voxel_information.at(best_voxel, 6U + row) *
              jacobian[12U + parameter];
          }
        }

        std::size_t upper_index = 0U;
        for (std::size_t row = 0U; row < 6U; ++row) {
          for (std::size_t column = row; column < 6U; ++column) {
            contribution.hessian_upper[upper_index] =
              jacobian[row] * weighted_jacobian[column] +
              jacobian[6U + row] * weighted_jacobian[6U + column] +
              jacobian[12U + row] * weighted_jacobian[12U + column];
            ++upper_index;
          }
        }
        return contribution;
      },
      [] __device__(
        const accumulator_t left,
        const accumulator_t right) {
        accumulator_t sum{};
        for (std::size_t index = 0U;
          index < UPPER_HESSIAN_ELEMENT_COUNT; ++index)
        {
          sum.hessian_upper[index] =
            left.hessian_upper[index] + right.hessian_upper[index];
        }
        for (std::size_t index = 0U; index < 6U; ++index) {
          sum.gradient[index] = left.gradient[index] + right.gradient[index];
        }
        sum.cost = left.cost + right.cost;
        sum.correspondence_count =
          left.correspondence_count + right.correspondence_count;
        return sum;
      })(context);
  }

private:
  static constexpr std::size_t UPPER_HESSIAN_ELEMENT_COUNT =
    NDT_UPPER_HESSIAN_ELEMENT_COUNT;

  common::float64_t m_voxel_size_m;
  common::uint32_t m_neighbor_radius_voxels;
  common::float64_t m_maximum_mahalanobis_distance_squared;
};

}  // namespace detail

cuda_ndt_localizer_c::cuda_ndt_localizer_c(
  const ndt_map_c & map,
  const autonomy_config::Localization::Localizer & config,
  const autonomy_config::Localization::Accelerator & accelerator_config)
: accelerator_c(make_accelerator_config(accelerator_config)),
  m_config(checked_localizer_config(config)),
  m_evaluate(make_operation(detail::cuda_ndt_evaluation_factory_c{
      map.voxel_size_m(),
      static_cast<common::uint32_t>(
        m_config.get_neighbor_radius_voxels()),
      m_config.get_maximum_mahalanobis_distance_squared()})),
  m_scan(make_buffer<point_3f_s>(
      accelerator::tensor_shape_s<1U>{{
        static_cast<std::size_t>(m_config.get_maximum_scan_points())}})),
  m_voxel_indices(make_buffer<common::int64_t>(
      accelerator::tensor_shape_s<2U>{{map.voxel_count(), 3U}})),
  m_voxel_means(make_buffer<common::float64_t>(
      accelerator::tensor_shape_s<2U>{{map.voxel_count(), 3U}})),
  m_voxel_information(make_buffer<common::float64_t>(
      accelerator::tensor_shape_s<2U>{{map.voxel_count(), 9U}})),
  m_pose(make_buffer<common::float64_t>(
      accelerator::tensor_shape_s<1U>{{POSE_ELEMENT_COUNT}})),
  m_workspace(make_buffer<detail::ndt_evaluation_accumulator_s>(
      accelerator::tensor_shape_s<1U>{{REDUCTION_WORKSPACE_CAPACITY}})),
  m_result(make_buffer<detail::ndt_evaluation_accumulator_s>(
      accelerator::tensor_shape_s<1U>{{1U}}))
{
  if (!map.ready()) {
    throw std::invalid_argument("CUDA NDT localizer requires a ready map");
  }
  const std::size_t voxel_count = map.voxel_count();
  if (voxel_count > std::numeric_limits<std::size_t>::max() / 9U) {
    throw std::invalid_argument("CUDA NDT map size overflows host staging storage");
  }

  std::vector<common::int64_t> indices(voxel_count * 3U);
  std::vector<common::float64_t> means(voxel_count * 3U);
  std::vector<common::float64_t> information(voxel_count * 9U);
  if (!map.copy_distributions(indices, means, information)) {
    throw accelerator::accelerator_error_c("Failed to export NDT map distributions");
  }

  using indices_view_t =
    accelerator::host_tensor_view_c<const common::int64_t, 2U>;
  using values_view_t =
    accelerator::host_tensor_view_c<const common::float64_t, 2U>;
  const std::optional<indices_view_t> indices_view =
    indices_view_t::try_create_contiguous(
    std::span<const common::int64_t>{indices},
    accelerator::tensor_shape_s<2U>{{voxel_count, 3U}});
  const std::optional<values_view_t> means_view =
    values_view_t::try_create_contiguous(
    std::span<const common::float64_t>{means},
    accelerator::tensor_shape_s<2U>{{voxel_count, 3U}});
  const std::optional<values_view_t> information_view =
    values_view_t::try_create_contiguous(
    std::span<const common::float64_t>{information},
    accelerator::tensor_shape_s<2U>{{voxel_count, 9U}});
  if (!indices_view.has_value() ||
    !means_view.has_value() ||
    !information_view.has_value())
  {
    throw accelerator::accelerator_error_c("Failed to create NDT map host views");
  }

  accelerator::status_e status = upload(*indices_view, m_voxel_indices);
  if (status == accelerator::status_e::success) {
    status = upload(*means_view, m_voxel_means);
  }
  if (status == accelerator::status_e::success) {
    status = upload(*information_view, m_voxel_information);
  }
  if (status != accelerator::status_e::success) {
    throw accelerator::accelerator_error_c("Failed to upload NDT map to CUDA");
  }
}

}  // namespace localization
