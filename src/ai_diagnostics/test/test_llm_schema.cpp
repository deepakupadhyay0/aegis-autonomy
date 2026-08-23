#include "ai_diagnostics/llm_schema.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>

TEST(LlmSchemaTest, SerializesTypedRequest)
{
  const ai_diagnostics::llm::diagnostic_context_s context{
    "2026-08-21T12:00:00.000000000Z",
    "camera_node:42",
    "camera_node",
    "camera_node.cpp",
    42U,
    "error",
    "Camera frame is stale"};
  const nlohmann::json context_json = context;

  ai_diagnostics::llm::chat_completion_request_s request;
  request.model = "diagnostic-model";
  request.messages = {
    ai_diagnostics::llm::chat_message_s{"user", context_json.dump()}};
  request.response_format.type = "json_object";

  const nlohmann::json request_json = request;
  EXPECT_EQ(request_json.at("model"), "diagnostic-model");
  EXPECT_EQ(request_json.at("messages").size(), 1U);
  EXPECT_EQ(
    request_json.at("response_format").at("type"),
    "json_object");
}

TEST(LlmSchemaTest, DeserializesTypedResponseAndAnalysis)
{
  const nlohmann::json response_json =
    nlohmann::json::parse(
    R"json(
    {
      "choices": [
        {
          "message": {
            "content": "{\"diagnostic_memory\":\"camera freshness is degrading\",\"probable_cause\":\"stale camera source\",\"predicted_failure\":\"camera data loss\",\"recommended_action\":\"inspect the capture path\",\"evidence_ids\":[\"camera_node:42\"],\"confidence\":0.8,\"insufficient_evidence\":false,\"potentially_recoverable\":true}"
          }
        }
      ]
    }
  )json");
  const ai_diagnostics::llm::chat_completion_response_s response =
    response_json.get<
    ai_diagnostics::llm::chat_completion_response_s>();
  ASSERT_EQ(response.choices.size(), 1U);

  const nlohmann::json analysis_json = nlohmann::json::parse(
    response.choices.front().message.content);
  const ai_diagnostics::llm::diagnostic_analysis_s analysis =
    analysis_json.get<ai_diagnostics::llm::diagnostic_analysis_s>();
  EXPECT_EQ(analysis.diagnostic_memory, "camera freshness is degrading");
  EXPECT_EQ(analysis.probable_cause, "stale camera source");
  EXPECT_EQ(analysis.predicted_failure, "camera data loss");
  EXPECT_EQ(analysis.recommended_action, "inspect the capture path");
  EXPECT_EQ(analysis.evidence_ids.size(), 1U);
  EXPECT_FLOAT_EQ(analysis.confidence, 0.8F);
  EXPECT_FALSE(analysis.insufficient_evidence);
  EXPECT_TRUE(analysis.potentially_recoverable);
}

TEST(LlmSchemaTest, RejectsResponseWithoutChoices)
{
  const nlohmann::json response_json = nlohmann::json::parse(
    R"json({"choices": []})json");
  EXPECT_THROW(
    response_json.get<ai_diagnostics::llm::chat_completion_response_s>(),
    std::invalid_argument);
}
