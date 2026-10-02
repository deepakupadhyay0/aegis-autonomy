#include "place_recognition/dataset.hpp"

#include <array>
#include <stdexcept>

namespace place_recognition
{

void normalize_wifi_features(
  place_dataset_s & dataset,
  const std::int64_t fit_sample_count)
{
  if (fit_sample_count <= 0 || fit_sample_count > dataset.wifi_features.size(0)) {
    throw std::invalid_argument("Invalid feature-normalization sample count");
  }

  const torch::Tensor fit_features = dataset.wifi_features.narrow(
    0, 0, fit_sample_count);
  const torch::Tensor fit_mask = dataset.wifi_access_point_mask
    .narrow(0, 0, fit_sample_count)
    .unsqueeze(-1)
    .expand_as(fit_features)
    .to(torch::kFloat32);
  constexpr std::array<std::int64_t, 2U> REDUCTION_DIMENSIONS{0, 1};
  const c10::IntArrayRef reduction_dimensions{REDUCTION_DIMENSIONS};
  const torch::Tensor valid_count = fit_mask.sum(reduction_dimensions)
    .clamp_min(1.0);
  const torch::Tensor mean = (fit_features * fit_mask)
    .sum(reduction_dimensions) / valid_count;
  const torch::Tensor centered = fit_features - mean;
  const torch::Tensor variance =
    (centered.square() * fit_mask).sum(reduction_dimensions) / valid_count;
  const torch::Tensor deviation = variance.sqrt().clamp_min(1.0e-6);
  const torch::Tensor complete_mask = dataset.wifi_access_point_mask
    .unsqueeze(-1)
    .expand_as(dataset.wifi_features);
  dataset.wifi_features = torch::where(
    complete_mask,
    (dataset.wifi_features - mean) / deviation,
    torch::zeros_like(dataset.wifi_features));
}

}  // namespace place_recognition
