// std::span은 C++20부터다. 코어는 C++17이므로 필요한 최소 기능만 정의한다.
// C++20으로 올릴 때 이 파일을 제거하고 std::span으로 치환한다.
#pragma once

#include <cstddef>
#include <type_traits>

namespace nota {

template <typename T>
class Span {
 public:
  Span() = default;
  Span(T* data, std::size_t size) noexcept : data_(data), size_(size) {}

  // 비const → const 변환 허용 (Span<float> → Span<const float>)
  template <typename U,
            typename = std::enable_if_t<std::is_same_v<const U, T>>>
  Span(Span<U> other) noexcept : data_(other.data()), size_(other.size()) {}

  T* data() const noexcept { return data_; }
  std::size_t size() const noexcept { return size_; }
  bool empty() const noexcept { return size_ == 0; }

  T& operator[](std::size_t i) const noexcept { return data_[i]; }

  T* begin() const noexcept { return data_; }
  T* end() const noexcept { return data_ + size_; }

  Span subspan(std::size_t offset, std::size_t count) const noexcept {
    return Span(data_ + offset, count);
  }

 private:
  T* data_ = nullptr;
  std::size_t size_ = 0;
};

}  // namespace nota
