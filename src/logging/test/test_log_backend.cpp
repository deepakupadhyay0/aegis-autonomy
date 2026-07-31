#include "logging/log_backend.hpp"
#include "logging/node_log_sink.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <source_location>
#include <utility>

namespace
{

struct fake_sink_state_s
{
  std::mutex mutex;
  std::condition_variable record_available;
  std::optional<logging::log_record_s> record;
};

class fake_log_sink_c final : public logging::abstract_log_sink_c
{
public:
  explicit fake_log_sink_c(
    std::shared_ptr<fake_sink_state_s> state) noexcept
  : m_state(std::move(state))
  {
  }

  ~fake_log_sink_c() noexcept override = default;

  fake_log_sink_c(const fake_log_sink_c &) = delete;
  fake_log_sink_c & operator=(const fake_log_sink_c &) = delete;
  fake_log_sink_c(fake_log_sink_c &&) = delete;
  fake_log_sink_c & operator=(fake_log_sink_c &&) = delete;

  bool write(const logging::log_record_s & record) noexcept override
  {
    {
      const std::lock_guard<std::mutex> lock(m_state->mutex);
      m_state->record = record;
    }
    m_state->record_available.notify_one();
    return true;
  }

  bool flush() noexcept override
  {
    return true;
  }

private:
  std::shared_ptr<fake_sink_state_s> m_state;
};

}  // namespace

TEST(LogBackendTest, WritesRecordThroughInjectedSink)
{
  const std::shared_ptr<fake_sink_state_s> state =
    std::make_shared<fake_sink_state_s>();
  logging::logging_options_s options;
  options.queue_capacity = 4U;
  options.enable_queue_diagnostics = false;
  logging::local_log_backend_c backend(
    "test_node",
    options,
    std::make_unique<fake_log_sink_c>(state));

  backend.enqueue(
    logging::log_level_e::info,
    "test message",
    std::source_location::current());

  std::unique_lock<std::mutex> lock(state->mutex);
  ASSERT_TRUE(state->record_available.wait_for(
      lock,
      std::chrono::seconds(1),
      [state]() {
        return state->record.has_value();
      }));
  EXPECT_EQ(state->record->level, logging::log_level_e::info);
  EXPECT_EQ(state->record->message, "test message");
  lock.unlock();

  backend.shutdown();
}
