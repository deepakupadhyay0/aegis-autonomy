#pragma once

#include <torch/torch.h>

#include <cstdint>

namespace place_recognition
{

class camera_encoder_c final : public torch::nn::Module
{
public:
  explicit camera_encoder_c(std::int64_t descriptor_size);

  camera_encoder_c(const camera_encoder_c &) = delete;
  camera_encoder_c & operator=(const camera_encoder_c &) = delete;
  camera_encoder_c(camera_encoder_c &&) = delete;
  camera_encoder_c & operator=(camera_encoder_c &&) = delete;

  torch::Tensor forward(const torch::Tensor & images);

private:
  torch::nn::Sequential m_encoder;
};

}  // namespace place_recognition
