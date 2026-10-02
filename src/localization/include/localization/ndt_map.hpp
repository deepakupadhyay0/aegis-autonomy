#pragma once

#include "common/numeric_types.hpp"

#include <autonomy_config/localization.hpp>
#include "localization/status.hpp"
#include "localization/types.hpp"
#include "localization/visibility_control.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <span>

namespace localization
{

class cuda_ndt_localizer_c;
class ndt_localizer_c;

/// Owns an immutable set of Gaussian voxel distributions after rebuild() succeeds.
///
/// rebuild() allocates and is not a real-time operation. Once built, const localization queries may
/// run concurrently as long as rebuild() and destruction are externally excluded.
class LOCALIZATION_PUBLIC ndt_map_c final
{
public:
  explicit ndt_map_c(const autonomy_config::Localization::Map & config);
  ~ndt_map_c() noexcept;

  ndt_map_c(const ndt_map_c &) = delete;
  ndt_map_c & operator=(const ndt_map_c &) = delete;
  ndt_map_c(ndt_map_c &&) = delete;
  ndt_map_c & operator=(ndt_map_c &&) = delete;

  status_e rebuild(std::span<const point_3f_s> points);

  bool ready() const noexcept;
  common::float64_t voxel_size_m() const noexcept;
  std::size_t source_point_count() const noexcept;
  std::size_t voxel_count() const noexcept;

private:
  friend class cuda_ndt_localizer_c;
  friend class ndt_localizer_c;

  struct distribution_s
  {
    std::array<common::float64_t, 3U> mean_m{};
    std::array<common::float64_t, 9U> information{};
  };
  class implementation_c;

  const distribution_s * best_distribution(
    const std::array<common::float64_t, 3U> & point_map_m,
    common::uint32_t neighbor_radius) const noexcept;

  bool copy_distributions(
    std::span<common::int64_t> indices,
    std::span<common::float64_t> means,
    std::span<common::float64_t> information) const noexcept;

  autonomy_config::Localization::Map m_config;
  std::unique_ptr<implementation_c> m_implementation;
};

}  // namespace localization
