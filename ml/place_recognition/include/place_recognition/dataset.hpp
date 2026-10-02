#pragma once

#include <torch/torch.h>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace place_recognition
{

struct place_dataset_s
{
  /// [sample, access point, feature].
  torch::Tensor wifi_features;
  /// [sample, access point]. False entries are ignored by the WiFi encoder.
  torch::Tensor wifi_access_point_mask;
  /// Empty paths represent samples without a synchronized camera frame.
  std::vector<std::filesystem::path> camera_frame_paths;
  /// [sample, 3]. Reference x, y and heading used only for training and evaluation.
  torch::Tensor reference_pose;
};

struct camera_batch_s
{
  /// Normalized RGB images arranged as [batch, channel, height, width].
  torch::Tensor images;
  /// [batch]. False entries have no synchronized image and contain zeros.
  torch::Tensor available;
};

void normalize_wifi_features(
  place_dataset_s & dataset,
  std::int64_t fit_sample_count);

}  // namespace place_recognition
