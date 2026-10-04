#include "ai_diagnostics/resource_monitor.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>

namespace ai_diagnostics
{
namespace
{

struct cpu_time_sample_s
{
  common::uint64_t total{0U};
  common::uint64_t idle{0U};
};

bool read_cpu_time_sample(cpu_time_sample_s & sample) noexcept
{
  try {
    std::ifstream statistics("/proc/stat");
    std::string cpu_label;
    common::uint64_t user = 0U;
    common::uint64_t nice = 0U;
    common::uint64_t system = 0U;
    common::uint64_t idle = 0U;
    common::uint64_t io_wait = 0U;
    common::uint64_t interrupt = 0U;
    common::uint64_t soft_interrupt = 0U;
    common::uint64_t steal = 0U;
    if (!(statistics >> cpu_label >> user >> nice >> system >> idle >>
      io_wait >> interrupt >> soft_interrupt >> steal) ||
      cpu_label != "cpu")
    {
      return false;
    }

    sample.idle = idle + io_wait;
    sample.total = user + nice + system + sample.idle + interrupt +
      soft_interrupt + steal;
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace

linux_cpu_monitor_c::linux_cpu_monitor_c(
  const common::float64_t maximum_cpu_percent,
  const common::uint32_t required_idle_samples)
: m_maximum_cpu_percent(maximum_cpu_percent),
  m_required_idle_samples(required_idle_samples),
  m_idle_sample_count(0U),
  m_previous_total_time(0U),
  m_previous_idle_time(0U),
  m_has_previous_sample(false)
{
  if (!std::isfinite(m_maximum_cpu_percent) ||
    m_maximum_cpu_percent < 0.0 || m_maximum_cpu_percent > 100.0)
  {
    throw std::invalid_argument("CPU threshold is outside [0, 100]");
  }
  if (m_required_idle_samples == 0U) {
    throw std::invalid_argument("Required idle sample count cannot be zero");
  }
}

bool linux_cpu_monitor_c::resources_available() noexcept
{
  cpu_time_sample_s sample;
  if (!read_cpu_time_sample(sample)) {
    m_idle_sample_count = 0U;
    return false;
  }

  if (!m_has_previous_sample) {
    m_previous_total_time = sample.total;
    m_previous_idle_time = sample.idle;
    m_has_previous_sample = true;
    return false;
  }
  if (sample.total <= m_previous_total_time ||
    sample.idle < m_previous_idle_time)
  {
    m_previous_total_time = sample.total;
    m_previous_idle_time = sample.idle;
    m_idle_sample_count = 0U;
    return false;
  }

  const common::uint64_t total_delta = sample.total - m_previous_total_time;
  const common::uint64_t idle_delta = sample.idle - m_previous_idle_time;
  m_previous_total_time = sample.total;
  m_previous_idle_time = sample.idle;

  const common::uint64_t busy_delta = total_delta > idle_delta ?
    total_delta - idle_delta : 0U;
  const common::float64_t cpu_percent =
    (static_cast<common::float64_t>(busy_delta) * 100.0) /
    static_cast<common::float64_t>(total_delta);
  if (cpu_percent <= m_maximum_cpu_percent) {
    m_idle_sample_count = std::min(
      m_idle_sample_count + 1U,
      m_required_idle_samples);
  } else {
    m_idle_sample_count = 0U;
  }
  return m_idle_sample_count >= m_required_idle_samples;
}

}  // namespace ai_diagnostics
