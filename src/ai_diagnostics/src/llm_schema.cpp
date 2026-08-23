#include "ai_diagnostics/llm_schema.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace ai_diagnostics
{
namespace llm
{

void to_json(nlohmann::json & json, const diagnostic_context_s & value)
{
  json = nlohmann::json{
    {"timestamp", value.timestamp},
    {"evidence_id", value.evidence_id},
    {"source_node", value.source_node},
    {"source_file", value.source_file},
    {"source_line", value.source_line},
    {"level", value.level},
    {"fault", value.fault}};
}

void to_json(nlohmann::json & json, const chat_message_s & value)
{
  json = nlohmann::json{
    {"role", value.role},
    {"content", value.content}};
}

void to_json(nlohmann::json & json, const response_format_s & value)
{
  json = nlohmann::json{{"type", value.type}};
}

void to_json(
  nlohmann::json & json,
  const chat_completion_request_s & value)
{
  json = nlohmann::json{
    {"model", value.model},
    {"messages", value.messages},
    {"response_format", value.response_format}};
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

void from_json(
  const nlohmann::json & json,
  diagnostic_analysis_s & value)
{
  json.at("diagnostic_memory").get_to(value.diagnostic_memory);
  json.at("probable_cause").get_to(value.probable_cause);
  json.at("predicted_failure").get_to(value.predicted_failure);
  json.at("recommended_action").get_to(value.recommended_action);
  json.at("evidence_ids").get_to(value.evidence_ids);
  json.at("confidence").get_to(value.confidence);
  json.at("insufficient_evidence").get_to(value.insufficient_evidence);
  json.at("potentially_recoverable").get_to(
    value.potentially_recoverable);
  if (value.confidence < 0.0F || value.confidence > 1.0F ||
    (!value.insufficient_evidence &&
    (value.probable_cause.empty() || value.predicted_failure.empty() ||
    value.recommended_action.empty())))
  {
    throw std::invalid_argument("LLM diagnostic analysis is incomplete");
  }
}

}  // namespace llm
}  // namespace ai_diagnostics
