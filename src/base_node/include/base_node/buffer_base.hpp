#pragma once

#include <cstddef>
#include <optional>

namespace base_node
{
namespace topic
{

template<typename T>
class buffer_base_c
{
public:
  using size_type = std::size_t;

  virtual ~buffer_base_c() = default;

  virtual void clear() noexcept = 0;
  virtual bool empty() const noexcept = 0;
  virtual size_type size() const noexcept = 0;
  virtual size_type capacity() const noexcept = 0;
  virtual bool push_back(const T & value) = 0;
  virtual std::optional<T> pop_front() = 0;
};

}  // namespace topic
}  // namespace base_node
