#include "ai_diagnostics/ai_diagnostics_node.hpp"

#include "base_core/base_node_runner.hpp"
#include "logging/node_logging.hpp"

#include <memory>

int32_t main(int32_t argc, char ** argv)
{
  base_core::base_node_options_s options;
  options.node_name = "ai_diagnostics_node";
  options.logging = std::make_shared<logging::node_logging_adapter_c>();
  return base_core::create_and_execute_node<
    ai_diagnostics::ai_diagnostics_node_c>(argc, argv, options);
}
