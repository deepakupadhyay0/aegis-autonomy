#pragma once

#include "common/fixed_string.hpp"
#include "common/numeric_types.hpp"

namespace ai_diagnostics
{

struct diagnostic_event_s
{
  common::uint8_t level{0U};
  common::uint32_t source_line{0U};
  common::string64_t source_node;
  common::string64_t source_file;
  common::string256_t fault;
};

struct diagnostic_report_s
{
  common::string256_t probable_cause;
  common::string256_t recommended_action;
  bool potentially_recoverable{false};
};

}  // namespace ai_diagnostics
