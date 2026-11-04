#include "nota/ring_buffer.hpp"

#include <algorithm>
#include <cstring>

namespace nota {
namespace {

std::size_t RoundUpPow2(std::size_t v) {
  if (v < 2) return 2;
  --v;
  for (std::size_t shift = 1; shift < sizeof(std::size_t) * 8; shift <<= 1) {
    v |= v >> shift;
  }
  return v + 1;
}

}  // namespace

RingBuffer::RingBuffer(std::size_t capacity_frames)
    : capacity_(RoundUpPow2(capacity_frames)) {
  mask_ = capacity_ - 1;
  buf_.assign(capacity_, 0.0f);
}

// --- 생산자 측 --------------------------------------------------------------

void RingBuffer::write(Span<const float> src) noexcept {
  std::size_t count = src.size();
  if (count == 0) return;

  // 한 번의 write가 버퍼 전체보다 크면 뒤쪽 capacity 프레임만 의미가 있다.
  // 정상 동작에서는 발생하지 않는다 (콜백 512 vs 버퍼 262144).
  std::size_t src_offset = 0;
  if (count > capacity_) {
    src_offset = count - capacity_;
    producer_dropped_.fetch_add(static_cast<std::uint64_t>(src_offset),
                                std::memory_order_relaxed);
    count = capacity_;
  }

  const std::uint64_t w = write_pos_.load(std::memory_order_relaxed);

  // 덮어쓰기 범위를 먼저 공개한다. 소비자는 이 값을 보고 안전 경계를 잡는다.
  reserved_pos_.store(w + count, std::memory_order_release);

  const std::size_t start = static_cast<std::size_t>(w & mask_);
  const std::size_t first = std::min(count, capacity_ - start);

  std::memcpy(buf_.data() + start, src.data() + src_offset,
              first * sizeof(float));
  if (count > first) {
    std::memcpy(buf_.data(), src.data() + src_offset + first,
                (count - first) * sizeof(float));
  }

  // release: 위의 데이터 쓰기가 인덱스 공개보다 먼저 보이도록 한다.
  write_pos_.store(w + count, std::memory_order_release);
}

// --- 소비자 측 내부 보조 ----------------------------------------------------

void RingBuffer::Resync(std::uint64_t reserved_pos,
                        std::uint64_t& read_pos) noexcept {
  const std::uint64_t cap = static_cast<std::uint64_t>(capacity_);
  // 생산자가 덮어쓰기를 예약한 구간 아래는 신뢰할 수 없다.
  const std::uint64_t oldest_safe = reserved_pos > cap ? reserved_pos - cap : 0;
  if (read_pos >= oldest_safe) return;
  consumer_dropped_.fetch_add(oldest_safe - read_pos, std::memory_order_relaxed);
  read_pos = oldest_safe;
  read_pos_.store(read_pos, std::memory_order_release);
}

bool RingBuffer::StillValid(std::uint64_t read_start) const noexcept {
  const std::uint64_t reserved = reserved_pos_.load(std::memory_order_acquire);
  return reserved - read_start <= static_cast<std::uint64_t>(capacity_);
}

std::uint64_t RingBuffer::TakeDropDelta() noexcept {
  const std::uint64_t total =
      producer_dropped_.load(std::memory_order_relaxed) +
      consumer_dropped_.load(std::memory_order_relaxed);
  const std::uint64_t delta = total - reported_dropped_;
  reported_dropped_ = total;
  return delta;
}

void RingBuffer::CopyOut(std::uint64_t from, std::size_t count,
                         float* dst) const noexcept {
  const std::size_t start = static_cast<std::size_t>(from & mask_);
  const std::size_t first = std::min(count, capacity_ - start);
  std::memcpy(dst, buf_.data() + start, first * sizeof(float));
  if (count > first) {
    std::memcpy(dst + first, buf_.data(), (count - first) * sizeof(float));
  }
}

// --- 소비자 측 공개 API -----------------------------------------------------

RingBuffer::ReadResult RingBuffer::read(Span<float> dst) noexcept {
  ReadResult result;

  // 찢어짐이 감지되면 한 번 재시도한다. 두 번 연속 실패하면 생산자가 압도적으로
  // 빠른 상황이므로 이번 호출은 빈손으로 돌아가고 유실량만 보고한다. 호출자가
  // 다시 부르면 그때는 진전이 있다. 빈손 반환이 찢어진 데이터보다 낫다.
  for (int attempt = 0; attempt < 2; ++attempt) {
    // w를 먼저, reserved를 나중에 읽는다. reserved >= w가 항상 성립하므로
    // 나중에 읽은 reserved는 더 보수적인(더 큰) 경계를 준다.
    const std::uint64_t w = write_pos_.load(std::memory_order_acquire);
    const std::uint64_t reserved = reserved_pos_.load(std::memory_order_acquire);
    std::uint64_t r = read_pos_.load(std::memory_order_relaxed);
    Resync(reserved, r);

    // 진행 중인 쓰기가 버퍼 전체를 덮는 극단적 상황에서는 안전 경계가 공개된
    // 데이터보다 앞설 수 있다. 읽을 것이 없다.
    if (r >= w) break;

    const std::uint64_t avail = w - r;
    const std::size_t count =
        static_cast<std::size_t>(std::min<std::uint64_t>(dst.size(), avail));
    if (count == 0) break;

    CopyOut(r, count, dst.data());

    if (StillValid(r)) {
      read_pos_.store(r + count, std::memory_order_release);
      result.frames_read = count;
      break;
    }
    // 복사 중 생산자가 이 구간을 덮어썼다. 복사본을 버린다. read_pos_는
    // 전진시키지 않고, 다음 시도의 Resync가 유효 범위로 끌어올리며 유실을 계상한다.
  }

  result.frames_dropped = TakeDropDelta();
  return result;
}

std::size_t RingBuffer::peek(std::size_t offset, Span<float> dst) noexcept {
  for (int attempt = 0; attempt < 2; ++attempt) {
    const std::uint64_t w = write_pos_.load(std::memory_order_acquire);
    const std::uint64_t reserved = reserved_pos_.load(std::memory_order_acquire);
    std::uint64_t r = read_pos_.load(std::memory_order_relaxed);
    Resync(reserved, r);

    if (r >= w) return 0;
    const std::uint64_t avail = w - r;
    if (static_cast<std::uint64_t>(offset) >= avail) return 0;

    const std::uint64_t avail_at_offset =
        avail - static_cast<std::uint64_t>(offset);
    const std::size_t count = static_cast<std::size_t>(
        std::min<std::uint64_t>(dst.size(), avail_at_offset));
    if (count == 0) return 0;

    CopyOut(r + static_cast<std::uint64_t>(offset), count, dst.data());

    if (StillValid(r)) return count;
    // 찢어짐. 재시도.
  }
  return 0;
}

std::size_t RingBuffer::consume(std::size_t n) noexcept {
  const std::uint64_t w = write_pos_.load(std::memory_order_acquire);
  const std::uint64_t reserved = reserved_pos_.load(std::memory_order_acquire);
  std::uint64_t r = read_pos_.load(std::memory_order_relaxed);
  Resync(reserved, r);

  if (r >= w) return 0;
  const std::uint64_t avail = w - r;
  const std::size_t count =
      static_cast<std::size_t>(std::min<std::uint64_t>(n, avail));
  read_pos_.store(r + count, std::memory_order_release);
  return count;
}

std::size_t RingBuffer::available() const noexcept {
  const std::uint64_t w = write_pos_.load(std::memory_order_acquire);
  const std::uint64_t r = read_pos_.load(std::memory_order_relaxed);
  const std::uint64_t avail = w - r;
  return static_cast<std::size_t>(
      std::min<std::uint64_t>(avail, static_cast<std::uint64_t>(capacity_)));
}

std::uint64_t RingBuffer::total_written() const noexcept {
  return write_pos_.load(std::memory_order_relaxed);
}

std::uint64_t RingBuffer::total_dropped() const noexcept {
  return producer_dropped_.load(std::memory_order_relaxed) +
         consumer_dropped_.load(std::memory_order_relaxed);
}

}  // namespace nota
