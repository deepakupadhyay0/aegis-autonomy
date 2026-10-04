#pragma once

#include "common/fixed_string.hpp"
#include "common/numeric_types.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace ai_diagnostics
{

inline constexpr std::size_t MAX_DIAGNOSTIC_MEASUREMENTS = 8U;

struct diagnostic_measurement_s
{
  // Include the unit in the key, for example lidar_odom_offset_ms.
  common::string64_t name;
  common::string64_t value;
};

struct diagnostic_event_s
{
  common::string32_t timestamp;
  common::string128_t evidence_id;
  common::uint8_t level{0U};
  common::uint32_t source_line{0U};
  common::string64_t source_node;
  common::string64_t source_file;
  common::string256_t fault;
  std::vector<diagnostic_measurement_s> measurements;
  bool healthy{false};
  bool truncated{false};
};

struct diagnostic_batch_s
{
  std::vector<diagnostic_event_s> events;
  std::vector<diagnostic_event_s> baseline;
  common::string128_t incident_id;
  common::string32_t as_of;
  bool truncated{false};
  bool recovery_observed{false};
};

struct diagnostic_report_s
{
  // Unverified model prose. Incident identity and state come from the node.
  std::string analysis;
};

}  // namespace ai_diagnostics
