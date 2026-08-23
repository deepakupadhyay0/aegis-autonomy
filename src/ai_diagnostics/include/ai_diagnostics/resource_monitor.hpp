#pragma once

#include "ai_diagnostics/visibility_control.hpp"
#include "common/numeric_types.hpp"

namespace ai_diagnostics
{

class AI_DIAGNOSTICS_PUBLIC resource_monitor_c
{
public:
  resource_monitor_c() = default;
  virtual ~resource_monitor_c() noexcept = default;

  resource_monitor_c(const resource_monitor_c &) = delete;
  resource_monitor_c & operator=(const resource_monitor_c &) = delete;
  resource_monitor_c(resource_monitor_c &&) = delete;
  resource_monitor_c & operator=(resource_monitor_c &&) = delete;

  virtual bool resources_available() noexcept = 0;
};

class AI_DIAGNOSTICS_PUBLIC linux_cpu_monitor_c final
  : public resource_monitor_c
{
public:
  linux_cpu_monitor_c(
    common::float64_t maximum_cpu_percent,
    common::uint32_t required_idle_samples);
  ~linux_cpu_monitor_c() noexcept override = default;

  linux_cpu_monitor_c(const linux_cpu_monitor_c &) = delete;
  linux_cpu_monitor_c & operator=(const linux_cpu_monitor_c &) = delete;
  linux_cpu_monitor_c(linux_cpu_monitor_c &&) = delete;
  linux_cpu_monitor_c & operator=(linux_cpu_monitor_c &&) = delete;

  bool resources_available() noexcept override;

private:
  common::float64_t m_maximum_cpu_percent;
  common::uint32_t m_required_idle_samples;
  common::uint32_t m_idle_sample_count;
  common::uint64_t m_previous_total_time;
  common::uint64_t m_previous_idle_time;
  bool m_has_previous_sample;
};

}  // namespace ai_diagnostics
