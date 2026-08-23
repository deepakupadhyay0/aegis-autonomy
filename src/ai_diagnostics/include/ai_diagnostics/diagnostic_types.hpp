#pragma once

#include "common/fixed_string.hpp"
#include "common/numeric_types.hpp"

#include <vector>

namespace ai_diagnostics
{

struct diagnostic_event_s
{
  common::string32_t timestamp;
  common::string128_t evidence_id;
  common::uint8_t level{0U};
  common::uint32_t source_line{0U};
  common::string64_t source_node;
  common::string64_t source_file;
  common::string256_t fault;
};

struct diagnostic_batch_s
{
  std::vector<diagnostic_event_s> events;
};

struct diagnostic_report_s
{
  common::string256_t probable_cause;
  common::string256_t predicted_failure;
  common::string256_t recommended_action;
  std::vector<common::string128_t> evidence_ids;
  common::float32_t confidence{0.0F};
  bool insufficient_evidence{false};
  bool potentially_recoverable{false};
};

}  // namespace ai_diagnostics
