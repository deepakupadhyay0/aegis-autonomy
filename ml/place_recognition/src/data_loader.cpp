#include "place_recognition/data_loader.hpp"

#include <H5Cpp.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace place_recognition
{
namespace
{

struct hdf5_array_s
{
  std::vector<hsize_t> dimensions;
  std::vector<double> values;
};

hdf5_array_s read_array(
  const H5::H5File & file,
  const std::string & name)
{
  const H5::DataSet data = file.openDataSet(name);
  const H5::DataSpace space = data.getSpace();
  const std::int32_t rank = space.getSimpleExtentNdims();
  if (rank <= 0) {
    throw std::runtime_error(name + " has no dimensions");
  }

  hdf5_array_s result;
  result.dimensions.resize(static_cast<std::size_t>(rank));
  space.getSimpleExtentDims(result.dimensions.data());
  const std::size_t element_count = std::accumulate(
    result.dimensions.begin(),
    result.dimensions.end(),
    std::size_t{1U},
    [](const std::size_t left, const hsize_t right) {
      return left * static_cast<std::size_t>(right);
    });
  result.values.resize(element_count);
  data.read(result.values.data(), H5::PredType::NATIVE_DOUBLE);
  return result;
}

std::size_t offset_of(
  const std::vector<hsize_t> & dimensions,
  const std::vector<std::size_t> & indices)
{
  if (dimensions.size() != indices.size()) {
    throw std::invalid_argument("HDF5 index rank does not match dataset rank");
  }

  std::size_t offset = 0U;
  for (std::size_t axis = 0U; axis < dimensions.size(); ++axis) {
    if (indices[axis] >= static_cast<std::size_t>(dimensions[axis])) {
      throw std::out_of_range("HDF5 index exceeds a dataset dimension");
    }
    offset = offset * static_cast<std::size_t>(dimensions[axis]) + indices[axis];
  }
  return offset;
}

std::size_t find_axis(
  const std::vector<hsize_t> & dimensions,
  const std::size_t extent,
  const std::size_t excluded_axis = static_cast<std::size_t>(-1))
{
  for (std::size_t axis = 0U; axis < dimensions.size(); ++axis) {
    if (axis != excluded_axis &&
      static_cast<std::size_t>(dimensions[axis]) == extent)
    {
      return axis;
    }
  }
  throw std::runtime_error("Could not infer WiFi HDF5 axis ordering");
}

double matrix_value(
  const hdf5_array_s & array,
  const std::size_t row_axis,
  const std::size_t row,
  const std::size_t column_axis,
  const std::size_t column)
{
  std::vector<std::size_t> index(2U, 0U);
  index[row_axis] = row;
  index[column_axis] = column;
  return array.values[offset_of(array.dimensions, index)];
}


void load_camera_manifest(
  place_dataset_s & dataset,
  const std::filesystem::path & manifest_path)
{
  if (manifest_path.empty()) {
    return;
  }

  std::ifstream stream(manifest_path);
  if (!stream.is_open()) {
    throw std::runtime_error(
            "Could not open camera manifest: " + manifest_path.string());
  }

  std::string line;
  std::size_t line_number = 0U;
  while (std::getline(stream, line)) {
    ++line_number;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || line.front() == '#') {
      continue;
    }

    const std::size_t separator = line.find(',');
    if (separator == std::string::npos) {
      throw std::runtime_error(
              "Camera manifest line " + std::to_string(line_number) +
              " must contain sample_index,image_path");
    }
    const std::string index_text = line.substr(0U, separator);
    if (index_text == "sample_index") {
      continue;
    }

    const std::int64_t sample_index = std::stoll(index_text);
    if (sample_index < 0 ||
      sample_index >= static_cast<std::int64_t>(
        dataset.camera_frame_paths.size()))
    {
      throw std::runtime_error(
              "Camera manifest sample index is outside the WiFi dataset");
    }
    std::filesystem::path image_path{line.substr(separator + 1U)};
    if (image_path.empty()) {
      continue;
    }
    if (image_path.is_relative()) {
      image_path = manifest_path.parent_path() / image_path;
    }
    std::filesystem::path & destination = dataset.camera_frame_paths[
      static_cast<std::size_t>(sample_index)];
    if (!destination.empty()) {
      throw std::runtime_error(
              "Camera manifest contains a duplicate sample index");
    }
    destination = image_path.lexically_normal();
  }
}

}  // namespace

place_dataset_s load_dataset(
  const std::filesystem::path & channels_path,
  const std::filesystem::path & camera_manifest_path)
{
  const H5::H5File file(channels_path.string(), H5F_ACC_RDONLY);
  const hdf5_array_s labels = read_array(file, "labels");
  const hdf5_array_s rssi = read_array(file, "ap_rssi_synced");
  const hdf5_array_s noise = read_array(file, "ap_hw_noise_synced");

  if (labels.dimensions.size() != 2U || rssi.dimensions.size() != 3U ||
    noise.dimensions.size() != 2U)
  {
    throw std::runtime_error(
            "WiFi labels, ap_rssi_synced, and ap_hw_noise_synced have "
            "unexpected ranks");
  }

  const std::size_t label_component_axis = find_axis(labels.dimensions, 3U);
  const std::size_t label_sample_axis = 1U - label_component_axis;
  const std::size_t sample_count =
    static_cast<std::size_t>(labels.dimensions[label_sample_axis]);
  const std::size_t noise_sample_axis = find_axis(noise.dimensions, sample_count);
  const std::size_t noise_access_point_axis = 1U - noise_sample_axis;
  const std::size_t access_point_count =
    static_cast<std::size_t>(noise.dimensions[noise_access_point_axis]);
  const std::size_t rssi_sample_axis = find_axis(rssi.dimensions, sample_count);
  const std::size_t rssi_access_point_axis = find_axis(
    rssi.dimensions, access_point_count, rssi_sample_axis);
  std::size_t rssi_antenna_axis = 0U;
  while (rssi_antenna_axis == rssi_sample_axis ||
    rssi_antenna_axis == rssi_access_point_axis)
  {
    ++rssi_antenna_axis;
  }
  const std::size_t antenna_count =
    static_cast<std::size_t>(rssi.dimensions[rssi_antenna_axis]);
  const std::size_t feature_count = antenna_count + 1U;

  place_dataset_s result;
  result.wifi_features = torch::zeros(
    {
      static_cast<std::int64_t>(sample_count),
      static_cast<std::int64_t>(access_point_count),
      static_cast<std::int64_t>(feature_count)
    },
    torch::TensorOptions().dtype(torch::kFloat32));
  result.wifi_access_point_mask = torch::zeros(
    {
      static_cast<std::int64_t>(sample_count),
      static_cast<std::int64_t>(access_point_count)
    },
    torch::TensorOptions().dtype(torch::kBool));
  result.reference_pose = torch::empty(
    {static_cast<std::int64_t>(sample_count), 3},
    torch::TensorOptions().dtype(torch::kFloat32));
  result.camera_frame_paths.resize(sample_count);

  torch::TensorAccessor<float, 3> features = result.wifi_features.accessor<float, 3>();
  torch::TensorAccessor<bool, 2> mask =
    result.wifi_access_point_mask.accessor<bool, 2>();
  torch::TensorAccessor<float, 2> poses =
    result.reference_pose.accessor<float, 2>();

  for (std::size_t sample = 0U; sample < sample_count; ++sample) {
    for (std::size_t component = 0U; component < 3U; ++component) {
      const double value = matrix_value(
        labels,
        label_sample_axis,
        sample,
        label_component_axis,
        component);
      if (!std::isfinite(value)) {
        throw std::runtime_error("WiFi labels contain a non-finite reference pose");
      }
      poses[static_cast<std::int64_t>(sample)]
        [static_cast<std::int64_t>(component)] = static_cast<float>(value);
    }

    for (std::size_t access_point = 0U;
      access_point < access_point_count;
      ++access_point)
    {
      const double noise_value = matrix_value(
        noise,
        noise_sample_axis,
        sample,
        noise_access_point_axis,
        access_point);
      bool has_measurement = false;
      std::vector<std::size_t> rssi_index(3U, 0U);
      rssi_index[rssi_sample_axis] = sample;
      rssi_index[rssi_access_point_axis] = access_point;
      for (std::size_t antenna = 0U; antenna < antenna_count; ++antenna) {
        rssi_index[rssi_antenna_axis] = antenna;
        const double rssi_value = rssi.values[
          offset_of(rssi.dimensions, rssi_index)];
        if (std::isfinite(rssi_value) && std::isfinite(noise_value)) {
          features[static_cast<std::int64_t>(sample)]
            [static_cast<std::int64_t>(access_point)]
            [static_cast<std::int64_t>(antenna)] =
            static_cast<float>(rssi_value - noise_value);
          has_measurement = true;
        }
      }
      if (has_measurement) {
        features[static_cast<std::int64_t>(sample)]
          [static_cast<std::int64_t>(access_point)]
          [static_cast<std::int64_t>(antenna_count)] =
          static_cast<float>(noise_value);
        mask[static_cast<std::int64_t>(sample)]
          [static_cast<std::int64_t>(access_point)] = true;
      }
    }
  }

  if (!result.wifi_access_point_mask.any().item<bool>()) {
    throw std::runtime_error("WiFi data contains no finite robot-side RSSI samples");
  }
  load_camera_manifest(result, camera_manifest_path);
  return result;
}

camera_batch_s load_camera_batch(
  const place_dataset_s & dataset,
  const torch::Tensor & sample_indices,
  const std::int64_t image_width,
  const std::int64_t image_height)
{
  if (sample_indices.dim() != 1 || sample_indices.scalar_type() != torch::kInt64 ||
    sample_indices.device().is_cuda() || image_width <= 0 || image_height <= 0)
  {
    throw std::invalid_argument("Invalid camera batch request");
  }

  const torch::Tensor contiguous_indices = sample_indices.contiguous();
  const torch::TensorAccessor<std::int64_t, 1> indices =
    contiguous_indices.accessor<std::int64_t, 1>();
  camera_batch_s result;
  result.images = torch::zeros(
    {sample_indices.size(0), 3, image_height, image_width},
    torch::TensorOptions().dtype(torch::kFloat32));
  result.available = torch::zeros(
    {sample_indices.size(0)}, torch::TensorOptions().dtype(torch::kBool));
  torch::TensorAccessor<bool, 1> available = result.available.accessor<bool, 1>();
  const torch::Tensor mean = torch::tensor({0.485F, 0.456F, 0.406F})
    .view({3, 1, 1});
  const torch::Tensor deviation = torch::tensor({0.229F, 0.224F, 0.225F})
    .view({3, 1, 1});

  for (std::int64_t batch_index = 0;
    batch_index < sample_indices.size(0);
    ++batch_index)
  {
    const std::int64_t sample_index = indices[batch_index];
    if (sample_index < 0 ||
      sample_index >= static_cast<std::int64_t>(
        dataset.camera_frame_paths.size()))
    {
      throw std::out_of_range("Camera sample index is outside the dataset");
    }
    const std::filesystem::path & image_path = dataset.camera_frame_paths[
      static_cast<std::size_t>(sample_index)];
    if (image_path.empty()) {
      continue;
    }

    const cv::Mat decoded = cv::imread(image_path.string(), cv::IMREAD_COLOR);
    if (decoded.empty()) {
      throw std::runtime_error(
              "Could not decode camera frame: " + image_path.string());
    }
    cv::Mat resized;
    cv::resize(
      decoded,
      resized,
      cv::Size(
        static_cast<std::int32_t>(image_width),
        static_cast<std::int32_t>(image_height)));
    cv::Mat rgb;
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
    cv::Mat float_image;
    rgb.convertTo(float_image, CV_32FC3, 1.0 / 255.0);
    const torch::Tensor image = torch::from_blob(
      float_image.data,
      {image_height, image_width, 3},
      torch::TensorOptions().dtype(torch::kFloat32))
      .permute({2, 0, 1});
    result.images[batch_index].copy_((image - mean) / deviation);
    available[batch_index] = true;
  }
  return result;
}

}  // namespace place_recognition
