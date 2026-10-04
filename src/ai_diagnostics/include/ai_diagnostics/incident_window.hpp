#pragma once

#include "ai_diagnostics/diagnostic_types.hpp"
#include "ai_diagnostics/visibility_control.hpp"

#include <cstddef>
#include <deque>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace ai_diagnostics
{

// Owned by the diagnostics worker thread. An incident begins on a warning or
// error and ends only when the same source explicitly reports healthy status.
class AI_DIAGNOSTICS_PUBLIC incident_window_c final
{
public:
  incident_window_c(
    std::size_t maximum_events,
    std::size_t maximum_context_bytes);

  std::vector<diagnostic_batch_s> ingest(
    std::span<const diagnostic_event_s> events);

private:
  void remember_healthy(const diagnostic_event_s & event);
  void begin_incident(const diagnostic_event_s & event);
  void append_event(const diagnostic_event_s & event);

  std::size_t m_maximum_events;
  std::size_t m_maximum_context_bytes;
  std::map<std::string, std::deque<diagnostic_event_s>> m_healthy_by_node;
  std::deque<std::string> m_healthy_node_order;
  diagnostic_batch_s m_active;
  common::string64_t m_root_node;
  common::string32_t m_last_root_fault_at;
};

}  // namespace ai_diagnostics
