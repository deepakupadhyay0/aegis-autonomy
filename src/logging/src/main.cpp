#include "logging/logging_service.hpp"

#include "common/process/exit_detection.hpp"

#include <rclcpp/rclcpp.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <sched.h>
#include <sys/resource.h>

namespace
{

bool diagnostic_dds_requested(
  const int32_t argc,
  char const * const * const argv) noexcept
{
  for (int32_t index = 1; index < argc; ++index) {
    if (argv[index] != nullptr &&
      std::strcmp(argv[index], "--enable-diagnostic-dds") == 0)
    {
      return true;
    }
  }
  return false;
}

void configure_service_scheduling() noexcept
{
  sched_param parameters{};
  parameters.sched_priority = 0;
  static_cast<void>(::sched_setscheduler(0, SCHED_OTHER, &parameters));
  static_cast<void>(::setpriority(PRIO_PROCESS, 0, 10));
}

}  // namespace

int32_t main(const int32_t argc, char ** argv)
{
  configure_service_scheduling();
  const bool enable_diagnostic_dds = diagnostic_dds_requested(argc, argv);
  if (enable_diagnostic_dds) {
    rclcpp::init(argc, argv);
  }
  int32_t return_code = 0;
  try {
    const int32_t exit_notification_fd =
      common::process::register_exit_handler();
    logging::logging_service_options_s options;
    options.enable_diagnostic_dds = enable_diagnostic_dds;
    logging::logging_service_c service(options);
    service.execute(exit_notification_fd);
  } catch (const std::exception & exception) {
    std::fprintf(
      stderr,
      "logging_service failed: %s\n",
      exception.what());
    return_code = -1;
  } catch (...) {
    std::fprintf(stderr, "logging_service failed with an unknown error\n");
    return_code = -1;
  }

  if (enable_diagnostic_dds && rclcpp::ok()) {
    rclcpp::shutdown();
  }
  return return_code;
}
