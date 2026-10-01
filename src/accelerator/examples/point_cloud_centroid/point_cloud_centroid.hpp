#pragma once

#include "accelerator/types.hpp"
#include "common/numeric_types.hpp"

#include <cstddef>
#include <span>

namespace accelerator_examples::point_cloud_centroid
{

struct point_sum_s
{
  common::float32_t x{0.0F};
  common::float32_t y{0.0F};
  common::float32_t z{0.0F};
  common::uint64_t count{0U};
};

static_assert(accelerator::accelerator_value<point_sum_s>);

using scalar_t = common::float32_t;
using points_shape_t = accelerator::tensor_shape_s<2U>;
using accumulator_shape_t = accelerator::tensor_shape_s<1U>;
using points_buffer_t = accelerator::buffer_c<scalar_t, 2U>;
using accumulator_buffer_t = accelerator::buffer_c<point_sum_s, 1U>;

inline constexpr std::size_t POINT_DIMENSION = 3U;

class point_cloud_centroid_c final : private accelerator::accelerator_c
{
public:
  explicit point_cloud_centroid_c(std::size_t point_count);
  ~point_cloud_centroid_c() noexcept;

  point_cloud_centroid_c(const point_cloud_centroid_c &) = delete;
  point_cloud_centroid_c & operator=(const point_cloud_centroid_c &) = delete;
  point_cloud_centroid_c(point_cloud_centroid_c &&) = delete;
  point_cloud_centroid_c & operator=(point_cloud_centroid_c &&) = delete;

  accelerator::status_e process(
    std::span<const scalar_t> points,
    std::size_t point_count,
    point_sum_s & point_sum) noexcept;

private:
  static constexpr std::size_t WORKSPACE_CAPACITY = 32U;

  accelerator::operation_c m_point_sum;
  points_buffer_t m_points;
  accumulator_buffer_t m_workspace;
  accumulator_buffer_t m_result;
};

}  // namespace accelerator_examples::point_cloud_centroid
