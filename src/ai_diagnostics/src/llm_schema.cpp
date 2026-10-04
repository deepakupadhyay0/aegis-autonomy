#include "ai_diagnostics/llm_schema.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace ai_diagnostics
{
namespace llm
{

void to_json(nlohmann::json & json, const diagnostic_measurement_s & value)
{
  json = nlohmann::json{{"name", value.name}, {"value", value.value}};
}

void to_json(nlohmann::json & json, const diagnostic_context_s & value)
{
  json = nlohmann::json{
    {"timestamp", value.timestamp},
    {"evidence_id", value.evidence_id},
    {"source_node", value.source_node},
    {"source_file", value.source_file},
    {"source_line", value.source_line},
    {"level", value.level},
    {"fault", value.fault},
    {"measurements", value.measurements},
    {"healthy", value.healthy},
    {"truncated", value.truncated}};
}

void to_json(nlohmann::json & json, const chat_message_s & value)
{
  json = nlohmann::json{
    {"role", value.role},
    {"content", value.content}};
}

void to_json(
  nlohmann::json & json,
  const chat_completion_request_s & value)
{
  json = nlohmann::json{
    {"model", value.model},
    {"messages", value.messages}};
}

void from_json(
  const nlohmann::json & json,
  chat_completion_message_s & value)
{
  json.at("content").get_to(value.content);
  if (value.content.empty()) {
    throw std::invalid_argument("LLM response message content is empty");
  }
}

void from_json(
  const nlohmann::json & json,
  chat_completion_choice_s & value)
{
  json.at("message").get_to(value.message);
}

void from_json(
  const nlohmann::json & json,
  chat_completion_response_s & value)
{
  json.at("choices").get_to(value.choices);
  if (value.choices.empty()) {
    throw std::invalid_argument("LLM response does not contain a choice");
  }
}

}  // namespace llm
}  // namespace ai_diagnostics
