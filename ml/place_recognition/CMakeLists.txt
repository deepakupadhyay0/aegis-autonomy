cmake_minimum_required(VERSION 3.24)
project(place_recognition_experiment LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ament_cmake REQUIRED)
find_package(HDF5 REQUIRED COMPONENTS CXX)
find_package(OpenCV REQUIRED COMPONENTS core imgcodecs imgproc)

add_library(place_recognition_experiment
  src/data_loader.cpp
  src/dataset.cpp
)
add_subdirectory(
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/place_recognition
  ${CMAKE_CURRENT_BINARY_DIR}/place_recognition_core
)
target_include_directories(place_recognition_experiment PUBLIC include)
target_include_directories(place_recognition_experiment PRIVATE
  ${HDF5_INCLUDE_DIRS}
  ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(place_recognition_experiment PUBLIC
  place_recognition::place_recognition
)
target_link_libraries(place_recognition_experiment PRIVATE
  ${HDF5_CXX_LIBRARIES}
  ${OpenCV_LIBRARIES}
)
target_compile_options(place_recognition_experiment
  PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

add_executable(place_model src/place_model.cpp)
target_link_libraries(place_model PRIVATE place_recognition_experiment)

include(CTest)
if(BUILD_TESTING)
  add_executable(test_place_encoder test/test_place_encoder.cpp)
  target_link_libraries(test_place_encoder PRIVATE place_recognition::place_recognition)
  add_test(NAME place_encoder COMMAND test_place_encoder)
endif()

