#include "utils/topic_timing_config.hpp"
#include "utils/topic_timing_monitor.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace
{
using namespace std::chrono_literals;
namespace base_core = utils;

bool has_issue(
  const utils::topic_timing_result_s & result,
  const utils::topic_timing_issue_e issue)
{
  return (static_cast<common::uint32_t>(result.issues) &
         static_cast<common::uint32_t>(issue)) != 0U;
}

utils::topic_timing_options_s make_options()
{
  utils::topic_timing_options_s options;
  options.topic_name = "/camera/image_raw";
  options.window_sample_count = 4U;
  options.startup_sample_count = 3U;
  options.expected_rate_hz = 10.0;
  options.startup_timeout = 500ms;
  options.warning_timeout = 150ms;
  options.error_timeout = 300ms;
  options.delay_mean_warning_percent = 50.0;
  options.delay_mean_error_percent = 100.0;
  options.delay_jitter_warning_percent = 50.0;
  options.delay_jitter_error_percent = 100.0;
  return options;
}

TEST(TopicTimingMonitorTest, DetectsStartupTimeout)
{
  const base_core::topic_timing_monitor_c::timestamp_t start{0};
  base_core::topic_timing_monitor_c monitor(make_options(), start);
  const base_core::topic_timing_result_s result = monitor.evaluate(start + 501ms);
  EXPECT_EQ(result.severity, base_core::topic_timing_severity_e::error);
  EXPECT_TRUE(has_issue(result, base_core::topic_timing_issue_e::startup_timeout));
}

TEST(TopicTimingMonitorTest, PausedSimulationTimeDoesNotAgeMessages)
{
  base_core::topic_timing_monitor_c monitor(make_options(), 100ms);
  monitor.record_message(100ms, 100ms, 90ms);
  const base_core::topic_timing_result_s result = monitor.evaluate(100ms);
  EXPECT_EQ(result.latest_message_age_ms, 0);
  EXPECT_FALSE(has_issue(result, base_core::topic_timing_issue_e::message_timeout));
}

TEST(TopicTimingMonitorTest, DetectsSimulationTimeRegression)
{
  base_core::topic_timing_monitor_c monitor(make_options(), 100ms);
  monitor.record_message(200ms, 200ms, 190ms);
  const base_core::topic_timing_result_s result = monitor.evaluate(50ms);
  EXPECT_TRUE(has_issue(
      result, base_core::topic_timing_issue_e::watchdog_time_regression));
  EXPECT_EQ(result.latest_message_age_ms, 0);
}

TEST(TopicTimingConfigTest, LoadsConfiguredTopics)
{
  const base_core::topic_timing_configuration_s config =
    base_core::load_topic_timing_configuration();
  ASSERT_FALSE(config.topics.empty());
  EXPECT_FALSE(config.topics.front().topic_name.empty());
  EXPECT_GT(config.topics.front().expected_rate_hz, 0.0);
}

TEST(TopicTimingMonitorTest, ReportsHealthySteadyStream)
{
  const base_core::topic_timing_monitor_c::timestamp_t start{0};
  base_core::topic_timing_monitor_c monitor(make_options(), start);
  monitor.record_message(start + 10ms, 10ms, 0ms);
  monitor.record_message(start + 110ms, 110ms, 100ms);
  monitor.record_message(start + 210ms, 210ms, 200ms);
  const base_core::topic_timing_result_s result = monitor.evaluate(start + 220ms);
  EXPECT_EQ(result.severity, base_core::topic_timing_severity_e::healthy);
  EXPECT_EQ(result.issues, base_core::topic_timing_issue_e::none);
  EXPECT_NEAR(result.measured_rate_hz, 10.0, 0.001);
  EXPECT_NEAR(result.mean_delay_ms, 10.0, 0.001);
}

TEST(TopicTimingMonitorTest, DetectsMessageTimeoutAtBothSeverities)
{
  const base_core::topic_timing_monitor_c::timestamp_t start{0};
  base_core::topic_timing_monitor_c monitor(make_options(), start);
  monitor.record_message(start + 10ms, 10ms, 0ms);
  EXPECT_EQ(monitor.evaluate(start + 160ms).severity,
    base_core::topic_timing_severity_e::warning);
  const base_core::topic_timing_result_s result = monitor.evaluate(start + 310ms);
  EXPECT_EQ(result.severity, base_core::topic_timing_severity_e::error);
  EXPECT_TRUE(has_issue(result, base_core::topic_timing_issue_e::message_timeout));
}

TEST(TopicTimingMonitorTest, DetectsTimestampIntegrityFaults)
{
  const base_core::topic_timing_monitor_c::timestamp_t start{0};
  base_core::topic_timing_monitor_c monitor(make_options(), start);
  monitor.record_message(start + 100ms, 100ms, 90ms);
  monitor.record_message(start + 90ms, 90ms, 90ms);
  monitor.record_message(start + 200ms, 200ms, 210ms);
  const base_core::topic_timing_result_s result = monitor.evaluate(start + 220ms);
  EXPECT_TRUE(has_issue(result, base_core::topic_timing_issue_e::receive_time_regression));
  EXPECT_TRUE(has_issue(result, base_core::topic_timing_issue_e::duplicate_acquisition_time));
  EXPECT_TRUE(has_issue(result, base_core::topic_timing_issue_e::future_acquisition_time));
  EXPECT_TRUE(has_issue(result, base_core::topic_timing_issue_e::negative_transport_delay));
}

TEST(TopicTimingMonitorTest, ReportsMultipleDdsFaultsTogether)
{
  const base_core::topic_timing_monitor_c::timestamp_t start{0};
  base_core::topic_timing_monitor_c monitor(make_options(), start);
  monitor.record_dds_issue(base_core::topic_timing_issue_e::dds_deadline_missed);
  monitor.record_dds_issue(base_core::topic_timing_issue_e::dds_message_lost);
  const base_core::topic_timing_result_s result = monitor.evaluate(start);
  EXPECT_TRUE(has_issue(result, base_core::topic_timing_issue_e::dds_deadline_missed));
  EXPECT_TRUE(has_issue(result, base_core::topic_timing_issue_e::dds_message_lost));
}

TEST(TopicTimingMonitorTest, DetectsLowRateAndHighTransportDelay)
{
  const base_core::topic_timing_monitor_c::timestamp_t start{0};
  base_core::topic_timing_monitor_c monitor(make_options(), start);
  monitor.record_message(start + 80ms, 80ms, 0ms);
  monitor.record_message(start + 280ms, 280ms, 200ms);
  monitor.record_message(start + 480ms, 480ms, 400ms);
  const base_core::topic_timing_result_s result = monitor.evaluate(start + 490ms);
  EXPECT_EQ(result.severity, base_core::topic_timing_severity_e::error);
  EXPECT_TRUE(has_issue(result, base_core::topic_timing_issue_e::rate_low));
  EXPECT_TRUE(has_issue(result, base_core::topic_timing_issue_e::delay_mean_high));
}

TEST(TopicTimingAlertQueueTest, PreservesHighestPendingSeverity)
{
  utils::topic_timing_alert_queue_c queue;
  utils::topic_timing_alert_s warning;
  warning.result.severity = utils::topic_timing_severity_e::warning;
  warning.sequence = 1U;
  utils::topic_timing_alert_s error;
  error.result.severity = utils::topic_timing_severity_e::error;
  error.sequence = 2U;
  utils::topic_timing_alert_s newer_warning = warning;
  newer_warning.sequence = 3U;

  EXPECT_TRUE(queue.try_push(warning));
  EXPECT_TRUE(queue.try_push(error));
  EXPECT_FALSE(queue.try_push(newer_warning));

  utils::topic_timing_alert_s result;
  ASSERT_TRUE(queue.try_pop(result));
  EXPECT_EQ(result.result.severity, utils::topic_timing_severity_e::error);
  EXPECT_EQ(result.sequence, 2U);
}

TEST(TopicTimingAlertQueueTest, FatalCannotBeOverwrittenByWarningOrError)
{
  utils::topic_timing_alert_queue_c queue;
  utils::topic_timing_alert_s fatal;
  fatal.result.severity = utils::topic_timing_severity_e::fatal;
  fatal.sequence = 10U;
  ASSERT_TRUE(queue.try_push(fatal));

  utils::topic_timing_alert_s warning;
  warning.result.severity = utils::topic_timing_severity_e::warning;
  EXPECT_FALSE(queue.try_push(warning));
  utils::topic_timing_alert_s error;
  error.result.severity = utils::topic_timing_severity_e::error;
  EXPECT_FALSE(queue.try_push(error));

  utils::topic_timing_alert_s result;
  ASSERT_TRUE(queue.try_pop(result));
  EXPECT_EQ(result.result.severity, utils::topic_timing_severity_e::fatal);
  EXPECT_EQ(result.sequence, 10U);
  EXPECT_FALSE(queue.try_pop(result));
}

TEST(TopicTimingAlertQueueTest, EqualSeverityKeepsNewestEvidence)
{
  utils::topic_timing_alert_queue_c queue;
  utils::topic_timing_alert_s first;
  first.result.severity = utils::topic_timing_severity_e::error;
  first.sequence = 1U;
  utils::topic_timing_alert_s second = first;
  second.sequence = 2U;
  ASSERT_TRUE(queue.try_push(first));
  ASSERT_TRUE(queue.try_push(second));

  utils::topic_timing_alert_s result;
  ASSERT_TRUE(queue.try_pop(result));
  EXPECT_EQ(result.sequence, 2U);
}

TEST(TopicTimingAlertQueueTest, SupportsLargerBoundedCapacity)
{
  utils::topic_timing_alert_queue_c queue(2U);
  utils::topic_timing_alert_s first;
  first.result.severity = utils::topic_timing_severity_e::warning;
  first.sequence = 1U;
  utils::topic_timing_alert_s second = first;
  second.sequence = 2U;
  ASSERT_TRUE(queue.try_push(first));
  ASSERT_TRUE(queue.try_push(second));

  utils::topic_timing_alert_s error;
  error.result.severity = utils::topic_timing_severity_e::error;
  error.sequence = 3U;
  ASSERT_TRUE(queue.try_push(error));

  utils::topic_timing_alert_s result;
  ASSERT_TRUE(queue.try_pop(result));
  EXPECT_EQ(result.sequence, 2U);
  ASSERT_TRUE(queue.try_pop(result));
  EXPECT_EQ(result.sequence, 3U);
}

}  // namespace
