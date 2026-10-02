#pragma once

#include "accelerator/accelerator.hpp"
#include "accelerator/buffer.hpp"
#include "accelerator/operation.hpp"
#include "common/numeric_types.hpp"
#include "localization/ndt_localizer.hpp"
#include "localization/ndt_map.hpp"
#include "localization/status.hpp"
#include "localization/types.hpp"
#include "localization/visibility_control.hpp"

#include <cstddef>
#include <span>

namespace localization
{

namespace detail
{

inline constexpr std::size_t NDT_UPPER_HESSIAN_ELEMENT_COUNT = 21U;

struct ndt_evaluation_accumulator_s
{
  common::float64_t hessian_upper[NDT_UPPER_HESSIAN_ELEMENT_COUNT]{};
  common::float64_t gradient[6U]{};
  common::float64_t cost{0.0};
  common::uint64_t correspondence_count{0U};
};

}  // namespace detail

/// CUDA NDT evaluator with bounded CPU optimization control.
///
/// Construction uploads an immutable snapshot of the map. localize() uploads one bounded scan and
/// keeps it device-resident across all iterations. An instance is not safe for concurrent calls.
class LOCALIZATION_PUBLIC cuda_ndt_localizer_c final : private accelerator::accelerator_c
{
public:
  cuda_ndt_localizer_c(
    const ndt_map_c & map,
    const autonomy_config::Localization::Localizer & config,
    const autonomy_config::Localization::Accelerator & accelerator_config);
  ~cuda_ndt_localizer_c() noexcept;

  cuda_ndt_localizer_c(const cuda_ndt_localizer_c &) = delete;
  cuda_ndt_localizer_c & operator=(const cuda_ndt_localizer_c &) = delete;
  cuda_ndt_localizer_c(cuda_ndt_localizer_c &&) = delete;
  cuda_ndt_localizer_c & operator=(cuda_ndt_localizer_c &&) = delete;

  status_e localize(
    std::span<const point_3f_s> scan,
    const pose_3d_s & initial_pose_map_from_scan,
    localization_result_s & result) noexcept;

private:
  static constexpr std::size_t POSE_ELEMENT_COUNT = 13U;
  static constexpr std::size_t REDUCTION_WORKSPACE_CAPACITY = 256U;

  autonomy_config::Localization::Localizer m_config;
  accelerator::operation_c m_evaluate;
  accelerator::buffer_c<point_3f_s, 1U> m_scan;
  accelerator::buffer_c<common::int64_t, 2U> m_voxel_indices;
  accelerator::buffer_c<common::float64_t, 2U> m_voxel_means;
  accelerator::buffer_c<common::float64_t, 2U> m_voxel_information;
  accelerator::buffer_c<common::float64_t, 1U> m_pose;
  accelerator::buffer_c<detail::ndt_evaluation_accumulator_s, 1U> m_workspace;
  accelerator::buffer_c<detail::ndt_evaluation_accumulator_s, 1U> m_result;
};

}  // namespace localization
