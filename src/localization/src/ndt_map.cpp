#include "localization/ndt_map.hpp"

#include <Eigen/Core>
#include <Eigen/Eigenvalues>
#include <Eigen/LU>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace localization
{

namespace
{

using vector3_t = Eigen::Matrix<common::float64_t, 3, 1>;
using matrix3_t = Eigen::Matrix<common::float64_t, 3, 3>;

struct voxel_index_s
{
  common::int64_t x{0};
  common::int64_t y{0};
  common::int64_t z{0};

  friend bool operator==(
    const voxel_index_s & left,
    const voxel_index_s & right) noexcept = default;
};

bool voxel_index_less(
  const voxel_index_s & left,
  const voxel_index_s & right) noexcept
{
  if (left.x != right.x) {
    return left.x < right.x;
  }
  if (left.y != right.y) {
    return left.y < right.y;
  }
  return left.z < right.z;
}

struct voxel_index_hash_c
{
  std::size_t operator()(const voxel_index_s & index) const noexcept
  {
    const std::size_t x_hash = std::hash<common::int64_t>{}(index.x);
    const std::size_t y_hash = std::hash<common::int64_t>{}(index.y);
    const std::size_t z_hash = std::hash<common::int64_t>{}(index.z);
    std::size_t seed = x_hash;
    seed ^= y_hash + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    seed ^= z_hash + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    return seed;
  }
};

struct voxel_accumulator_s
{
  std::size_t point_count{0U};
  vector3_t mean{vector3_t::Zero()};
  matrix3_t centered_product_sum{matrix3_t::Zero()};

  void add(const vector3_t & point) noexcept
  {
    ++point_count;
    const vector3_t difference = point - mean;
    mean += difference / static_cast<common::float64_t>(point_count);
    const vector3_t updated_difference = point - mean;
    centered_product_sum += difference * updated_difference.transpose();
  }
};

bool finite_point(const point_3f_s & point) noexcept
{
  return std::isfinite(point.x_m) &&
         std::isfinite(point.y_m) &&
         std::isfinite(point.z_m);
}

bool valid_map_config(
  const autonomy_config::Localization::Map & config) noexcept
{
  const common::int64_t maximum_points = config.get_maximum_points();
  const common::int64_t maximum_voxels = config.get_maximum_voxels();
  return std::isfinite(config.get_voxel_size_m()) &&
         config.get_voxel_size_m() > 0.0 &&
         config.get_minimum_points_per_voxel() >= 4 &&
         std::isfinite(config.get_minimum_covariance_eigenvalue()) &&
         config.get_minimum_covariance_eigenvalue() > 0.0 &&
         std::isfinite(config.get_covariance_regularization_ratio()) &&
         config.get_covariance_regularization_ratio() > 0.0 &&
         config.get_covariance_regularization_ratio() <= 1.0 &&
         maximum_points > 0 &&
         static_cast<common::uint64_t>(maximum_points) <=
         static_cast<common::uint64_t>(std::numeric_limits<std::size_t>::max()) &&
         maximum_voxels > 0 &&
         static_cast<common::uint64_t>(maximum_voxels) <=
         static_cast<common::uint64_t>(std::numeric_limits<std::size_t>::max());
}

std::optional<voxel_index_s> try_voxel_index(
  const std::array<common::float64_t, 3U> & point_m,
  const common::float64_t voxel_size_m) noexcept
{
  const common::float64_t minimum_index =
    static_cast<common::float64_t>(std::numeric_limits<common::int64_t>::min());
  const common::float64_t maximum_index = std::nextafter(
    static_cast<common::float64_t>(
      std::numeric_limits<common::int64_t>::max()),
    0.0);

  std::array<common::float64_t, 3U> index_values{};
  for (std::size_t dimension = 0U; dimension < index_values.size(); ++dimension) {
    const common::float64_t index_value = std::floor(point_m[dimension] / voxel_size_m);
    if (!std::isfinite(index_value) ||
      index_value < minimum_index ||
      index_value > maximum_index)
    {
      return std::nullopt;
    }
    index_values[dimension] = index_value;
  }

  return voxel_index_s{
    static_cast<common::int64_t>(index_values[0U]),
    static_cast<common::int64_t>(index_values[1U]),
    static_cast<common::int64_t>(index_values[2U])};
}

bool try_add_index_offset(
  const common::int64_t index,
  const common::int64_t offset,
  common::int64_t & result) noexcept
{
  if ((offset > 0 &&
    index > std::numeric_limits<common::int64_t>::max() - offset) ||
    (offset < 0 &&
    index < std::numeric_limits<common::int64_t>::min() - offset))
  {
    return false;
  }
  result = index + offset;
  return true;
}

}  // namespace

class ndt_map_c::implementation_c final
{
public:
  struct entry_s
  {
    voxel_index_s index{};
    distribution_s distribution{};
  };

  using distribution_container_t = std::vector<entry_s>;

  distribution_container_t distributions;
  std::size_t source_point_count{0U};
};

ndt_map_c::ndt_map_c(
  const autonomy_config::Localization::Map & config)
: m_config(config),
  m_implementation(std::make_unique<implementation_c>())
{
  if (!valid_map_config(config)) {
    throw std::invalid_argument("Invalid NDT map configuration");
  }
}

ndt_map_c::~ndt_map_c() noexcept = default;

status_e ndt_map_c::rebuild(const std::span<const point_3f_s> points)
{
  if (points.empty()) {
    return status_e::invalid_point_cloud;
  }
  if (points.size() > static_cast<std::size_t>(m_config.get_maximum_points())) {
    return status_e::capacity_exceeded;
  }

  using accumulator_map_t =
    std::unordered_map<voxel_index_s, voxel_accumulator_s, voxel_index_hash_c>;
  accumulator_map_t accumulators;
  accumulators.reserve(std::min(points.size(),
      static_cast<std::size_t>(m_config.get_maximum_voxels())));

  for (const point_3f_s & point : points) {
    if (!finite_point(point)) {
      return status_e::invalid_point_cloud;
    }
    const std::array<common::float64_t, 3U> point_values{
      static_cast<common::float64_t>(point.x_m),
      static_cast<common::float64_t>(point.y_m),
      static_cast<common::float64_t>(point.z_m)};
    const std::optional<voxel_index_s> index =
      try_voxel_index(point_values, m_config.get_voxel_size_m());
    if (!index.has_value()) {
      return status_e::invalid_point_cloud;
    }

    accumulator_map_t::iterator accumulator = accumulators.find(*index);
    if (accumulator == accumulators.end()) {
      if (accumulators.size() >= static_cast<std::size_t>(m_config.get_maximum_voxels())) {
        return status_e::capacity_exceeded;
      }
      const std::pair<accumulator_map_t::iterator, bool> insertion =
        accumulators.emplace(*index, voxel_accumulator_s{});
      accumulator = insertion.first;
    }
    accumulator->second.add(vector3_t{
        point_values[0U], point_values[1U], point_values[2U]});
  }

  std::unique_ptr<implementation_c> candidate =
    std::make_unique<implementation_c>();
  candidate->source_point_count = points.size();
  candidate->distributions.reserve(accumulators.size());

  for (const accumulator_map_t::value_type & entry : accumulators) {
    const voxel_accumulator_s & accumulator = entry.second;
    if (accumulator.point_count <
      static_cast<std::size_t>(m_config.get_minimum_points_per_voxel()))
    {
      continue;
    }

    matrix3_t covariance =
      accumulator.centered_product_sum /
      static_cast<common::float64_t>(accumulator.point_count - 1U);
    covariance = 0.5 * (covariance + covariance.transpose());

    Eigen::SelfAdjointEigenSolver<matrix3_t> eigen_solver{covariance};
    if (eigen_solver.info() != Eigen::Success) {
      continue;
    }

    const vector3_t eigenvalues = eigen_solver.eigenvalues();
    const common::float64_t largest_eigenvalue = eigenvalues.maxCoeff();
    if (!std::isfinite(largest_eigenvalue)) {
      continue;
    }
    const common::float64_t eigenvalue_floor = std::max(
      m_config.get_minimum_covariance_eigenvalue(),
      largest_eigenvalue * m_config.get_covariance_regularization_ratio());
    const vector3_t regularized_eigenvalues =
      eigenvalues.cwiseMax(eigenvalue_floor);
    covariance =
      eigen_solver.eigenvectors() *
      regularized_eigenvalues.asDiagonal() *
      eigen_solver.eigenvectors().transpose();
    const matrix3_t information = covariance.inverse();
    if (!information.allFinite()) {
      continue;
    }

    distribution_s distribution{};
    for (std::size_t row = 0U; row < 3U; ++row) {
      distribution.mean_m[row] =
        accumulator.mean[static_cast<Eigen::Index>(row)];
      for (std::size_t column = 0U; column < 3U; ++column) {
        distribution.information[(column * 3U) + row] = information(
          static_cast<Eigen::Index>(row),
          static_cast<Eigen::Index>(column));
      }
    }
    candidate->distributions.push_back(
      implementation_c::entry_s{entry.first, distribution});
  }

  std::sort(
    candidate->distributions.begin(),
    candidate->distributions.end(),
    [](const implementation_c::entry_s & left,
    const implementation_c::entry_s & right) noexcept
    {
      return voxel_index_less(left.index, right.index);
    });

  if (candidate->distributions.empty()) {
    return status_e::insufficient_map_structure;
  }

  m_implementation = std::move(candidate);
  return status_e::success;
}

bool ndt_map_c::ready() const noexcept
{
  return !m_implementation->distributions.empty();
}

common::float64_t ndt_map_c::voxel_size_m() const noexcept
{
  return m_config.get_voxel_size_m();
}

std::size_t ndt_map_c::source_point_count() const noexcept
{
  return m_implementation->source_point_count;
}

std::size_t ndt_map_c::voxel_count() const noexcept
{
  return m_implementation->distributions.size();
}

bool ndt_map_c::copy_distributions(
  const std::span<common::int64_t> indices,
  const std::span<common::float64_t> means,
  const std::span<common::float64_t> information) const noexcept
{
  const std::size_t count = m_implementation->distributions.size();
  if (indices.size() != count * 3U ||
    means.size() != count * 3U ||
    information.size() != count * 9U)
  {
    return false;
  }

  for (std::size_t voxel = 0U; voxel < count; ++voxel) {
    const implementation_c::entry_s & entry =
      m_implementation->distributions[voxel];
    indices[(voxel * 3U) + 0U] = entry.index.x;
    indices[(voxel * 3U) + 1U] = entry.index.y;
    indices[(voxel * 3U) + 2U] = entry.index.z;
    for (std::size_t dimension = 0U; dimension < 3U; ++dimension) {
      means[(voxel * 3U) + dimension] =
        entry.distribution.mean_m[dimension];
    }
    for (std::size_t element = 0U; element < 9U; ++element) {
      information[(voxel * 9U) + element] =
        entry.distribution.information[element];
    }
  }
  return true;
}

const ndt_map_c::distribution_s * ndt_map_c::best_distribution(
  const std::array<common::float64_t, 3U> & point_map_m,
  const common::uint32_t neighbor_radius) const noexcept
{
  const std::optional<voxel_index_s> center_index =
    try_voxel_index(point_map_m, m_config.get_voxel_size_m());
  if (!center_index.has_value()) {
    return nullptr;
  }

  const vector3_t point{
    point_map_m[0U], point_map_m[1U], point_map_m[2U]};
  const common::int64_t radius =
    static_cast<common::int64_t>(neighbor_radius);
  const distribution_s * best = nullptr;
  common::float64_t best_distance =
    std::numeric_limits<common::float64_t>::infinity();

  for (common::int64_t x_offset = -radius; x_offset <= radius; ++x_offset) {
    for (common::int64_t y_offset = -radius; y_offset <= radius; ++y_offset) {
      for (common::int64_t z_offset = -radius; z_offset <= radius; ++z_offset) {
        voxel_index_s index{};
        if (!try_add_index_offset(center_index->x, x_offset, index.x) ||
          !try_add_index_offset(center_index->y, y_offset, index.y) ||
          !try_add_index_offset(center_index->z, z_offset, index.z))
        {
          continue;
        }
        const implementation_c::distribution_container_t::const_iterator distribution =
          std::lower_bound(
          m_implementation->distributions.begin(),
          m_implementation->distributions.end(),
          index,
          [](const implementation_c::entry_s & entry,
          const voxel_index_s & target) noexcept
          {
            return voxel_index_less(entry.index, target);
          });
        if (distribution == m_implementation->distributions.end() ||
          !(distribution->index == index))
        {
          continue;
        }
        const Eigen::Map<const vector3_t> mean{
          distribution->distribution.mean_m.data()};
        const Eigen::Map<const matrix3_t> information{
          distribution->distribution.information.data()};
        const vector3_t residual = point - mean;
        const common::float64_t distance =
          residual.dot(information * residual);
        if (std::isfinite(distance) && distance < best_distance) {
          best_distance = distance;
          best = &distribution->distribution;
        }
      }
    }
  }
  return best;
}

}  // namespace localization
