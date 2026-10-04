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

struct diagnostic_measurement_s
{
  std::string name;
  std::string value;
};

struct diagnostic_context_s
{
  std::string timestamp;
  std::string evidence_id;
  std::string source_node;
  std::string source_file;
  common::uint32_t source_line{0U};
  std::string level;
  std::string fault;
  std::vector<diagnostic_measurement_s> measurements;
  bool healthy{false};
  bool truncated{false};
};

struct chat_message_s
{
  std::string role;
  std::string content;
};

struct chat_completion_request_s
{
  std::string model;
  std::vector<chat_message_s> messages;
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

AI_DIAGNOSTICS_PUBLIC void to_json(
  nlohmann::json & json,
  const diagnostic_measurement_s & value);
AI_DIAGNOSTICS_PUBLIC void to_json(
  nlohmann::json & json,
  const diagnostic_context_s & value);
AI_DIAGNOSTICS_PUBLIC void to_json(
  nlohmann::json & json,
  const chat_message_s & value);
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

}  // namespace llm
}  // namespace ai_diagnostics
