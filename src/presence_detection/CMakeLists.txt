cmake_minimum_required(VERSION 3.8)
project(presence_detection)

# Automatically fetch AI weights if they are missing
message(STATUS "Checking and downloading AI models...")

# Ensure CMake re-runs this script if we edit the python or JSON files
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "scripts/download_models.py" "scripts/models.json")

execute_process(
  COMMAND python3 scripts/download_models.py
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  RESULT_VARIABLE DOWNLOAD_RESULT
)
if(NOT DOWNLOAD_RESULT EQUAL 0)
  message(FATAL_ERROR "Failed to download AI models! Check network connection.")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(base_node REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(OpenCV REQUIRED)
find_package(ament_index_cpp REQUIRED)

include_directories(
  include
  ${OpenCV_INCLUDE_DIRS}
)

# camera_node_c
add_library(camera_node src/camera_node.cpp)
target_include_directories(camera_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(camera_node rclcpp base_node sensor_msgs std_msgs cv_bridge OpenCV)

add_executable(camera_node_exe src/camera_node_exe.cpp)
target_link_libraries(camera_node_exe camera_node)
ament_target_dependencies(camera_node_exe rclcpp base_node sensor_msgs std_msgs cv_bridge OpenCV)

# Use FetchContent to instantly download the pre-compiled GPU binaries
include(FetchContent)
FetchContent_Declare(
  onnxruntime
  URL https://github.com/microsoft/onnxruntime/releases/download/v1.19.0/onnxruntime-linux-x64-gpu-1.19.0.tgz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(onnxruntime)

# perception_node_c
add_library(perception_node src/perception_node.cpp)
target_include_directories(perception_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
  ${onnxruntime_SOURCE_DIR}/include
)
target_link_libraries(perception_node ${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.so)
ament_target_dependencies(perception_node rclcpp base_node sensor_msgs std_msgs cv_bridge OpenCV ament_index_cpp)

add_executable(perception_node_exe src/perception_node_exe.cpp)
target_link_libraries(perception_node_exe perception_node)
ament_target_dependencies(perception_node_exe rclcpp base_node sensor_msgs std_msgs cv_bridge OpenCV ament_index_cpp)

# Ensure the executable knows where to find the downloaded .so files and CUDA libs at runtime
set_target_properties(perception_node_exe PROPERTIES BUILD_RPATH "${onnxruntime_SOURCE_DIR}/lib:/usr/local/cuda/lib64:/usr/local/cuda-12.8/lib64" INSTALL_RPATH "${onnxruntime_SOURCE_DIR}/lib:/usr/local/cuda/lib64:/usr/local/cuda-12.8/lib64")


install(TARGETS
  camera_node_exe
  perception_node_exe
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY include/
  DESTINATION include
)

install(DIRECTORY models/
  DESTINATION share/${PROJECT_NAME}/models
)

ament_export_include_directories(include)
ament_package()
