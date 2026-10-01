#include "vector_pipeline.hpp"

#include <array>
#include <exception>
#include <iostream>
#include <optional>
#include <utility>

namespace accelerator_examples::vector_pipeline
{

vector_pipeline_c::~vector_pipeline_c() noexcept = default;

accelerator::status_e vector_pipeline_c::process(
  const std::span<const scalar_t> input,
  const std::span<scalar_t> scaled,
  scalar_t & sum) noexcept
{
  using host_input_t =
    accelerator::host_tensor_view_c<const scalar_t, 1U>;
  using host_output_t = accelerator::host_tensor_view_c<scalar_t, 1U>;

  const shape_t input_shape{{input.size()}};
  const shape_t output_shape{{scaled.size()}};
  const shape_t result_shape{{1U}};
  const std::optional<host_input_t> input_view =
    host_input_t::try_create_contiguous(input, input_shape);
  const std::optional<host_output_t> output_view =
    host_output_t::try_create_contiguous(scaled, output_shape);
  const std::optional<host_output_t> result_view =
    host_output_t::try_create_contiguous(
      std::span<scalar_t>{&sum, 1U}, result_shape);
  if (!input_view.has_value() || !output_view.has_value() ||
    !result_view.has_value() || input_shape != m_input.shape() ||
    output_shape != m_scaled.shape())
  {
    return accelerator::status_e::shape_mismatch;
  }

  accelerator::status_e status = upload(*input_view, m_input);
  if (status != accelerator::status_e::success) {
    return status;
  }
  status = m_scale.execute(std::as_const(m_input), m_scaled);
  if (status != accelerator::status_e::success) {
    return status;
  }
  status = m_sum.execute(
    std::as_const(m_scaled), m_workspace, m_result);
  if (status != accelerator::status_e::success) {
    return status;
  }
  status = download(m_scaled, *output_view);
  if (status != accelerator::status_e::success) {
    return status;
  }
  return download(m_result, *result_view);
}

}  // namespace accelerator_examples::vector_pipeline

int main()
{
  using accelerator_examples::vector_pipeline::scalar_t;
  using accelerator_examples::vector_pipeline::vector_pipeline_c;

  constexpr std::size_t ELEMENT_COUNT = 4U;
  constexpr scalar_t SCALE = 2.0F;
  const std::array<scalar_t, ELEMENT_COUNT> input{1.0F, 2.0F, 3.0F, 4.0F};
  std::array<scalar_t, ELEMENT_COUNT> scaled{};
  scalar_t sum = 0.0F;

  try {
    vector_pipeline_c pipeline(input.size(), SCALE);
    const accelerator::status_e status = pipeline.process(input, scaled, sum);
    if (status != accelerator::status_e::success) {
      std::cerr << "Vector pipeline failed with status "
                << static_cast<common::uint32_t>(status) << '\n';
      return 1;
    }
  } catch (const std::exception & error) {
    std::cerr << "Vector pipeline initialization failed: "
              << error.what() << '\n';
    return 1;
  }

  std::cout << "Scaled values:";
  for (const scalar_t value : scaled) {
    std::cout << ' ' << value;
  }
  std::cout << "\nSum: " << sum << '\n';
  return 0;
}
