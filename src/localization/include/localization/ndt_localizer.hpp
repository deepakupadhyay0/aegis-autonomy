#pragma once

#include "common/numeric_types.hpp"

#include <autonomy_config/localization.hpp>
#include "localization/ndt_map.hpp"
#include "localization/status.hpp"
#include "localization/types.hpp"
#include "localization/visibility_control.hpp"

#include <cstddef>
#include <span>

namespace localization
{

LOCALIZATION_PUBLIC void validate_ndt_localizer_config(
  const autonomy_config::Localization::Localizer & config);

struct localization_result_s
{
  pose_3d_s pose_map_from_scan{};
  common::float64_t mean_mahalanobis_cost{0.0};
  std::size_t correspondence_count{0U};
  common::uint32_t iteration_count{0U};
  bool converged{false};
};

/// Bounded CPU reference implementation of 3D scan-to-map NDT.
///
/// localize() performs no intentional heap allocation. The map and this object must not be mutated
/// while a call is active. Separate localizer instances may query the same immutable map.
class LOCALIZATION_PUBLIC ndt_localizer_c final
{
public:
  explicit ndt_localizer_c(
    const autonomy_config::Localization::Localizer & config);
  ~ndt_localizer_c() noexcept = default;

  ndt_localizer_c(const ndt_localizer_c &) = delete;
  ndt_localizer_c & operator=(const ndt_localizer_c &) = delete;
  ndt_localizer_c(ndt_localizer_c &&) = delete;
  ndt_localizer_c & operator=(ndt_localizer_c &&) = delete;

  status_e localize(
    const ndt_map_c & map,
    std::span<const point_3f_s> scan,
    const pose_3d_s & initial_pose_map_from_scan,
    localization_result_s & result) const noexcept;

private:
  autonomy_config::Localization::Localizer m_config;
};

}  // namespace localization
