#include <gtest/gtest.h>

#include "accelerator/cuda/operations.hpp"
#include "accelerator/types.hpp"
#include "cuda_pattern_test_accelerator.hpp"
#include "common/numeric_types.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace
{

using scalar_t = common::float32_t;
using shape2_t = accelerator::tensor_shape_s<2U>;
using host_input_t = accelerator::host_tensor_view_c<const scalar_t, 2U>;
using host_output_t = accelerator::host_tensor_view_c<scalar_t, 2U>;
using buffer2_t = accelerator::buffer_c<scalar_t, 2U>;

static_assert(accelerator::accelerator_value<scalar_t>);
static_assert(accelerator::accelerator_value<common::uint32_t>);
static_assert(accelerator::numeric_operation_value<scalar_t>);
static_assert(accelerator::numeric_operation_value<common::uint32_t>);
static_assert(accelerator::floating_operation_value<scalar_t>);
static_assert(!accelerator::floating_operation_value<common::uint32_t>);
static_assert(!accelerator::numeric_operation_value<const scalar_t>);
static_assert(!accelerator::accelerator_value<const scalar_t>);
static_assert(!accelerator::accelerator_value<scalar_t *>);
static_assert(!std::is_copy_constructible_v<buffer2_t>);
static_assert(std::is_move_constructible_v<buffer2_t>);
static_assert(
  !std::is_constructible_v<
    accelerator::device_tensor_view_c<const scalar_t, 2U>,
    const scalar_t *,
    shape2_t,
    accelerator::tensor_strides_s<2U>>);

struct extension_provider_state_s
{
  accelerator::cuda::operation_context_s context{};
  common::uint32_t factory_calls{0U};
  common::uint32_t enqueue_calls{0U};
};

class test_extension_provider_c final : public accelerator::operation_provider_i
{
public:
  explicit test_extension_provider_c(extension_provider_state_s & state) noexcept
  : m_state(state)
  {
  }

  ~test_extension_provider_c() noexcept override = default;

  accelerator::status_e enqueue(
    const std::span<const accelerator::operation_argument_s> arguments) noexcept override
  {
    if (arguments.size() != 3U) {
      return accelerator::status_e::invalid_argument;
    }
    ++m_state.enqueue_calls;
    return accelerator::status_e::success;
  }

private:
  extension_provider_state_s & m_state;
};

class extension_test_accelerator_c final : private accelerator::accelerator_c
{
public:
  explicit extension_test_accelerator_c(extension_provider_state_s & state)
  : accelerator_c(accelerator::accelerator_config_s{}),
    m_operation(make_operation(
        [&state](const accelerator::cuda::operation_context_s & context) {
          ++state.factory_calls;
          state.context = context;
          return std::make_unique<test_extension_provider_c>(state);
        }))
  {
  }

  ~extension_test_accelerator_c() noexcept = default;

  accelerator::status_e exercise()
  {
    const accelerator::tensor_shape_s<1U> shape{{1U}};
    accelerator::buffer_c<scalar_t, 1U> left = make_buffer<scalar_t>(shape);
    accelerator::buffer_c<scalar_t, 1U> right = make_buffer<scalar_t>(shape);
    accelerator::buffer_c<scalar_t, 1U> result = make_buffer<scalar_t>(shape);
    return m_operation.execute(left, right, result);
  }

private:
  accelerator::operation_c m_operation;
};

class test_accelerator_c final : private accelerator::accelerator_c
{
public:
  test_accelerator_c()
  : accelerator_c(accelerator::accelerator_config_s{}),
    m_elementwise_multiply(make_operation(
        accelerator::cuda_operations::make_elementwise_multiply_provider)),
    m_matrix_multiply(make_operation(
        accelerator::cuda_operations::make_matrix_multiply_provider))
  {
  }

  ~test_accelerator_c() noexcept = default;

  using accelerator_c::clear_bytes;
  using accelerator_c::device_index;
  using accelerator_c::download;
  using accelerator_c::make_buffer;
  using accelerator_c::synchronize;
  using accelerator_c::upload;

  template<accelerator::numeric_operation_value value_t, std::size_t rank_v>
  accelerator::status_e elementwise_multiply(
    const accelerator::buffer_c<value_t, rank_v> & left,
    const accelerator::buffer_c<value_t, rank_v> & right,
    accelerator::buffer_c<value_t, rank_v> & result) noexcept
  {
    return m_elementwise_multiply.execute(left, right, result);
  }

  template<accelerator::numeric_operation_value value_t, std::size_t rank_v>
  accelerator::status_e elementwise_multiply(
    const accelerator::device_tensor_view_c<const value_t, rank_v> & left,
    const accelerator::device_tensor_view_c<const value_t, rank_v> & right,
    const accelerator::device_tensor_view_c<value_t, rank_v> & result) noexcept
  {
    return m_elementwise_multiply.execute(left, right, result);
  }

  template<accelerator::floating_operation_value value_t>
  accelerator::status_e matrix_multiply(
    const accelerator::buffer_c<value_t, 2U> & left,
    const accelerator::buffer_c<value_t, 2U> & right,
    accelerator::buffer_c<value_t, 2U> & result) noexcept
  {
    return m_matrix_multiply.execute(left, right, result);
  }

private:
  accelerator::operation_c m_elementwise_multiply;
  accelerator::operation_c m_matrix_multiply;
};

}  // namespace

TEST(AcceleratorTensorTest, CalculatesShapeAndContiguousStrides)
{
  const shape2_t shape{{2U, 3U}};
  const std::optional<std::size_t> count = shape.try_element_count();
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(count.value(), 6U);

  const std::array<scalar_t, 6U> values{};
  const std::optional<host_input_t> view =
    host_input_t::try_create_contiguous(std::span<const scalar_t>{values}, shape);
  ASSERT_TRUE(view.has_value());
  EXPECT_TRUE(view->contiguous());
  EXPECT_EQ(view->strides().elements[0U], 3U);
  EXPECT_EQ(view->strides().elements[1U], 1U);
}

TEST(AcceleratorTensorTest, RepresentsValidatedStridedHostStorage)
{
  const shape2_t shape{{2U, 2U}};
  const accelerator::tensor_strides_s<2U> strides{{3U, 1U}};
  const std::array<scalar_t, 5U> values{};
  const std::optional<host_input_t> view = host_input_t::try_create(
    std::span<const scalar_t>{values}, shape, strides);

  ASSERT_TRUE(view.has_value());
  EXPECT_FALSE(view->contiguous());

  const std::array<scalar_t, 4U> undersized{};
  EXPECT_FALSE(
    host_input_t::try_create(
      std::span<const scalar_t>{undersized}, shape, strides).has_value());
}

TEST(AcceleratorTensorTest, RejectsOverflowingShape)
{
  const shape2_t shape{{
    std::numeric_limits<std::size_t>::max(),
    2U}};
  EXPECT_FALSE(shape.try_element_count().has_value());
}

TEST(AcceleratorRuntimeTest, ConstructsOwnedProviderWithBorrowedCudaContext)
{
  extension_provider_state_s state{};
  std::unique_ptr<extension_test_accelerator_c> accelerator;
  try {
    accelerator = std::make_unique<extension_test_accelerator_c>(state);
  } catch (const accelerator::accelerator_error_c & error) {
    GTEST_SKIP() << error.what();
  }

  EXPECT_EQ(state.factory_calls, 1U);
  EXPECT_NE(state.context.stream, nullptr);
  EXPECT_NE(state.context.linear_algebra_handle, nullptr);
  EXPECT_EQ(state.context.device_index, 0U);
  EXPECT_GT(state.context.threads_per_block, 0U);
  EXPECT_GT(state.context.maximum_block_count, 0U);
  EXPECT_GT(state.context.shared_memory_per_block, 0U);

  EXPECT_EQ(accelerator->exercise(), accelerator::status_e::success);
  EXPECT_EQ(state.enqueue_calls, 1U);
}

TEST(AcceleratorRuntimeTest, UploadsClearsAndDownloadsTypedBuffer)
{
  std::unique_ptr<test_accelerator_c> accelerator;
  try {
    accelerator = std::make_unique<test_accelerator_c>();
  } catch (const accelerator::accelerator_error_c & error) {
    GTEST_SKIP() << error.what();
  }

  const shape2_t shape{{2U, 3U}};
  const std::array<scalar_t, 6U> input{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
  std::array<scalar_t, 6U> output{};
  const std::optional<host_input_t> input_view =
    host_input_t::try_create_contiguous(std::span<const scalar_t>{input}, shape);
  const std::optional<host_output_t> output_view =
    host_output_t::try_create_contiguous(std::span<scalar_t>{output}, shape);
  ASSERT_TRUE(input_view.has_value());
  ASSERT_TRUE(output_view.has_value());

  buffer2_t buffer = accelerator->make_buffer<scalar_t>(shape);
  EXPECT_EQ(accelerator->device_index(), 0U);
  EXPECT_EQ(
    accelerator->upload(input_view.value(), buffer),
    accelerator::status_e::success);
  EXPECT_EQ(
    accelerator->download(buffer, output_view.value()),
    accelerator::status_e::success);
  EXPECT_EQ(output, input);

  EXPECT_EQ(accelerator->clear_bytes(buffer), accelerator::status_e::success);
  EXPECT_EQ(
    accelerator->download(buffer, output_view.value()),
    accelerator::status_e::success);
  EXPECT_EQ(output, (std::array<scalar_t, 6U>{}));
}

TEST(AcceleratorRuntimeTest, UploadsIntoContiguousDeviceSubview)
{
  using shape1_t = accelerator::tensor_shape_s<1U>;
  using input1_t = accelerator::host_tensor_view_c<const scalar_t, 1U>;
  using output1_t = accelerator::host_tensor_view_c<scalar_t, 1U>;
  using buffer1_t = accelerator::buffer_c<scalar_t, 1U>;

  std::unique_ptr<test_accelerator_c> accelerator;
  try {
    accelerator = std::make_unique<test_accelerator_c>();
  } catch (const accelerator::accelerator_error_c & error) {
    GTEST_SKIP() << error.what();
  }

  const shape1_t storage_shape{{6U}};
  const shape1_t input_shape{{4U}};
  const std::array<scalar_t, 4U> input{1.0F, 2.0F, 3.0F, 4.0F};
  std::array<scalar_t, 6U> output{};
  const std::optional<input1_t> input_view =
    input1_t::try_create_contiguous(
    std::span<const scalar_t>{input}, input_shape);
  const std::optional<output1_t> output_view =
    output1_t::try_create_contiguous(
    std::span<scalar_t>{output}, storage_shape);
  ASSERT_TRUE(input_view.has_value());
  ASSERT_TRUE(output_view.has_value());

  buffer1_t buffer = accelerator->make_buffer<scalar_t>(storage_shape);
  ASSERT_EQ(
    accelerator->clear_bytes(buffer), accelerator::status_e::success);
  const std::optional<accelerator::device_tensor_view_c<scalar_t, 1U>>
  device_view = buffer.try_view(
    input_shape, accelerator::tensor_strides_s<1U>{{1U}});
  ASSERT_TRUE(device_view.has_value());
  ASSERT_EQ(
    accelerator->upload(*input_view, *device_view),
    accelerator::status_e::success);
  ASSERT_EQ(
    accelerator->download(buffer, *output_view),
    accelerator::status_e::success);

  const std::array<scalar_t, 6U> expected{1.0F, 2.0F, 3.0F, 4.0F, 0.0F, 0.0F};
  EXPECT_EQ(output, expected);
}

TEST(AcceleratorRuntimeTest, RunsIndexedParallelForThenTransformReduce)
{
  using shape1_t = accelerator::tensor_shape_s<1U>;
  using input1_t = accelerator::host_tensor_view_c<const scalar_t, 1U>;
  using output1_t = accelerator::host_tensor_view_c<scalar_t, 1U>;
  using buffer1_t = accelerator::buffer_c<scalar_t, 1U>;

  std::unique_ptr<accelerator_test::cuda_pattern_test_accelerator_c> accelerator;
  try {
    accelerator = std::make_unique<accelerator_test::cuda_pattern_test_accelerator_c>();
  } catch (const accelerator::accelerator_error_c & error) {
    GTEST_SKIP() << error.what();
  }

  const shape1_t input_shape{{4U}};
  const shape1_t workspace_shape{{32U}};
  const shape1_t result_shape{{1U}};
  const std::array<scalar_t, 4U> input_values{1.0F, 2.0F, 3.0F, 4.0F};
  std::array<scalar_t, 1U> result_value{};
  const std::optional<input1_t> input_view = input1_t::try_create_contiguous(
    std::span<const scalar_t>{input_values}, input_shape);
  const std::optional<output1_t> result_view = output1_t::try_create_contiguous(
    std::span<scalar_t>{result_value}, result_shape);
  ASSERT_TRUE(input_view.has_value());
  ASSERT_TRUE(result_view.has_value());

  buffer1_t input = accelerator->make_buffer<scalar_t>(input_shape);
  buffer1_t incremented = accelerator->make_buffer<scalar_t>(input_shape);
  buffer1_t workspace = accelerator->make_buffer<scalar_t>(workspace_shape);
  buffer1_t result = accelerator->make_buffer<scalar_t>(result_shape);
  ASSERT_EQ(
    accelerator->upload(input_view.value(), input),
    accelerator::status_e::success);
  ASSERT_EQ(
    accelerator->increment(input, incremented),
    accelerator::status_e::success);
  ASSERT_EQ(
    accelerator->sum(incremented, workspace, result),
    accelerator::status_e::success);
  ASSERT_EQ(
    accelerator->download(result, result_view.value()),
    accelerator::status_e::success);
  EXPECT_FLOAT_EQ(result_value[0U], 14.0F);
}

TEST(AcceleratorRuntimeTest, RejectsWritableAccessorForReadOnlyOutput)
{
  using shape1_t = accelerator::tensor_shape_s<1U>;
  using buffer1_t = accelerator::buffer_c<scalar_t, 1U>;

  std::unique_ptr<accelerator_test::cuda_pattern_test_accelerator_c> accelerator;
  try {
    accelerator = std::make_unique<accelerator_test::cuda_pattern_test_accelerator_c>();
  } catch (const accelerator::accelerator_error_c & error) {
    GTEST_SKIP() << error.what();
  }

  const shape1_t shape{{1U}};
  buffer1_t input = accelerator->make_buffer<scalar_t>(shape);
  buffer1_t output = accelerator->make_buffer<scalar_t>(shape);
  EXPECT_EQ(
    accelerator->increment_read_only_output(input, std::as_const(output)),
    accelerator::status_e::invalid_argument);
}

TEST(AcceleratorRuntimeTest, RejectsReductionWorkspaceResultAlias)
{
  using shape1_t = accelerator::tensor_shape_s<1U>;
  using buffer1_t = accelerator::buffer_c<scalar_t, 1U>;

  std::unique_ptr<accelerator_test::cuda_pattern_test_accelerator_c> accelerator;
  try {
    accelerator = std::make_unique<accelerator_test::cuda_pattern_test_accelerator_c>();
  } catch (const accelerator::accelerator_error_c & error) {
    GTEST_SKIP() << error.what();
  }

  const shape1_t shape{{1U}};
  buffer1_t input = accelerator->make_buffer<scalar_t>(shape);
  buffer1_t workspace_and_result = accelerator->make_buffer<scalar_t>(shape);
  EXPECT_EQ(
    accelerator->sum(input, workspace_and_result, workspace_and_result),
    accelerator::status_e::overlapping_buffers);
}

TEST(AcceleratorRuntimeTest, MultipliesStridedRankTwoViews)
{
  std::unique_ptr<test_accelerator_c> accelerator;
  try {
    accelerator = std::make_unique<test_accelerator_c>();
  } catch (const accelerator::accelerator_error_c & error) {
    GTEST_SKIP() << error.what();
  }

  using shape1_t = accelerator::tensor_shape_s<1U>;
  using input1_t = accelerator::host_tensor_view_c<const scalar_t, 1U>;
  using output1_t = accelerator::host_tensor_view_c<scalar_t, 1U>;
  using buffer1_t = accelerator::buffer_c<scalar_t, 1U>;

  const shape1_t storage_shape{{6U}};
  const std::array<scalar_t, 6U> left_values{
    1.0F, 2.0F, 100.0F, 3.0F, 4.0F, 100.0F};
  const std::array<scalar_t, 6U> right_values{
    5.0F, 6.0F, 100.0F, 7.0F, 8.0F, 100.0F};
  const std::array<scalar_t, 6U> initial_result{
    -1.0F, -1.0F, -1.0F, -1.0F, -1.0F, -1.0F};
  std::array<scalar_t, 6U> result_values{};

  const std::optional<input1_t> left_host = input1_t::try_create_contiguous(
    std::span<const scalar_t>{left_values}, storage_shape);
  const std::optional<input1_t> right_host = input1_t::try_create_contiguous(
    std::span<const scalar_t>{right_values}, storage_shape);
  const std::optional<input1_t> initial_result_host = input1_t::try_create_contiguous(
    std::span<const scalar_t>{initial_result}, storage_shape);
  const std::optional<output1_t> result_host = output1_t::try_create_contiguous(
    std::span<scalar_t>{result_values}, storage_shape);
  ASSERT_TRUE(left_host.has_value());
  ASSERT_TRUE(right_host.has_value());
  ASSERT_TRUE(initial_result_host.has_value());
  ASSERT_TRUE(result_host.has_value());

  buffer1_t left = accelerator->make_buffer<scalar_t>(storage_shape);
  buffer1_t right = accelerator->make_buffer<scalar_t>(storage_shape);
  buffer1_t result = accelerator->make_buffer<scalar_t>(storage_shape);
  ASSERT_EQ(
    accelerator->upload(left_host.value(), left),
    accelerator::status_e::success);
  ASSERT_EQ(
    accelerator->upload(right_host.value(), right),
    accelerator::status_e::success);
  ASSERT_EQ(
    accelerator->upload(initial_result_host.value(), result),
    accelerator::status_e::success);

  const shape2_t logical_shape{{2U, 2U}};
  const accelerator::tensor_strides_s<2U> logical_strides{{3U, 1U}};
  const std::optional<accelerator::device_tensor_view_c<const scalar_t, 2U>> left_view =
    std::as_const(left).try_view(logical_shape, logical_strides);
  const std::optional<accelerator::device_tensor_view_c<const scalar_t, 2U>> right_view =
    std::as_const(right).try_view(logical_shape, logical_strides);
  const std::optional<accelerator::device_tensor_view_c<scalar_t, 2U>> result_view =
    result.try_view(logical_shape, logical_strides);
  ASSERT_TRUE(left_view.has_value());
  ASSERT_TRUE(right_view.has_value());
  ASSERT_TRUE(result_view.has_value());

  ASSERT_EQ(
    accelerator->elementwise_multiply(
      left_view.value(), right_view.value(), result_view.value()),
    accelerator::status_e::success);
  ASSERT_EQ(
    accelerator->download(result, result_host.value()),
    accelerator::status_e::success);

  const std::array<scalar_t, 6U> expected{
    5.0F, 12.0F, -1.0F, 21.0F, 32.0F, -1.0F};
  EXPECT_EQ(result_values, expected);
}

TEST(AcceleratorRuntimeTest, MultipliesRowMajorFloatMatrices)
{
  std::unique_ptr<test_accelerator_c> accelerator;
  try {
    accelerator = std::make_unique<test_accelerator_c>();
  } catch (const accelerator::accelerator_error_c & error) {
    GTEST_SKIP() << error.what();
  }

  const shape2_t left_shape{{2U, 3U}};
  const shape2_t right_shape{{3U, 2U}};
  const shape2_t result_shape{{2U, 2U}};
  const std::array<scalar_t, 6U> left_values{
    1.0F, 2.0F, 3.0F,
    4.0F, 5.0F, 6.0F};
  const std::array<scalar_t, 6U> right_values{
    7.0F, 8.0F,
    9.0F, 10.0F,
    11.0F, 12.0F};
  std::array<scalar_t, 4U> result_values{};

  const std::optional<host_input_t> left_view =
    host_input_t::try_create_contiguous(
    std::span<const scalar_t>{left_values}, left_shape);
  const std::optional<host_input_t> right_view =
    host_input_t::try_create_contiguous(
    std::span<const scalar_t>{right_values}, right_shape);
  const std::optional<host_output_t> result_view =
    host_output_t::try_create_contiguous(
    std::span<scalar_t>{result_values}, result_shape);
  ASSERT_TRUE(left_view.has_value());
  ASSERT_TRUE(right_view.has_value());
  ASSERT_TRUE(result_view.has_value());

  buffer2_t left = accelerator->make_buffer<scalar_t>(left_shape);
  buffer2_t right = accelerator->make_buffer<scalar_t>(right_shape);
  buffer2_t result = accelerator->make_buffer<scalar_t>(result_shape);
  ASSERT_EQ(
    accelerator->upload(left_view.value(), left),
    accelerator::status_e::success);
  ASSERT_EQ(
    accelerator->upload(right_view.value(), right),
    accelerator::status_e::success);

  ASSERT_EQ(
    accelerator->matrix_multiply(left, right, result),
    accelerator::status_e::success);
  ASSERT_EQ(
    accelerator->download(result, result_view.value()),
    accelerator::status_e::success);

  const std::array<scalar_t, 4U> expected{58.0F, 64.0F, 139.0F, 154.0F};
  EXPECT_EQ(result_values, expected);
}

TEST(AcceleratorRuntimeTest, MultipliesRowMajorDoubleMatrices)
{
  using double_t = common::float64_t;
  using double_input_t = accelerator::host_tensor_view_c<const double_t, 2U>;
  using double_output_t = accelerator::host_tensor_view_c<double_t, 2U>;
  using double_buffer_t = accelerator::buffer_c<double_t, 2U>;

  std::unique_ptr<test_accelerator_c> accelerator;
  try {
    accelerator = std::make_unique<test_accelerator_c>();
  } catch (const accelerator::accelerator_error_c & error) {
    GTEST_SKIP() << error.what();
  }

  const shape2_t left_shape{{1U, 2U}};
  const shape2_t right_shape{{2U, 1U}};
  const shape2_t result_shape{{1U, 1U}};
  const std::array<double_t, 2U> left_values{1.5, 2.0};
  const std::array<double_t, 2U> right_values{2.0, 3.0};
  std::array<double_t, 1U> result_values{};

  const std::optional<double_input_t> left_view =
    double_input_t::try_create_contiguous(
    std::span<const double_t>{left_values}, left_shape);
  const std::optional<double_input_t> right_view =
    double_input_t::try_create_contiguous(
    std::span<const double_t>{right_values}, right_shape);
  const std::optional<double_output_t> result_view =
    double_output_t::try_create_contiguous(
    std::span<double_t>{result_values}, result_shape);
  ASSERT_TRUE(left_view.has_value());
  ASSERT_TRUE(right_view.has_value());
  ASSERT_TRUE(result_view.has_value());

  double_buffer_t left = accelerator->make_buffer<double_t>(left_shape);
  double_buffer_t right = accelerator->make_buffer<double_t>(right_shape);
  double_buffer_t result = accelerator->make_buffer<double_t>(result_shape);
  ASSERT_EQ(
    accelerator->upload(left_view.value(), left),
    accelerator::status_e::success);
  ASSERT_EQ(
    accelerator->upload(right_view.value(), right),
    accelerator::status_e::success);
  ASSERT_EQ(
    accelerator->matrix_multiply(left, right, result),
    accelerator::status_e::success);
  ASSERT_EQ(
    accelerator->download(result, result_view.value()),
    accelerator::status_e::success);

  EXPECT_DOUBLE_EQ(result_values[0U], 9.0);
}
