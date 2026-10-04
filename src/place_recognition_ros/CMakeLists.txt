cmake_minimum_required(VERSION 3.24)
project(place_recognition_ros)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall>
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wextra>
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wpedantic>
)

find_package(ament_cmake REQUIRED)
find_package(autonomy_config REQUIRED)
find_package(autonomy_msgs REQUIRED)
find_package(base_core REQUIRED)
find_package(common REQUIRED)
find_package(logging REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgcodecs imgproc)

# place_recognition exports Torch. Keep CMake's CUDA architecture setting away
# from Torch, which manages its own architecture list.
unset(CMAKE_CUDA_ARCHITECTURES)
unset(CMAKE_CUDA_ARCHITECTURES CACHE)
if(DEFINED ENV{TORCH_CUDA_ARCH_LIST} AND
  NOT "$ENV{TORCH_CUDA_ARCH_LIST}" STREQUAL "")
  set(TORCH_CUDA_ARCH_LIST "$ENV{TORCH_CUDA_ARCH_LIST}")
endif()
if(DEFINED ENV{TORCH_CMAKE_PREFIX} AND
  NOT "$ENV{TORCH_CMAKE_PREFIX}" STREQUAL "")
  set(
    Torch_DIR
    "$ENV{TORCH_CMAKE_PREFIX}/Torch"
    CACHE PATH "Path containing TorchConfig.cmake" FORCE)
endif()
# place_recognition imports Torch, whose Caffe2 config probes nvtx3.
set(FPHSA_NAME_MISMATCHED TRUE)
find_package(place_recognition REQUIRED)
unset(FPHSA_NAME_MISMATCHED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)

add_executable(place_recognition_node
  src/place_recognition_config.cpp
  src/place_recognition_node.cpp
  src/place_recognition_node_exe.cpp
)
target_include_directories(place_recognition_node PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(place_recognition_node
  place_recognition::place_recognition
  ${OpenCV_LIBRARIES}
)
ament_target_dependencies(place_recognition_node
  autonomy_config
  autonomy_msgs
  base_core
  common
  logging
  rclcpp
  sensor_msgs
)

install(
  TARGETS place_recognition_node
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_cmake_cppcheck REQUIRED)
  find_package(ament_cmake_uncrustify REQUIRED)

  ament_cppcheck()
  ament_uncrustify()
endif()

ament_package()
