#include "point_cloud_centroid.hpp"

#include <array>
#include <exception>
#include <iostream>
#include <optional>
#include <utility>

namespace accelerator_examples::point_cloud_centroid
{

point_cloud_centroid_c::~point_cloud_centroid_c() noexcept = default;

accelerator::status_e point_cloud_centroid_c::process(
  const std::span<const scalar_t> points,
  const std::size_t point_count,
  point_sum_s & point_sum) noexcept
{
  using points_host_t =
    accelerator::host_tensor_view_c<const scalar_t, 2U>;
  using accumulator_host_t =
    accelerator::host_tensor_view_c<point_sum_s, 1U>;

  const points_shape_t points_shape{{
    point_count,
    POINT_DIMENSION}};
  const accumulator_shape_t result_shape{{1U}};
  const std::optional<points_host_t> points_view =
    points_host_t::try_create_contiguous(points, points_shape);
  const std::optional<accumulator_host_t> result_view =
    accumulator_host_t::try_create_contiguous(
      std::span<point_sum_s>{&point_sum, 1U}, result_shape);
  if (!points_view.has_value() || !result_view.has_value() ||
    points_shape != m_points.shape())
  {
    return accelerator::status_e::shape_mismatch;
  }

  accelerator::status_e status = upload(*points_view, m_points);
  if (status != accelerator::status_e::success) {
    return status;
  }
  status = m_point_sum.execute(
    std::as_const(m_points), m_workspace, m_result);
  if (status != accelerator::status_e::success) {
    return status;
  }
  return download(m_result, *result_view);
}

}  // namespace accelerator_examples::point_cloud_centroid

int main()
{
  using accelerator_examples::point_cloud_centroid::POINT_DIMENSION;
  using accelerator_examples::point_cloud_centroid::point_cloud_centroid_c;
  using accelerator_examples::point_cloud_centroid::point_sum_s;
  using accelerator_examples::point_cloud_centroid::scalar_t;

  constexpr std::size_t POINT_COUNT = 4U;
  const std::array<scalar_t, POINT_COUNT * POINT_DIMENSION> points{
    0.0F, 0.0F, 0.0F,
    2.0F, 0.0F, 0.0F,
    0.0F, 4.0F, 0.0F,
    2.0F, 4.0F, 8.0F};
  point_sum_s point_sum{};

  try {
    point_cloud_centroid_c centroid(POINT_COUNT);
    const accelerator::status_e status =
      centroid.process(points, POINT_COUNT, point_sum);
    if (status != accelerator::status_e::success) {
      std::cerr << "Point-cloud reduction failed with status "
                << static_cast<common::uint32_t>(status) << '\n';
      return 1;
    }
  } catch (const std::exception & error) {
    std::cerr << "Point-cloud example initialization failed: "
              << error.what() << '\n';
    return 1;
  }

  if (point_sum.count == 0U) {
    std::cerr << "The point cloud was empty\n";
    return 1;
  }

  const scalar_t divisor = static_cast<scalar_t>(point_sum.count);
  std::cout << "Centroid: ["
            << point_sum.x / divisor << ", "
            << point_sum.y / divisor << ", "
            << point_sum.z / divisor << "]\n";
  return 0;
}
