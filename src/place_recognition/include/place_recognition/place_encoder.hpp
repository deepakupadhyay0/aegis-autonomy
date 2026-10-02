#pragma once

#include "place_recognition/camera_encoder.hpp"
#include "place_recognition/wifi_encoder.hpp"

#include <torch/torch.h>

#include <cstdint>
#include <memory>

namespace place_recognition
{

class place_encoder_c final : public torch::nn::Module
{
public:
  place_encoder_c(
    std::int64_t wifi_feature_count,
    std::int64_t access_point_count,
    std::int64_t hidden_size,
    std::int64_t descriptor_size);

  place_encoder_c(const place_encoder_c &) = delete;
  place_encoder_c & operator=(const place_encoder_c &) = delete;
  place_encoder_c(place_encoder_c &&) = delete;
  place_encoder_c & operator=(place_encoder_c &&) = delete;

  torch::Tensor forward(
    const torch::Tensor & wifi_features,
    const torch::Tensor & wifi_access_point_mask,
    const torch::Tensor & camera_images,
    const torch::Tensor & camera_available);

private:
  std::shared_ptr<wifi_encoder_c> m_wifi_encoder;
  std::shared_ptr<camera_encoder_c> m_camera_encoder;
  torch::nn::Sequential m_fusion_head;
  std::int64_t m_descriptor_size;
};

}  // namespace place_recognition
