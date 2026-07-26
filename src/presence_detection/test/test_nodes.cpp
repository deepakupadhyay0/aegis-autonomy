#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <string>
#include <stdexcept>

#include "rclcpp/rclcpp.hpp"
#include "presence_detection/camera_node.hpp"
#include "presence_detection/perception_node.hpp"
#include "base_node/base_node.hpp"

class TestNodeConfig
{
public:
  static void set_name(const std::string & name) noexcept
  {
    get_instance().m_name = name;
  }

  static const char * get_name() noexcept
  {
    return get_instance().m_name.c_str();
  }

private:
  static TestNodeConfig & get_instance() noexcept
  {
    static TestNodeConfig instance;
    return instance;
  }

  std::string m_name = "test_node";
};

namespace base_node
{
const char * get_node_name() noexcept
{
  return TestNodeConfig::get_name();
}
}  // namespace base_node

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
  TestNodeConfig::set_name("perception_node");
  auto node = std::make_shared<presence_detection::perception_node_c>(std::vector<std::string>{});
  ASSERT_NE(node, nullptr);
  EXPECT_STREQ(node->get_name(), "perception_node");
}

TEST_F(NodeTestFixture, PerceptionNodeResourceAllocation)
{
  TestNodeConfig::set_name("perception_node");
  auto node = std::make_shared<presence_detection::perception_node_c>(std::vector<std::string>{});
  ASSERT_NE(node, nullptr);

  EXPECT_NO_THROW({
    node->step1_allocate_resources({});
  });
}

TEST_F(NodeTestFixture, CameraNodeInitialization)
{
  TestNodeConfig::set_name("camera_node");
  auto node = std::make_shared<presence_detection::camera_node_c>(std::vector<std::string>{});
  ASSERT_NE(node, nullptr);
  EXPECT_STREQ(node->get_name(), "camera_node");
}

TEST_F(NodeTestFixture, CameraNodeHardwareFallbackHandling)
{
  TestNodeConfig::set_name("camera_node");
  auto node = std::make_shared<presence_detection::camera_node_c>(std::vector<std::string>{});
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

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
