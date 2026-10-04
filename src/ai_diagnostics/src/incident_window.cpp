#include "ai_diagnostics/incident_window.hpp"

#include "logging/log_types.hpp"

#include <stdexcept>
#include <utility>

namespace ai_diagnostics
{
namespace
{

constexpr std::size_t BASELINE_RECORDS = 5U;
constexpr std::size_t MAX_BASELINE_SOURCES = 32U;

std::size_t event_bytes(const diagnostic_event_s & event) noexcept
{
  constexpr std::size_t FIELD_OVERHEAD = 128U;
  std::size_t size = event.timestamp.size() + event.evidence_id.size() +
    event.source_node.size() + event.source_file.size() +
    event.fault.size() + FIELD_OVERHEAD;
  for (const diagnostic_measurement_s & measurement : event.measurements) {
    size += measurement.name.size() + measurement.value.size() + 32U;
  }
  return size;
}

std::size_t window_bytes(const diagnostic_batch_s & window) noexcept
{
  std::size_t size = 0U;
  for (const diagnostic_event_s & event : window.baseline) {
    size += event_bytes(event);
  }
  for (const diagnostic_event_s & event : window.events) {
    size += event_bytes(event);
  }
  return size;
}

}  // namespace

incident_window_c::incident_window_c(
  const std::size_t maximum_events,
  const std::size_t maximum_context_bytes)
: m_maximum_events(maximum_events),
  m_maximum_context_bytes(maximum_context_bytes)
{
  if (maximum_events < 2U || maximum_context_bytes < 1024U) {
    throw std::invalid_argument("Incident window limits are too small");
  }
}

void incident_window_c::remember_healthy(const diagnostic_event_s & event)
{
  const std::string node(event.source_node.view());
  std::map<std::string, std::deque<diagnostic_event_s>>::iterator position =
    m_healthy_by_node.find(node);
  if (position == m_healthy_by_node.end()) {
    if (m_healthy_by_node.size() == MAX_BASELINE_SOURCES) {
      m_healthy_by_node.erase(m_healthy_node_order.front());
      m_healthy_node_order.pop_front();
    }
    position = m_healthy_by_node.emplace(node, std::deque<diagnostic_event_s>{}).first;
    m_healthy_node_order.push_back(node);
  }
  std::deque<diagnostic_event_s> & baseline = position->second;
  baseline.push_back(event);
  if (baseline.size() > BASELINE_RECORDS) {
    baseline.pop_front();
  }
}

void incident_window_c::begin_incident(const diagnostic_event_s & event)
{
  m_active = diagnostic_batch_s{};
  m_root_node = event.source_node;
  m_last_root_fault_at = event.timestamp;
  m_active.incident_id = event.evidence_id;
  const std::map<std::string, std::deque<diagnostic_event_s>>::const_iterator
    position = m_healthy_by_node.find(std::string(event.source_node.view()));
  if (position != m_healthy_by_node.end()) {
    for (const diagnostic_event_s & baseline_event : position->second) {
      if (window_bytes(m_active) + event_bytes(baseline_event) +
        event_bytes(event) > m_maximum_context_bytes)
      {
        m_active.truncated = true;
        continue;
      }
      m_active.baseline.push_back(baseline_event);
    }
  }
  this->append_event(event);
}

void incident_window_c::append_event(const diagnostic_event_s & event)
{
  diagnostic_event_s retained = event;
  if (event.timestamp.view() > m_active.as_of.view()) {
    m_active.as_of = event.timestamp;
  }
  if (event.truncated) {
    m_active.truncated = true;
  }
  if (event_bytes(retained) > m_maximum_context_bytes) {
    retained.fault = "details truncated";
    retained.measurements.clear();
    retained.truncated = true;
    m_active.truncated = true;
  }
  while ((m_active.events.size() + m_active.baseline.size() >= m_maximum_events ||
    window_bytes(m_active) + event_bytes(retained) > m_maximum_context_bytes) &&
    !m_active.baseline.empty())
  {
    m_active.baseline.erase(m_active.baseline.begin());
    m_active.truncated = true;
  }
  while ((m_active.events.size() + m_active.baseline.size() >= m_maximum_events ||
    window_bytes(m_active) + event_bytes(retained) > m_maximum_context_bytes) &&
    m_active.events.size() > 1U)
  {
    m_active.events.erase(m_active.events.begin() + 1);
    m_active.truncated = true;
  }
  if (!m_active.events.empty() &&
    window_bytes(m_active) + event_bytes(retained) > m_maximum_context_bytes)
  {
    m_active.events.front().fault = "details truncated";
    m_active.events.front().measurements.clear();
    m_active.events.front().truncated = true;
    retained.fault = "details truncated";
    retained.measurements.clear();
    retained.truncated = true;
    m_active.truncated = true;
  }
  if (m_active.events.size() + m_active.baseline.size() < m_maximum_events &&
    window_bytes(m_active) + event_bytes(retained) <= m_maximum_context_bytes)
  {
    m_active.events.push_back(std::move(retained));
  } else {
    m_active.truncated = true;
  }
}

std::vector<diagnostic_batch_s> incident_window_c::ingest(
  const std::span<const diagnostic_event_s> events)
{
  std::vector<diagnostic_batch_s> ready;
  bool active_changed = false;
  for (const diagnostic_event_s & event : events) {
    const bool is_fault = !event.healthy &&
      event.level >= static_cast<common::uint8_t>(logging::log_level_e::warning);
    if (m_active.events.empty()) {
      if (event.healthy) {
        this->remember_healthy(event);
      } else if (is_fault) {
        this->begin_incident(event);
        active_changed = true;
      }
      continue;
    }

    if (event.healthy && event.source_node == m_root_node &&
      event.timestamp.view() < m_last_root_fault_at.view())
    {
      continue;
    }
    if (is_fault && event.source_node == m_root_node &&
      event.timestamp.view() > m_last_root_fault_at.view())
    {
      m_last_root_fault_at = event.timestamp;
    }
    this->append_event(event);
    active_changed = true;
    if (event.healthy && event.source_node == m_root_node) {
      m_active.recovery_observed = true;
      ready.push_back(std::move(m_active));
      m_active = diagnostic_batch_s{};
      m_root_node.clear();
      m_last_root_fault_at.clear();
      this->remember_healthy(event);
      active_changed = false;
    }
  }
  if (active_changed && !m_active.events.empty()) {
    ready.push_back(m_active);
  }
  return ready;
}

}  // namespace ai_diagnostics
