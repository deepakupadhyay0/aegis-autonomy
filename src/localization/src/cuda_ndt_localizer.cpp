#include "localization/cuda_ndt_localizer.hpp"

#include "ndt_solver.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace localization
{

namespace
{

bool finite_point(const point_3f_s & point) noexcept
{
  return std::isfinite(point.x_m) &&
         std::isfinite(point.y_m) &&
         std::isfinite(point.z_m);
}

}  // namespace

cuda_ndt_localizer_c::~cuda_ndt_localizer_c() noexcept = default;

status_e cuda_ndt_localizer_c::localize(
  const std::span<const point_3f_s> scan,
  const pose_3d_s & initial_pose_map_from_scan,
  localization_result_s & result) noexcept
{
  result = localization_result_s{};
  if (scan.empty()) {
    return status_e::invalid_point_cloud;
  }
  if (scan.size() > static_cast<std::size_t>(m_config.get_maximum_scan_points())) {
    return status_e::capacity_exceeded;
  }
  for (const point_3f_s & point : scan) {
    if (!finite_point(point)) {
      return status_e::invalid_point_cloud;
    }
  }

  using scan_host_view_t =
    accelerator::host_tensor_view_c<const point_3f_s, 1U>;
  const accelerator::tensor_shape_s<1U> scan_shape{{scan.size()}};
  const std::optional<scan_host_view_t> scan_host_view =
    scan_host_view_t::try_create_contiguous(scan, scan_shape);
  std::optional<accelerator::device_tensor_view_c<point_3f_s, 1U>>
  scan_device_view = m_scan.try_view<1U>(
    scan_shape, accelerator::tensor_strides_s<1U>{{1U}});
  if (!scan_host_view.has_value() || !scan_device_view.has_value()) {
    return status_e::accelerator_failure;
  }

  accelerator::status_e accelerator_status =
    upload(*scan_host_view, *scan_device_view);
  if (accelerator_status != accelerator::status_e::success) {
    return status_e::accelerator_failure;
  }
  const accelerator::device_tensor_view_c<const point_3f_s, 1U>
  const_scan_device_view{*scan_device_view};

  const auto evaluate =
    [this, &const_scan_device_view](
    const ndt_solver::pose_state_s & pose,
    const bool calculate_derivatives,
    ndt_solver::normal_equation_s & equation) noexcept
    {
      std::array<common::float64_t, POSE_ELEMENT_COUNT> pose_values{};
      for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t column = 0U; column < 3U; ++column) {
          pose_values[(row * 3U) + column] = pose.rotation(
            static_cast<Eigen::Index>(row),
            static_cast<Eigen::Index>(column));
        }
        pose_values[9U + row] = pose.translation[static_cast<Eigen::Index>(row)];
      }
      pose_values[12U] = calculate_derivatives ? 1.0 : 0.0;

      using pose_host_view_t =
        accelerator::host_tensor_view_c<const common::float64_t, 1U>;
      using result_host_view_t =
        accelerator::host_tensor_view_c<detail::ndt_evaluation_accumulator_s, 1U>;
      const std::optional<pose_host_view_t> pose_view =
        pose_host_view_t::try_create_contiguous(
        std::span<const common::float64_t>{pose_values},
        accelerator::tensor_shape_s<1U>{{POSE_ELEMENT_COUNT}});
      detail::ndt_evaluation_accumulator_s reduced{};
      const std::optional<result_host_view_t> result_view =
        result_host_view_t::try_create_contiguous(
        std::span<detail::ndt_evaluation_accumulator_s>{&reduced, 1U},
        accelerator::tensor_shape_s<1U>{{1U}});
      if (!pose_view.has_value() || !result_view.has_value()) {
        return status_e::accelerator_failure;
      }

      accelerator::status_e status = upload(*pose_view, m_pose);
      if (status != accelerator::status_e::success) {
        return status_e::accelerator_failure;
      }
      status = m_evaluate.execute(
        const_scan_device_view,
        std::as_const(m_voxel_indices),
        std::as_const(m_voxel_means),
        std::as_const(m_voxel_information),
        std::as_const(m_pose),
        m_workspace,
        m_result);
      if (status != accelerator::status_e::success) {
        return status_e::accelerator_failure;
      }
      status = download(m_result, *result_view);
      if (status != accelerator::status_e::success ||
        reduced.correspondence_count >
        static_cast<common::uint64_t>(std::numeric_limits<std::size_t>::max()))
      {
        return status_e::accelerator_failure;
      }

      equation = ndt_solver::normal_equation_s{};
      equation.cost = reduced.cost;
      equation.correspondence_count =
        static_cast<std::size_t>(reduced.correspondence_count);
      std::size_t upper_index = 0U;
      for (std::size_t row = 0U; row < 6U; ++row) {
        equation.gradient[static_cast<Eigen::Index>(row)] =
          reduced.gradient[row];
        for (std::size_t column = row; column < 6U; ++column) {
          const common::float64_t value = reduced.hessian_upper[upper_index];
          equation.hessian(
            static_cast<Eigen::Index>(row),
            static_cast<Eigen::Index>(column)) = value;
          equation.hessian(
            static_cast<Eigen::Index>(column),
            static_cast<Eigen::Index>(row)) = value;
          ++upper_index;
        }
      }
      return status_e::success;
    };

  return ndt_solver::solve(
    m_config, initial_pose_map_from_scan, result, evaluate);
}

}  // namespace localization
