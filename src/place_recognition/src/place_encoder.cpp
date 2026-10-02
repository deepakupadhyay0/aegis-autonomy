#include "place_recognition/place_encoder.hpp"

#include <stdexcept>

namespace place_recognition
{

place_encoder_c::place_encoder_c(
  const std::int64_t wifi_feature_count,
  const std::int64_t access_point_count,
  const std::int64_t hidden_size,
  const std::int64_t descriptor_size)
: m_wifi_encoder(register_module(
      "wifi_encoder",
      std::make_shared<wifi_encoder_c>(
        wifi_feature_count,
        access_point_count,
        hidden_size,
        descriptor_size))),
  m_camera_encoder(register_module(
      "camera_encoder",
      std::make_shared<camera_encoder_c>(descriptor_size))),
  m_fusion_head(register_module(
      "fusion_head",
      torch::nn::Sequential(
        torch::nn::Linear((descriptor_size * 2) + 2, hidden_size),
        torch::nn::ReLU(),
        torch::nn::Linear(hidden_size, descriptor_size)))),
  m_descriptor_size(descriptor_size)
{
  if (hidden_size <= 0 || descriptor_size <= 0) {
    throw std::invalid_argument("Place encoder dimensions must be positive");
  }
}

torch::Tensor place_encoder_c::forward(
  const torch::Tensor & wifi_features,
  const torch::Tensor & wifi_access_point_mask,
  const torch::Tensor & camera_images,
  const torch::Tensor & camera_available)
{
  const std::int64_t batch_size = wifi_features.size(0);
  if (camera_images.dim() != 4 || camera_images.size(0) != batch_size ||
    camera_available.dim() != 1 || camera_available.size(0) != batch_size)
  {
    throw std::invalid_argument("Place encoder inputs have incompatible batches");
  }

  const torch::Tensor wifi_available = wifi_access_point_mask
    .to(torch::kBool)
    .any(1, true);
  const torch::Tensor camera_mask = camera_available
    .to(torch::kBool)
    .unsqueeze(1);
  if (!(wifi_available.logical_or(camera_mask)).all().item<bool>()) {
    throw std::invalid_argument(
            "Each place sample must contain WiFi or a camera frame");
  }

  torch::Tensor wifi_descriptor = m_wifi_encoder->forward(
    wifi_features, wifi_access_point_mask);
  torch::Tensor camera_descriptor;
  if (camera_mask.any().item<bool>()) {
    camera_descriptor = m_camera_encoder->forward(camera_images);
  } else {
    camera_descriptor = torch::zeros(
      {batch_size, m_descriptor_size}, wifi_descriptor.options());
  }

  const torch::Tensor wifi_weight = wifi_available.to(wifi_descriptor.dtype());
  const torch::Tensor camera_weight = camera_mask.to(camera_descriptor.dtype());
  wifi_descriptor = wifi_descriptor * wifi_weight;
  camera_descriptor = camera_descriptor * camera_weight;
  torch::Tensor descriptor = m_fusion_head->forward(torch::cat(
      {
        wifi_descriptor,
        camera_descriptor,
        wifi_weight,
        camera_weight
      },
      1));
  return descriptor / descriptor.square()
         .sum(1, true)
         .sqrt()
         .clamp_min(1.0e-12);
}

}  // namespace place_recognition
