#include "place_recognition/data_loader.hpp"
#include "place_recognition/place_encoder.hpp"

#include <torch/torch.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace
{

constexpr std::int64_t HIDDEN_SIZE = 128;
constexpr std::int64_t DESCRIPTOR_SIZE = 64;
constexpr std::int64_t BATCH_SIZE = 32;
constexpr std::int64_t TRAINING_STEPS_PER_EPOCH = 200;
constexpr std::int64_t INFERENCE_BATCH_SIZE = 32;
constexpr std::int64_t IMAGE_WIDTH = 160;
constexpr std::int64_t IMAGE_HEIGHT = 120;
constexpr std::int64_t MAXIMUM_SAMPLE_ATTEMPTS = 1024;
constexpr float POSITIVE_RADIUS_M = 1.5F;
constexpr float NEGATIVE_RADIUS_M = 8.0F;
constexpr float EVALUATION_RADIUS_M = 2.0F;
constexpr float TRIPLET_MARGIN = 0.2F;
constexpr float LEARNING_RATE = 1.0e-3F;
constexpr float SINGLE_MODALITY_TRAINING_PROBABILITY = 0.4F;
constexpr std::int64_t MINIMUM_TEMPORAL_SEPARATION = 100;

enum class modality_e : std::uint8_t
{
  wifi_only,
  camera_only,
  combined
};

struct split_s
{
  std::int64_t training_end{0};
  std::int64_t validation_end{0};
};

struct triplet_batch_s
{
  torch::Tensor anchor;
  torch::Tensor positive;
  torch::Tensor negative;
};

struct retrieval_metrics_s
{
  std::int64_t eligible_queries{0};
  double recall_at_1{0.0};
  double recall_at_5{0.0};
  double recall_at_10{0.0};
  double median_top_1_error_m{0.0};
};

float squared_planar_distance(
  const torch::TensorAccessor<float, 2> & poses,
  const std::int64_t left,
  const std::int64_t right)
{
  const float x = poses[left][0] - poses[right][0];
  const float y = poses[left][1] - poses[right][1];
  return (x * x) + (y * y);
}

split_s make_split(const std::int64_t sample_count)
{
  if (sample_count < 100) {
    throw std::runtime_error(
            "Place-recognition experiment requires at least 100 samples");
  }
  return split_s{(sample_count * 6) / 10, (sample_count * 8) / 10};
}

triplet_batch_s sample_triplets(
  const torch::Tensor & reference_pose,
  const std::int64_t training_end,
  std::mt19937_64 & random_engine)
{
  const torch::Tensor contiguous_pose = reference_pose.contiguous();
  const torch::TensorAccessor<float, 2> poses =
    contiguous_pose.accessor<float, 2>();
  std::uniform_int_distribution<std::int64_t> sample_distribution(
    0, training_end - 1);
  const float positive_limit_squared = POSITIVE_RADIUS_M * POSITIVE_RADIUS_M;
  const float negative_limit_squared = NEGATIVE_RADIUS_M * NEGATIVE_RADIUS_M;

  std::vector<std::int64_t> anchors;
  std::vector<std::int64_t> positives;
  std::vector<std::int64_t> negatives;
  anchors.reserve(static_cast<std::size_t>(BATCH_SIZE));
  positives.reserve(static_cast<std::size_t>(BATCH_SIZE));
  negatives.reserve(static_cast<std::size_t>(BATCH_SIZE));

  for (std::int64_t batch_index = 0;
    batch_index < BATCH_SIZE;
    ++batch_index)
  {
    bool found = false;
    for (std::int64_t attempt = 0;
      attempt < MAXIMUM_SAMPLE_ATTEMPTS && !found;
      ++attempt)
    {
      const std::int64_t anchor = sample_distribution(random_engine);
      const std::int64_t positive = sample_distribution(random_engine);
      const std::int64_t negative = sample_distribution(random_engine);
      if (std::abs(anchor - positive) < MINIMUM_TEMPORAL_SEPARATION ||
        squared_planar_distance(poses, anchor, positive) >
        positive_limit_squared ||
        squared_planar_distance(poses, anchor, negative) <
        negative_limit_squared)
      {
        continue;
      }

      anchors.push_back(anchor);
      positives.push_back(positive);
      negatives.push_back(negative);
      found = true;
    }
    if (!found) {
      throw std::runtime_error(
              "Could not sample spatial triplets; inspect the reference "
              "trajectory or adjust the radii");
    }
  }

  return triplet_batch_s{
    torch::tensor(anchors, torch::TensorOptions().dtype(torch::kInt64)),
    torch::tensor(positives, torch::TensorOptions().dtype(torch::kInt64)),
    torch::tensor(negatives, torch::TensorOptions().dtype(torch::kInt64))
  };
}

void apply_modality_dropout(
  torch::Tensor & wifi_mask,
  torch::Tensor & camera_available,
  std::mt19937_64 & random_engine)
{
  torch::TensorAccessor<bool, 2> wifi = wifi_mask.accessor<bool, 2>();
  torch::TensorAccessor<bool, 1> camera = camera_available.accessor<bool, 1>();
  std::uniform_real_distribution<float> distribution(0.0F, 1.0F);
  const float half_probability = SINGLE_MODALITY_TRAINING_PROBABILITY * 0.5F;

  for (std::int64_t sample = 0; sample < wifi_mask.size(0); ++sample) {
    bool has_wifi = false;
    for (std::int64_t access_point = 0;
      access_point < wifi_mask.size(1);
      ++access_point)
    {
      has_wifi = has_wifi || wifi[sample][access_point];
    }
    if (!has_wifi || !camera[sample]) {
      continue;
    }

    const float selection = distribution(random_engine);
    if (selection < half_probability) {
      for (std::int64_t access_point = 0;
        access_point < wifi_mask.size(1);
        ++access_point)
      {
        wifi[sample][access_point] = false;
      }
    } else if (selection < SINGLE_MODALITY_TRAINING_PROBABILITY) {
      camera[sample] = false;
    }
  }
}

torch::Tensor encode_indices(
  place_recognition::place_encoder_c & model,
  const place_recognition::place_dataset_s & dataset,
  const torch::Tensor & sample_indices,
  const torch::Device & device,
  const modality_e modality)
{
  torch::Tensor wifi_mask = dataset.wifi_access_point_mask
    .index_select(0, sample_indices)
    .clone();
  place_recognition::camera_batch_s camera =
    place_recognition::load_camera_batch(
    dataset, sample_indices, IMAGE_WIDTH, IMAGE_HEIGHT);
  if (modality == modality_e::wifi_only) {
    camera.available.zero_();
  } else if (modality == modality_e::camera_only) {
    wifi_mask.zero_();
  }

  return model.forward(
    dataset.wifi_features.index_select(0, sample_indices).to(device),
    wifi_mask.to(device),
    camera.images.to(device),
    camera.available.to(device));
}

torch::Tensor encode_training_indices(
  place_recognition::place_encoder_c & model,
  const place_recognition::place_dataset_s & dataset,
  const torch::Tensor & sample_indices,
  const torch::Device & device,
  std::mt19937_64 & random_engine)
{
  torch::Tensor wifi_mask = dataset.wifi_access_point_mask
    .index_select(0, sample_indices)
    .clone();
  place_recognition::camera_batch_s camera =
    place_recognition::load_camera_batch(
    dataset, sample_indices, IMAGE_WIDTH, IMAGE_HEIGHT);
  apply_modality_dropout(wifi_mask, camera.available, random_engine);
  return model.forward(
    dataset.wifi_features.index_select(0, sample_indices).to(device),
    wifi_mask.to(device),
    camera.images.to(device),
    camera.available.to(device));
}

torch::Tensor encode_range(
  place_recognition::place_encoder_c & model,
  const place_recognition::place_dataset_s & dataset,
  const torch::Device & device,
  const std::int64_t begin,
  const std::int64_t end,
  const modality_e modality)
{
  torch::NoGradGuard no_gradient;
  std::vector<torch::Tensor> batches;
  for (std::int64_t offset = begin; offset < end;
    offset += INFERENCE_BATCH_SIZE)
  {
    const std::int64_t count = std::min(
      INFERENCE_BATCH_SIZE, end - offset);
    const torch::Tensor indices = torch::arange(
      offset,
      offset + count,
      torch::TensorOptions().dtype(torch::kInt64));
    batches.push_back(encode_indices(
        model, dataset, indices, device, modality).to(torch::kCPU));
  }
  return torch::cat(batches, 0).contiguous();
}

bool candidates_contain_match(
  const torch::Tensor & candidate_indices,
  const torch::TensorAccessor<float, 2> & reference_poses,
  const torch::TensorAccessor<float, 2> & query_poses,
  const std::int64_t query_index,
  const std::int64_t candidate_count,
  const float radius_squared)
{
  const torch::TensorAccessor<std::int64_t, 1> candidates =
    candidate_indices.accessor<std::int64_t, 1>();
  for (std::int64_t index = 0; index < candidate_count; ++index) {
    const std::int64_t candidate = candidates[index];
    const float x = reference_poses[candidate][0] - query_poses[query_index][0];
    const float y = reference_poses[candidate][1] - query_poses[query_index][1];
    if ((x * x) + (y * y) <= radius_squared) {
      return true;
    }
  }
  return false;
}

retrieval_metrics_s evaluate_descriptors(
  const place_recognition::place_dataset_s & dataset,
  const torch::Tensor & reference_descriptors,
  const torch::Tensor & query_descriptors,
  const std::int64_t reference_end,
  const std::int64_t query_begin,
  const std::int64_t query_end)
{
  const torch::Tensor reference_poses_tensor = dataset.reference_pose
    .narrow(0, 0, reference_end)
    .contiguous();
  const torch::Tensor query_poses_tensor = dataset.reference_pose
    .narrow(0, query_begin, query_end - query_begin)
    .contiguous();
  const torch::TensorAccessor<float, 2> reference_poses =
    reference_poses_tensor.accessor<float, 2>();
  const torch::TensorAccessor<float, 2> query_poses =
    query_poses_tensor.accessor<float, 2>();
  const std::int64_t maximum_k = std::min<std::int64_t>(10, reference_end);
  const float radius_squared = EVALUATION_RADIUS_M * EVALUATION_RADIUS_M;

  std::int64_t matches_at_1 = 0;
  std::int64_t matches_at_5 = 0;
  std::int64_t matches_at_10 = 0;
  std::vector<double> top_1_errors;
  top_1_errors.reserve(static_cast<std::size_t>(query_end - query_begin));
  retrieval_metrics_s result;

  for (std::int64_t query = 0; query < query_end - query_begin; ++query) {
    const torch::Tensor delta = reference_poses_tensor.index(
      {torch::indexing::Slice(), torch::indexing::Slice(0, 2)}) -
      query_poses_tensor.index({query, torch::indexing::Slice(0, 2)});
    const torch::Tensor distance_squared = delta.square().sum(1);
    if (distance_squared.min().item<float>() > radius_squared) {
      continue;
    }

    ++result.eligible_queries;
    const torch::Tensor similarities = torch::matmul(
      reference_descriptors, query_descriptors[query]);
    const torch::Tensor candidates = std::get<1>(
      similarities.topk(maximum_k)).to(torch::kInt64).contiguous();
    matches_at_1 += candidates_contain_match(
      candidates, reference_poses, query_poses, query, 1, radius_squared);
    matches_at_5 += candidates_contain_match(
      candidates,
      reference_poses,
      query_poses,
      query,
      std::min<std::int64_t>(5, maximum_k),
      radius_squared);
    matches_at_10 += candidates_contain_match(
      candidates,
      reference_poses,
      query_poses,
      query,
      maximum_k,
      radius_squared);
    const std::int64_t top_1 = candidates[0].item<std::int64_t>();
    top_1_errors.push_back(std::sqrt(
        static_cast<double>(distance_squared[top_1].item<float>())));
  }

  if (result.eligible_queries == 0) {
    throw std::runtime_error(
            "The reference and query trajectory sections have no spatial overlap");
  }
  const double denominator = static_cast<double>(result.eligible_queries);
  result.recall_at_1 = static_cast<double>(matches_at_1) / denominator;
  result.recall_at_5 = static_cast<double>(matches_at_5) / denominator;
  result.recall_at_10 = static_cast<double>(matches_at_10) / denominator;
  std::sort(top_1_errors.begin(), top_1_errors.end());
  result.median_top_1_error_m = top_1_errors[top_1_errors.size() / 2U];
  return result;
}

retrieval_metrics_s evaluate_model(
  place_recognition::place_encoder_c & model,
  const place_recognition::place_dataset_s & dataset,
  const torch::Device & device,
  const std::int64_t reference_end,
  const std::int64_t query_begin,
  const std::int64_t query_end,
  const modality_e modality)
{
  model.eval();
  return evaluate_descriptors(
    dataset,
    encode_range(model, dataset, device, 0, reference_end, modality),
    encode_range(
      model, dataset, device, query_begin, query_end, modality),
    reference_end,
    query_begin,
    query_end);
}

torch::Tensor fingerprint_descriptors(
  const place_recognition::place_dataset_s & dataset,
  const std::int64_t begin,
  const std::int64_t end)
{
  torch::Tensor descriptors = dataset.wifi_features
    .narrow(0, begin, end - begin)
    .flatten(1)
    .contiguous();
  const torch::Tensor norms = descriptors.square()
    .sum(1, true)
    .sqrt()
    .clamp_min(1.0e-12);
  return descriptors / norms;
}

bool has_complete_camera_range(
  const place_recognition::place_dataset_s & dataset,
  const std::int64_t begin,
  const std::int64_t end)
{
  for (std::int64_t sample = begin; sample < end; ++sample) {
    if (dataset.camera_frame_paths[static_cast<std::size_t>(sample)].empty()) {
      return false;
    }
  }
  return true;
}

void print_metrics(
  const std::string_view name,
  const retrieval_metrics_s & metrics)
{
  std::cout << name << ": eligible_queries=" << metrics.eligible_queries
            << " recall@1=" << metrics.recall_at_1
            << " recall@5=" << metrics.recall_at_5
            << " recall@10=" << metrics.recall_at_10
            << " median_top1_error_m=" << metrics.median_top_1_error_m
            << '\n';
}

void save_model(
  place_recognition::place_encoder_c & model,
  const std::filesystem::path & path)
{
  torch::serialize::OutputArchive archive;
  model.save(archive);
  archive.save_to(path.string());
}

void load_model(
  place_recognition::place_encoder_c & model,
  const std::filesystem::path & path)
{
  torch::serialize::InputArchive archive;
  archive.load_from(path.string());
  model.load(archive);
}

torch::Device choose_device()
{
  return torch::cuda::is_available() ?
         torch::Device(torch::kCUDA) : torch::Device(torch::kCPU);
}

void train(
  place_recognition::place_encoder_c & model,
  const place_recognition::place_dataset_s & dataset,
  const split_s & split,
  const torch::Device & device,
  const std::filesystem::path & model_path,
  const std::int64_t epoch_count)
{
  model.to(device);
  torch::optim::Adam optimizer(
    model.parameters(), torch::optim::AdamOptions(LEARNING_RATE));
  std::mt19937_64 random_engine{0x57414953U};
  double best_recall = -1.0;

  for (std::int64_t epoch = 0; epoch < epoch_count; ++epoch) {
    model.train();
    double epoch_loss = 0.0;
    for (std::int64_t step = 0;
      step < TRAINING_STEPS_PER_EPOCH;
      ++step)
    {
      const triplet_batch_s batch = sample_triplets(
        dataset.reference_pose, split.training_end, random_engine);
      const torch::Tensor anchor_descriptor = encode_training_indices(
        model, dataset, batch.anchor, device, random_engine);
      const torch::Tensor positive_descriptor = encode_training_indices(
        model, dataset, batch.positive, device, random_engine);
      const torch::Tensor negative_descriptor = encode_training_indices(
        model, dataset, batch.negative, device, random_engine);
      const torch::Tensor positive_distance =
        (anchor_descriptor - positive_descriptor).square().sum(1);
      const torch::Tensor negative_distance =
        (anchor_descriptor - negative_descriptor).square().sum(1);
      const torch::Tensor loss = torch::relu(
        positive_distance - negative_distance + TRIPLET_MARGIN).mean();

      optimizer.zero_grad();
      loss.backward();
      optimizer.step();
      epoch_loss += loss.item<double>();
    }

    const retrieval_metrics_s validation = evaluate_model(
      model,
      dataset,
      device,
      split.training_end,
      split.training_end,
      split.validation_end,
      modality_e::combined);
    std::cout << "epoch=" << (epoch + 1)
              << " mean_loss="
              << epoch_loss / static_cast<double>(TRAINING_STEPS_PER_EPOCH)
              << ' ';
    print_metrics("validation", validation);
    if (validation.recall_at_5 > best_recall) {
      best_recall = validation.recall_at_5;
      save_model(model, model_path);
    }
  }
}

void print_usage(const char * executable)
{
  std::cerr << "Usage:\n  " << executable
            << " train <channels.mat> <model.pt> [epochs] [camera_manifest.csv]"
            << "\n  " << executable
            << " evaluate <channels.mat> <model.pt> [camera_manifest.csv]\n";
}

}  // namespace

int main(const int argc, const char * const argv[])
{
  try {
    if (argc < 4) {
      print_usage(argv[0]);
      return 2;
    }

    const std::string command{argv[1]};
    const std::filesystem::path channels_path{argv[2]};
    const std::filesystem::path model_path{argv[3]};
    const std::int64_t epoch_count = command == "train" && argc >= 5 ?
      std::stoll(argv[4]) : 30;
    const std::filesystem::path camera_manifest_path = command == "train" ?
      (argc >= 6 ? std::filesystem::path{argv[5]} : std::filesystem::path{}) :
      (argc >= 5 ? std::filesystem::path{argv[4]} : std::filesystem::path{});
    place_recognition::place_dataset_s dataset =
      place_recognition::load_dataset(channels_path, camera_manifest_path);
    const split_s split = make_split(dataset.wifi_features.size(0));
    place_recognition::normalize_wifi_features(dataset, split.training_end);

    place_recognition::place_encoder_c model(
      dataset.wifi_features.size(2),
      dataset.wifi_features.size(1),
      HIDDEN_SIZE,
      DESCRIPTOR_SIZE);
    const torch::Device device = choose_device();
    const std::size_t camera_sample_count = static_cast<std::size_t>(
      std::count_if(
        dataset.camera_frame_paths.begin(),
        dataset.camera_frame_paths.end(),
        [](const std::filesystem::path & path) {return !path.empty();}));
    std::cout << "samples=" << dataset.wifi_features.size(0)
              << " camera_samples=" << camera_sample_count
              << " access_points=" << dataset.wifi_features.size(1)
              << " features_per_access_point=" << dataset.wifi_features.size(2)
              << " device=" << device << '\n';

    if (command == "train") {
      if (epoch_count <= 0) {
        throw std::invalid_argument("Epoch count must be positive");
      }
      train(model, dataset, split, device, model_path, epoch_count);
      load_model(model, model_path);
    } else if (command == "evaluate") {
      load_model(model, model_path);
    } else {
      print_usage(argv[0]);
      return 2;
    }

    const retrieval_metrics_s fingerprint_baseline = evaluate_descriptors(
      dataset,
      fingerprint_descriptors(dataset, 0, split.training_end),
      fingerprint_descriptors(
        dataset, split.validation_end, dataset.wifi_features.size(0)),
      split.training_end,
      split.validation_end,
      dataset.wifi_features.size(0));
    print_metrics("raw_wifi_fingerprint", fingerprint_baseline);

    model.to(device);
    print_metrics(
      "learned_wifi_only",
      evaluate_model(
        model,
        dataset,
        device,
        split.training_end,
        split.validation_end,
        dataset.wifi_features.size(0),
        modality_e::wifi_only));

    if (camera_sample_count > 0U) {
      print_metrics(
        "learned_combined",
        evaluate_model(
          model,
          dataset,
          device,
          split.training_end,
          split.validation_end,
          dataset.wifi_features.size(0),
          modality_e::combined));
      const bool complete_camera_coverage = has_complete_camera_range(
        dataset, 0, split.training_end) &&
        has_complete_camera_range(
        dataset, split.validation_end, dataset.wifi_features.size(0));
      if (complete_camera_coverage) {
        print_metrics(
          "learned_camera_only",
          evaluate_model(
            model,
            dataset,
            device,
            split.training_end,
            split.validation_end,
            dataset.wifi_features.size(0),
            modality_e::camera_only));
      } else {
        std::cout << "learned_camera_only: skipped because the camera manifest "
                  << "does not cover every reference and test sample\n";
      }
    }
    return 0;
  } catch (const std::exception & error) {
    std::cerr << "place_model: " << error.what() << '\n';
    return 1;
  }
}
