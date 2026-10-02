#pragma once

#include "place_recognition/dataset.hpp"

#include <torch/torch.h>

#include <cstdint>
#include <filesystem>

namespace place_recognition
{

/// Loads WiFi tensors, reference poses, and an optional synchronized camera manifest.
place_dataset_s load_dataset(
  const std::filesystem::path & channels_path,
  const std::filesystem::path & camera_manifest_path = {});

camera_batch_s load_camera_batch(
  const place_dataset_s & dataset,
  const torch::Tensor & sample_indices,
  std::int64_t image_width,
  std::int64_t image_height);

}  // namespace place_recognition
