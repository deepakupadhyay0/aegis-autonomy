#include "ai_diagnostics/incident_window.hpp"
#include "logging/log_types.hpp"

#include <gtest/gtest.h>

#include <span>
#include <vector>

namespace
{

ai_diagnostics::diagnostic_event_s make_event(
  const char * const id,
  const char * const source,
  const logging::log_level_e level,
  const bool healthy)
{
  ai_diagnostics::diagnostic_event_s event;
  event.timestamp = "2026-10-04T12:00:00.000000000Z";
  event.evidence_id = id;
  event.source_node = source;
  event.level = static_cast<common::uint8_t>(level);
  event.fault = healthy ? "health=ok pose_innovation_m=0.02" :
    "pose_innovation_m=0.82";
  event.healthy = healthy;
  return event;
}

}  // namespace

TEST(IncidentWindowTest, KeepsFiveHealthyOutputsAndClosesOnSameSourceRecovery)
{
  using logging::log_level_e;
  ai_diagnostics::incident_window_c builder(16U, 8192U);
  const std::vector<ai_diagnostics::diagnostic_event_s> first = {
    make_event("ok:1", "ndt", log_level_e::info, true),
    make_event("ok:2", "ndt", log_level_e::info, true),
    make_event("ok:3", "ndt", log_level_e::info, true),
    make_event("ok:4", "ndt", log_level_e::info, true),
    make_event("ok:5", "ndt", log_level_e::info, true),
    make_event("ok:6", "ndt", log_level_e::info, true),
    make_event("fault:1", "ndt", log_level_e::warning, false)};
  const std::vector<ai_diagnostics::diagnostic_batch_s> active =
    builder.ingest(std::span<const ai_diagnostics::diagnostic_event_s>(first));
  ASSERT_EQ(active.size(), 1U);
  EXPECT_EQ(active.front().baseline.size(), 5U);
  EXPECT_EQ(active.front().baseline.front().evidence_id, "ok:2");
  EXPECT_EQ(active.front().incident_id, "fault:1");
  EXPECT_FALSE(active.front().recovery_observed);

  const std::vector<ai_diagnostics::diagnostic_event_s> unrelated = {
    make_event("other:1", "camera", log_level_e::info, true)};
  EXPECT_EQ(builder.ingest(unrelated).size(), 1U);

  const std::vector<ai_diagnostics::diagnostic_event_s> recovery = {
    make_event("ok:7", "ndt", log_level_e::info, true)};
  const std::vector<ai_diagnostics::diagnostic_batch_s> recovered =
    builder.ingest(recovery);
  ASSERT_EQ(recovered.size(), 1U);
  EXPECT_TRUE(recovered.front().recovery_observed);
  EXPECT_EQ(recovered.front().events.back().evidence_id, "ok:7");
}

TEST(IncidentWindowTest, BoundsTheWindowAndMarksLostEvidence)
{
  using logging::log_level_e;
  ai_diagnostics::incident_window_c builder(3U, 1024U);
  const std::vector<ai_diagnostics::diagnostic_event_s> input = {
    make_event("ok:1", "ndt", log_level_e::info, true),
    make_event("fault:1", "ndt", log_level_e::warning, false),
    make_event("fault:2", "ndt", log_level_e::warning, false),
    make_event("fault:3", "ndt", log_level_e::warning, false),
    make_event("fault:4", "ndt", log_level_e::warning, false)};
  const std::vector<ai_diagnostics::diagnostic_batch_s> windows =
    builder.ingest(input);
  ASSERT_EQ(windows.size(), 1U);
  EXPECT_TRUE(windows.front().truncated);
  EXPECT_LE(
    windows.front().events.size() + windows.front().baseline.size(),
    3U);
  EXPECT_EQ(windows.front().events.front().evidence_id, "fault:1");
  EXPECT_EQ(windows.front().events.back().evidence_id, "fault:4");
}

TEST(IncidentWindowTest, DoesNotCloseOnAnOlderHealthyStatus)
{
  using logging::log_level_e;
  ai_diagnostics::incident_window_c builder(8U, 4096U);
  ai_diagnostics::diagnostic_event_s fault =
    make_event("fault:1", "ndt", log_level_e::warning, false);
  fault.timestamp = "2026-10-04T12:00:02.000000000Z";
  const std::vector<ai_diagnostics::diagnostic_event_s> start = {fault};
  ASSERT_EQ(builder.ingest(start).size(), 1U);

  ai_diagnostics::diagnostic_event_s stale =
    make_event("ok:old", "ndt", log_level_e::info, true);
  stale.timestamp = "2026-10-04T12:00:01.000000000Z";
  const std::vector<ai_diagnostics::diagnostic_event_s> late = {stale};
  EXPECT_TRUE(builder.ingest(late).empty());

  ai_diagnostics::diagnostic_event_s current =
    make_event("ok:new", "ndt", log_level_e::info, true);
  current.timestamp = "2026-10-04T12:00:03.000000000Z";
  const std::vector<ai_diagnostics::diagnostic_event_s> recovery = {current};
  const std::vector<ai_diagnostics::diagnostic_batch_s> windows =
    builder.ingest(recovery);
  ASSERT_EQ(windows.size(), 1U);
  EXPECT_TRUE(windows.front().recovery_observed);
  EXPECT_EQ(windows.front().events.back().evidence_id, "ok:new");
}
