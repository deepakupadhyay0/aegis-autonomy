#include "logging/log_client.hpp"
#include "logging/log_transport.hpp"

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

struct fake_transport_state_s
{
  std::mutex mutex;
  std::condition_variable record_available;
  common::logging::log_registration_s registration;
  std::optional<common::logging::log_record_s> record;
};

class fake_log_transport_c final : public logging::abstract_log_transport_c
{
public:
  explicit fake_log_transport_c(
    std::shared_ptr<fake_transport_state_s> state) noexcept
  : m_state(std::move(state))
  {
  }

  ~fake_log_transport_c() noexcept override = default;

  fake_log_transport_c(const fake_log_transport_c &) = delete;
  fake_log_transport_c & operator=(const fake_log_transport_c &) = delete;
  fake_log_transport_c(fake_log_transport_c &&) = delete;
  fake_log_transport_c & operator=(fake_log_transport_c &&) = delete;

  bool connect(
    const common::logging::log_registration_s & registration) noexcept override
  {
    const std::lock_guard<std::mutex> lock(m_state->mutex);
    m_state->registration = registration;
    return true;
  }

  bool send(
    const common::logging::log_record_s & record) noexcept override
  {
    {
      const std::lock_guard<std::mutex> lock(m_state->mutex);
      m_state->record = record;
    }
    m_state->record_available.notify_one();
    return true;
  }

  void disconnect() noexcept override
  {
  }

private:
  std::shared_ptr<fake_transport_state_s> m_state;
};

}  // namespace

TEST(LogClientTest, SendsRecordThroughInjectedTransport)
{
  const std::shared_ptr<fake_transport_state_s> state =
    std::make_shared<fake_transport_state_s>();
  logging::logging_options_s options;
  options.queue_capacity = 4U;
  logging::log_client_c client(
    "test_node",
    options,
    std::make_unique<fake_log_transport_c>(state));

  client.enqueue(
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
  EXPECT_EQ(state->registration.node_name, "test_node");
  EXPECT_EQ(state->record->level, logging::log_level_e::info);
  EXPECT_EQ(state->record->message, "test message");
  lock.unlock();

  client.shutdown();
}
