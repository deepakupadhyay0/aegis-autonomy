#include <gtest/gtest.h>

#include "base_core/base_node.hpp"
#include "logging/node_logging.hpp"
#include "presence_detection/camera_node.hpp"
#include "presence_detection/ipc/camera_ipc_validation.hpp"
#include "presence_detection/perception_node.hpp"
#include "rclcpp/rclcpp.hpp"

#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

base_core::base_node_options_s make_node_options(
  const common::string64_t & node_name)
{
  base_core::base_node_options_s options;
  options.node_name = node_name;
  options.logging = std::make_shared<logging::node_logging_adapter_c>();
  options.enable_executor = false;
  return options;
}

class testable_camera_node_c final : public presence_detection::camera_node_c
{
public:
  using presence_detection::camera_node_c::camera_node_c;
  using presence_detection::camera_node_c::step1_allocate_resources;
};

class testable_perception_node_c final :
  public presence_detection::perception_node_c
{
public:
  using presence_detection::perception_node_c::perception_node_c;
  using presence_detection::perception_node_c::step1_allocate_resources;
};

}  // namespace

class NodeTestFixture : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    rclcpp::init(0, nullptr);
  }

  static void TearDownTestSuite()
  {
    if (rclcpp::ok()) {
      rclcpp::shutdown();
    }
  }
};

TEST_F(NodeTestFixture, PerceptionNodeInitialization)
{
  const base_core::base_node_options_s options =
    make_node_options(common::string64_t{"perception_node"});
  std::shared_ptr<presence_detection::perception_node_c> node =
    std::make_shared<presence_detection::perception_node_c>(
    std::vector<std::string>{},
    options);
  ASSERT_NE(node, nullptr);
  EXPECT_STREQ(node->get_name(), "perception_node");
}

TEST_F(NodeTestFixture, PerceptionNodeResourceAllocation)
{
  const base_core::base_node_options_s options =
    make_node_options(common::string64_t{"perception_node"});
  std::shared_ptr<testable_perception_node_c> node =
    std::make_shared<testable_perception_node_c>(
    std::vector<std::string>{},
    options);
  ASSERT_NE(node, nullptr);

  EXPECT_NO_THROW({
    node->step1_allocate_resources({});
  });
}

TEST_F(NodeTestFixture, CameraNodeInitialization)
{
  const base_core::base_node_options_s options =
    make_node_options(common::string64_t{"camera_node"});
  std::shared_ptr<presence_detection::camera_node_c> node =
    std::make_shared<presence_detection::camera_node_c>(
    std::vector<std::string>{},
    options);
  ASSERT_NE(node, nullptr);
  EXPECT_STREQ(node->get_name(), "camera_node");
}

TEST_F(NodeTestFixture, CameraNodeHardwareFallbackHandling)
{
  const base_core::base_node_options_s options =
    make_node_options(common::string64_t{"camera_node"});
  std::shared_ptr<testable_camera_node_c> node =
    std::make_shared<testable_camera_node_c>(
    std::vector<std::string>{},
    options);
  ASSERT_NE(node, nullptr);

  try {
    node->step1_allocate_resources({});
  } catch (const std::runtime_error & e) {
    std::string err_msg(e.what());
    EXPECT_NE(err_msg.find("VideoCapture"), std::string::npos);
  } catch (const std::exception & e) {
    FAIL() << "Unexpected exception thrown: " << e.what();
  }
}

TEST(CameraIpcValidationTest, BuildsDescriptorFromCapturedFrame)
{
  const cv::Mat frame(3, 5, CV_8UC3);
  common::ipc::stream_descriptor_s descriptor;

  ASSERT_TRUE(presence_detection::ipc::make_camera_stream_descriptor(
      frame,
      3U,
      descriptor));
  EXPECT_EQ(descriptor.width, 5U);
  EXPECT_EQ(descriptor.height, 3U);
  EXPECT_EQ(descriptor.stride, 15U);
  EXPECT_EQ(descriptor.slot_size, 45U);
}

TEST(CameraIpcValidationTest, PacksRowsWithSourcePadding)
{
  cv::Mat backing(2, 4, CV_8UC3);
  for (common::int32_t row = 0; row < backing.rows; ++row) {
    for (common::int32_t column = 0; column < backing.cols; ++column) {
      backing.at<cv::Vec3b>(row, column) = cv::Vec3b(
        static_cast<common::uint8_t>(row),
        static_cast<common::uint8_t>(column),
        0U);
    }
  }
  const cv::Mat frame = backing(cv::Rect(0, 0, 3, 2));
  ASSERT_FALSE(frame.isContinuous());

  common::ipc::stream_descriptor_s descriptor;
  ASSERT_TRUE(presence_detection::ipc::make_camera_stream_descriptor(
      frame,
      3U,
      descriptor));
  std::vector<std::byte> destination(
    static_cast<std::size_t>(descriptor.slot_size));

  ASSERT_TRUE(presence_detection::ipc::copy_camera_frame_to_buffer(
      frame,
      descriptor,
      destination));
  for (common::uint32_t row = 0U; row < descriptor.height; ++row) {
    EXPECT_EQ(
      std::memcmp(
        destination.data() +
        (static_cast<std::size_t>(row) * descriptor.stride),
        frame.ptr(static_cast<common::int32_t>(row)),
        descriptor.stride),
      0);
  }
}

TEST(CameraIpcValidationTest, CopiesContinuousFrame)
{
  cv::Mat frame(2, 3, CV_8UC3);
  frame.setTo(cv::Scalar(1, 2, 3));
  ASSERT_TRUE(frame.isContinuous());

  common::ipc::stream_descriptor_s descriptor;
  ASSERT_TRUE(presence_detection::ipc::make_camera_stream_descriptor(
      frame,
      3U,
      descriptor));
  std::vector<std::byte> destination(
    static_cast<std::size_t>(descriptor.slot_size));

  ASSERT_TRUE(presence_detection::ipc::copy_camera_frame_to_buffer(
      frame,
      descriptor,
      destination));
  EXPECT_EQ(
    std::memcmp(
      destination.data(),
      frame.data,
      static_cast<std::size_t>(descriptor.slot_size)),
    0);
}

TEST(CameraIpcValidationTest, RejectsUnsupportedPixelFormat)
{
  const cv::Mat frame(3, 5, CV_8UC1);
  common::ipc::stream_descriptor_s descriptor;

  EXPECT_FALSE(presence_detection::ipc::make_camera_stream_descriptor(
      frame,
      3U,
      descriptor));
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
