#include "place_recognition/place_encoder.hpp"

#include <torch/torch.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace
{

bool unit_normalized(const torch::Tensor & descriptor)
{
  const torch::Tensor norm = descriptor.square().sum(1).sqrt();
  return torch::allclose(norm, torch::ones_like(norm), 1.0e-5, 1.0e-5);
}

}  // namespace

int main()
{
  constexpr std::int64_t BATCH_SIZE = 4;
  constexpr std::int64_t ACCESS_POINT_COUNT = 3;
  constexpr std::int64_t WIFI_FEATURE_COUNT = 5;
  constexpr std::int64_t DESCRIPTOR_SIZE = 16;

  place_recognition::place_encoder_c model(
    WIFI_FEATURE_COUNT, ACCESS_POINT_COUNT, 32, DESCRIPTOR_SIZE);
  const torch::Tensor wifi_features = torch::randn(
    {BATCH_SIZE, ACCESS_POINT_COUNT, WIFI_FEATURE_COUNT});
  const torch::Tensor wifi_mask = torch::ones(
    {BATCH_SIZE, ACCESS_POINT_COUNT},
    torch::TensorOptions().dtype(torch::kBool));
  const torch::Tensor images = torch::randn({BATCH_SIZE, 3, 64, 96});
  const torch::Tensor camera_available = torch::ones(
    {BATCH_SIZE}, torch::TensorOptions().dtype(torch::kBool));

  const torch::Tensor combined = model.forward(
    wifi_features, wifi_mask, images, camera_available);
  if (combined.size(0) != BATCH_SIZE ||
    combined.size(1) != DESCRIPTOR_SIZE || !unit_normalized(combined))
  {
    std::cerr << "Unexpected combined descriptor\n";
    return 1;
  }

  const torch::Tensor no_camera = torch::zeros_like(camera_available);
  const torch::Tensor wifi_only = model.forward(
    wifi_features, wifi_mask, images, no_camera);
  const torch::Tensor changed_images = images + 1000.0;
  const torch::Tensor wifi_only_after_image_change = model.forward(
    wifi_features, wifi_mask, changed_images, no_camera);
  if (!torch::allclose(
      wifi_only, wifi_only_after_image_change, 1.0e-6, 1.0e-6))
  {
    std::cerr << "Disabled camera data changed the descriptor\n";
    return 1;
  }

  const torch::Tensor no_wifi = torch::zeros_like(wifi_mask);
  const torch::Tensor camera_only = model.forward(
    wifi_features, no_wifi, images, camera_available);
  const torch::Tensor camera_only_after_wifi_change = model.forward(
    wifi_features + 1000.0, no_wifi, images, camera_available);
  if (!torch::allclose(
      camera_only, camera_only_after_wifi_change, 1.0e-6, 1.0e-6))
  {
    std::cerr << "Disabled WiFi data changed the descriptor\n";
    return 1;
  }

  const bool require_cuda =
    std::getenv("PLACE_RECOGNITION_REQUIRE_CUDA") != nullptr;
  if (!torch::cuda::is_available()) {
    if (require_cuda) {
      std::cerr << "CUDA is required but unavailable to LibTorch\n";
      return 1;
    }
    std::cout << "CUDA unavailable; skipped GPU encoder smoke test\n";
    return 0;
  }

  const torch::Device cuda_device{torch::kCUDA};
  model.to(cuda_device);
  const torch::Tensor cuda_descriptor = model.forward(
    wifi_features.to(cuda_device),
    wifi_mask.to(cuda_device),
    images.to(cuda_device),
    camera_available.to(cuda_device));
  if (!unit_normalized(cuda_descriptor)) {
    std::cerr << "Unexpected CUDA descriptor\n";
    return 1;
  }
  std::cout << "CUDA encoder smoke test passed\n";
  return 0;
}
