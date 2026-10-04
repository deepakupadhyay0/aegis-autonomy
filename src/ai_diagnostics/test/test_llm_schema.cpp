#include "ai_diagnostics/llm_schema.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <stdexcept>

TEST(LlmSchemaTest, SerializesRequestWithoutOutputSchema)
{
  ai_diagnostics::llm::chat_completion_request_s request;
  request.model = "diagnostic-model";
  request.messages = {
    ai_diagnostics::llm::chat_message_s{"user", "Review this incident"}};

  const nlohmann::json json = request;
  EXPECT_EQ(json.at("model"), "diagnostic-model");
  EXPECT_EQ(json.at("messages").at(0).at("content"), "Review this incident");
  EXPECT_FALSE(json.contains("response_format"));
}

TEST(LlmSchemaTest, ReadsPlainTextFromChatCompletion)
{
  const nlohmann::json choice = {
    {"message", {{"content", "Timing and calibration are both plausible."}}}};
  const nlohmann::json json = {
    {"choices", nlohmann::json::array({choice})}};
  const ai_diagnostics::llm::chat_completion_response_s response =
    json.get<ai_diagnostics::llm::chat_completion_response_s>();
  ASSERT_EQ(response.choices.size(), 1U);
  EXPECT_EQ(response.choices.front().message.content,
    "Timing and calibration are both plausible.");
}

TEST(LlmSchemaTest, RejectsMissingOrEmptyAnswer)
{
  const nlohmann::json no_choices = {{"choices", nlohmann::json::array()}};
  EXPECT_THROW(
    no_choices.get<ai_diagnostics::llm::chat_completion_response_s>(),
    std::invalid_argument);

  const nlohmann::json empty_choice = {
    {"message", {{"content", ""}}}};
  const nlohmann::json empty_answer = {
    {"choices", nlohmann::json::array({empty_choice})}};
  EXPECT_THROW(
    empty_answer.get<ai_diagnostics::llm::chat_completion_response_s>(),
    std::invalid_argument);
}
