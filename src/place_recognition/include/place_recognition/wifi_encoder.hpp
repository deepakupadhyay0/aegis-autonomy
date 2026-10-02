#pragma once

#include <torch/torch.h>

#include <cstdint>

namespace place_recognition
{

class wifi_encoder_c final : public torch::nn::Module
{
public:
  wifi_encoder_c(
    std::int64_t feature_count,
    std::int64_t access_point_count,
    std::int64_t hidden_size,
    std::int64_t descriptor_size);

  wifi_encoder_c(const wifi_encoder_c &) = delete;
  wifi_encoder_c & operator=(const wifi_encoder_c &) = delete;
  wifi_encoder_c(wifi_encoder_c &&) = delete;
  wifi_encoder_c & operator=(wifi_encoder_c &&) = delete;

  torch::Tensor forward(
    const torch::Tensor & features,
    const torch::Tensor & access_point_mask);

private:
  torch::nn::Linear m_feature_projection{nullptr};
  torch::nn::Embedding m_access_point_embedding{nullptr};
  torch::nn::Sequential m_token_encoder;
  torch::nn::Sequential m_descriptor_head;
};

}  // namespace place_recognition

