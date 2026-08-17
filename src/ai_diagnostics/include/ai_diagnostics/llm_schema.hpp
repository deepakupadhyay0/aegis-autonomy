#pragma once

#include "ai_diagnostics/visibility_control.hpp"
#include "common/numeric_types.hpp"

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace ai_diagnostics
{
namespace llm
{

struct diagnostic_context_s
{
  std::string source_node;
  std::string source_file;
  common::uint32_t source_line{0U};
  common::uint8_t level{0U};
  std::string fault;
};

struct chat_message_s
{
  std::string role;
  std::string content;
};

struct response_format_s
{
  std::string type;
};

struct chat_completion_request_s
{
  std::string model;
  std::vector<chat_message_s> messages;
  response_format_s response_format;
};

struct chat_completion_message_s
{
  std::string content;
};

struct chat_completion_choice_s
{
  chat_completion_message_s message;
};

struct chat_completion_response_s
{
  std::vector<chat_completion_choice_s> choices;
};

struct diagnostic_analysis_s
{
  std::string probable_cause;
  std::string recommended_action;
  bool potentially_recoverable{false};
};

AI_DIAGNOSTICS_PUBLIC void to_json(
  nlohmann::json & json,
  const diagnostic_context_s & value);
AI_DIAGNOSTICS_PUBLIC void to_json(
  nlohmann::json & json,
  const chat_message_s & value);
AI_DIAGNOSTICS_PUBLIC void to_json(
  nlohmann::json & json,
  const response_format_s & value);
AI_DIAGNOSTICS_PUBLIC void to_json(
  nlohmann::json & json,
  const chat_completion_request_s & value);

AI_DIAGNOSTICS_PUBLIC void from_json(
  const nlohmann::json & json,
  chat_completion_message_s & value);
AI_DIAGNOSTICS_PUBLIC void from_json(
  const nlohmann::json & json,
  chat_completion_choice_s & value);
AI_DIAGNOSTICS_PUBLIC void from_json(
  const nlohmann::json & json,
  chat_completion_response_s & value);
AI_DIAGNOSTICS_PUBLIC void from_json(
  const nlohmann::json & json,
  diagnostic_analysis_s & value);

}  // namespace llm
}  // namespace ai_diagnostics
