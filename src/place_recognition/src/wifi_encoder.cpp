#include "place_recognition/wifi_encoder.hpp"

#include <stdexcept>
#include <tuple>

namespace place_recognition
{

wifi_encoder_c::wifi_encoder_c(
  const std::int64_t feature_count,
  const std::int64_t access_point_count,
  const std::int64_t hidden_size,
  const std::int64_t descriptor_size)
: m_feature_projection(register_module(
      "feature_projection",
      torch::nn::Linear(feature_count, hidden_size))),
  m_access_point_embedding(register_module(
      "access_point_embedding",
      torch::nn::Embedding(access_point_count, hidden_size))),
  m_token_encoder(register_module(
      "token_encoder",
      torch::nn::Sequential(
        torch::nn::LayerNorm(torch::nn::LayerNormOptions({hidden_size})),
        torch::nn::ReLU(),
        torch::nn::Linear(hidden_size, hidden_size),
        torch::nn::ReLU()))),
  m_descriptor_head(register_module(
      "descriptor_head",
      torch::nn::Sequential(
        torch::nn::Linear(hidden_size * 2, hidden_size),
        torch::nn::ReLU(),
        torch::nn::Linear(hidden_size, descriptor_size))))
{
  if (feature_count <= 0 || access_point_count <= 0 || hidden_size <= 0 ||
    descriptor_size <= 0)
  {
    throw std::invalid_argument("WiFi encoder dimensions must be positive");
  }
}

torch::Tensor wifi_encoder_c::forward(
  const torch::Tensor & features,
  const torch::Tensor & access_point_mask)
{
  if (features.dim() != 3 || access_point_mask.dim() != 2 ||
    features.size(0) != access_point_mask.size(0) ||
    features.size(1) != access_point_mask.size(1))
  {
    throw std::invalid_argument(
            "WiFi encoder expects [batch, access_point, feature] and "
            "[batch, access_point]");
  }

  const std::int64_t batch_size = features.size(0);
  const std::int64_t access_point_count = features.size(1);
  const torch::Tensor access_point_indices = torch::arange(
    access_point_count,
    torch::TensorOptions().dtype(torch::kInt64).device(features.device()))
    .unsqueeze(0)
    .expand({batch_size, access_point_count});

  torch::Tensor tokens = m_feature_projection->forward(features) +
    m_access_point_embedding->forward(access_point_indices);
  tokens = m_token_encoder->forward(tokens);

  const torch::Tensor expanded_mask = access_point_mask
    .to(torch::kBool)
    .unsqueeze(-1);
  const torch::Tensor numeric_mask = expanded_mask.to(tokens.dtype());
  const torch::Tensor valid_count = numeric_mask.sum(1).clamp_min(1.0);
  const torch::Tensor mean_pool = (tokens * numeric_mask).sum(1) / valid_count;

  const torch::Tensor masked_tokens = torch::where(
    expanded_mask,
    tokens,
    torch::full_like(tokens, -1.0e30));
  torch::Tensor max_pool = std::get<0>(masked_tokens.max(1));
  const torch::Tensor has_measurement = access_point_mask
    .to(torch::kBool)
    .any(1, true);
  max_pool = torch::where(
    has_measurement, max_pool, torch::zeros_like(max_pool));

  torch::Tensor descriptor = m_descriptor_head->forward(
    torch::cat({mean_pool, max_pool}, 1));
  const torch::Tensor norm = descriptor.square()
    .sum(1, true)
    .sqrt()
    .clamp_min(1.0e-12);
  descriptor = descriptor / norm;
  return descriptor;
}

}  // namespace place_recognition

