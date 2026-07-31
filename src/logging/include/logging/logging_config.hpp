#pragma once

#include "logging/log_types.hpp"
#include "logging/visibility_control.hpp"

namespace logging
{

/// Loads and validates the immutable process logging configuration.
LOGGING_PUBLIC logging_options_s load_logging_options();

}  // namespace logging
