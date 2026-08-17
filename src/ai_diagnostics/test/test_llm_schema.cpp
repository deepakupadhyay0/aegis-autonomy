#include "ai_diagnostics/llm_schema.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>

TEST(LlmSchemaTest, SerializesTypedRequest)
{
  const ai_diagnostics::llm::diagnostic_context_s context{
    "camera_node",
    "camera_node.cpp",
    42U,
    2U,
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
  const nlohmann::json response_json = nlohmann::json::parse(R"json(
    {
      "choices": [
        {
          "message": {
            "content": "{\"probable_cause\":\"stale camera source\",\"recommended_action\":\"inspect the capture path\",\"potentially_recoverable\":true}"
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
  EXPECT_EQ(analysis.probable_cause, "stale camera source");
  EXPECT_EQ(analysis.recommended_action, "inspect the capture path");
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
