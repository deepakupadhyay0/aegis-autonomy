#include "place_recognition/camera_encoder.hpp"

#include <stdexcept>

namespace place_recognition
{

camera_encoder_c::camera_encoder_c(const std::int64_t descriptor_size)
: m_encoder(register_module(
      "encoder",
      torch::nn::Sequential(
        torch::nn::Conv2d(
          torch::nn::Conv2dOptions(3, 32, 5).stride(2).padding(2)),
        torch::nn::ReLU(),
        torch::nn::Conv2d(
          torch::nn::Conv2dOptions(32, 64, 3).stride(2).padding(1)),
        torch::nn::ReLU(),
        torch::nn::Conv2d(
          torch::nn::Conv2dOptions(64, 128, 3).stride(2).padding(1)),
        torch::nn::ReLU(),
        torch::nn::AdaptiveAvgPool2d(
          torch::nn::AdaptiveAvgPool2dOptions({1, 1})),
        torch::nn::Flatten(),
        torch::nn::Linear(128, descriptor_size))))
{
  if (descriptor_size <= 0) {
    throw std::invalid_argument("Camera descriptor size must be positive");
  }
}

torch::Tensor camera_encoder_c::forward(const torch::Tensor & images)
{
  if (images.dim() != 4 || images.size(1) != 3) {
    throw std::invalid_argument(
            "Camera encoder expects [batch, 3, height, width]");
  }
  torch::Tensor descriptor = m_encoder->forward(images);
  return descriptor / descriptor.square()
         .sum(1, true)
         .sqrt()
         .clamp_min(1.0e-12);
}

}  // namespace place_recognition
