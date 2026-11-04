// 실시간 오디오 콜백과 분석 워커 스레드 사이의 lock-free SPSC 링 버퍼.
// docs/ALGORITHM.md 1.2절.
//
// 생산자(오디오 스레드)는 할당도 락도 하지 않는다. 생산자는 소비자를 기다리지
// 않으며, 소비자가 뒤처지면 가장 오래된 데이터가 버려진다. 실시간 추적에서는
// 과거 데이터보다 최신 데이터가 중요하므로 이것이 올바른 정책이다.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "nota/span.hpp"

namespace nota {

// 알려진 제약 — 폐기 경로의 데이터 경쟁
//
// 예약 카운터 덕분에 "커밋되는" 읽기는 생산자가 쓰는 영역과 겹치지 않는다.
// 그러나 생산자가 복사 도중 구간을 침범해 StillValid가 거짓이 되는 경우,
// 그 복사 자체는 비원자적 float에 대한 동시 접근이었으므로 표준상 데이터
// 경쟁(UB)이다. 결과는 버려지지만 UB라는 사실은 남는다. ThreadSanitizer는
// 이를 보고한다.
//
// 엄밀한 해법은 버퍼 원소를 relaxed 원자 접근으로 읽고 쓰는 것이다(C++20의
// atomic_ref). 다만 memcpy의 벡터화를 잃으므로 실측 없이 적용하지 않는다.
// 실제 피해가 발생하려면 소비자가 버퍼 한 바퀴만큼 뒤처져야 하는데, 이는
// 5.9초 분량이므로 정상 동작에서는 일어나지 않는다. 일어난다면 그 자체가
// 보고되어야 할 사건이며(frames_dropped), 그때 분석은 이미 재설정된다.
// TODO(core): C++20 전환 시 atomic_ref 적용을 검토한다.
class RingBuffer {
 public:
  struct ReadResult {
    std::size_t frames_read = 0;
    // 지난 read 이후 유실된 프레임 수. 원인을 구분하지 않는다 — 소비자가
    // 뒤처져 덮어쓰였든, 생산자가 한 번에 용량을 초과해 넘겼든, 소비자
    // 입장에서는 오디오 구간을 놓친 것이 동일하기 때문이다. 0이 아니면
    // 시간 연속성이 끊겼으므로 호출자는 STFT 상태와 위치 추정기를 재설정해야
    // 한다. 유실을 알리지 않으면 추정기가 조용히 어긋난다.
    std::uint64_t frames_dropped = 0;
  };

  // capacity_frames는 2의 거듭제곱으로 올림된다. 생성 시점에만 할당한다.
  explicit RingBuffer(std::size_t capacity_frames);

  // --- 생산자 측 (오디오 스레드에서만 호출) ---------------------------------
  // 할당·락·시스템 호출을 하지 않는다. 항상 성공한다.
  void write(Span<const float> src) noexcept;

  // --- 소비자 측 (분석 스레드에서만 호출) -----------------------------------
  // 읽고 읽은 만큼 소비한다.
  ReadResult read(Span<float> dst) noexcept;

  // 소비하지 않고 들여다본다. STFT처럼 윈도우가 겹치는 분석에 필요하다.
  // (윈도우 2048을 peek한 뒤 홉 512만 consume하는 식)
  // offset은 현재 읽기 위치로부터의 프레임 수다.
  // 반환값은 실제로 채운 프레임 수.
  std::size_t peek(std::size_t offset, Span<float> dst) noexcept;

  // 읽기 위치를 n 프레임 전진시킨다. 사용 가능한 양을 넘으면 가능한 만큼만.
  std::size_t consume(std::size_t n) noexcept;

  // --- 상태 조회 -------------------------------------------------------------
  std::size_t available() const noexcept;
  std::size_t capacity() const noexcept { return capacity_; }
  std::uint64_t total_written() const noexcept;
  std::uint64_t total_dropped() const noexcept;

  RingBuffer(const RingBuffer&) = delete;
  RingBuffer& operator=(const RingBuffer&) = delete;

 private:
  // 읽기 위치를 안전 범위로 끌어올린다. read/peek/consume이 공통으로 쓴다.
  // reserved_pos는 생산자가 "쓰겠다고 예약한" 위치다. write_pos가 아니라
  // 이것을 기준으로 삼아야 하는 이유는 reserved_pos_ 선언부에 있다.
  void Resync(std::uint64_t reserved_pos, std::uint64_t& read_pos) noexcept;

  // 복사한 구간 [read_start, read_start + count) 이 아직 오염되지 않았는지
  // 확인한다. 생산자는 소비자를 기다리지 않으므로 memcpy 도중에 그 영역을
  // 덮어쓸 수 있다. 검사하지 않으면 찢어진 PCM이 분석에 들어가 온셋을
  // 오검출한다.
  bool StillValid(std::uint64_t read_start) const noexcept;

  // 마지막 보고 이후 누적된 유실 프레임 수를 반환하고 기준점을 갱신한다.
  std::uint64_t TakeDropDelta() noexcept;

  // from 위치에서 count 프레임을 복사한다. 랩어라운드를 처리한다.
  void CopyOut(std::uint64_t from, std::size_t count, float* dst) const noexcept;

  std::vector<float> buf_;
  std::size_t capacity_ = 0;  // 2의 거듭제곱
  std::size_t mask_ = 0;      // capacity_ - 1

  // 64비트 단조 증가 카운터. 래핑 모호성이 없다.

  // 생산자가 쓰기를 "시작한" 위치. memcpy 전에 공개한다.
  //
  // write_pos_만으로는 부족하다. 생산자는 데이터를 memcpy한 뒤에 write_pos_를
  // 올리므로, write_pos_가 w일 때 생산자는 이미 [w, w+n) 구간의 인덱스에
  // 데이터를 쓰고 있을 수 있다. 그 인덱스는 논리 위치 [w-capacity, w+n-capacity)
  // 와 같은 자리다. 즉 소비자가 w-capacity 부근을 읽으면 생산자가 지금 덮어쓰는
  // 중인 메모리를 읽는다. write_pos_는 아직 올라가지 않았으므로 어떤 사후
  // 검증으로도 이를 알아낼 수 없다.
  //
  // 따라서 덮어쓰기 경계는 reserved_pos_로 판단하고, 읽을 수 있는 데이터의
  // 상한은 write_pos_로 판단한다. 두 카운터의 차이는 진행 중인 쓰기 한 번의
  // 크기이므로(콜백 512 vs 버퍼 262144) 실질 용량 손실은 무시할 수 있다.
  std::atomic<std::uint64_t> reserved_pos_{0};
  // 생산자가 쓰기를 "완료하고 공개한" 위치. 이 아래의 데이터는 읽어도 된다.
  std::atomic<std::uint64_t> write_pos_{0};
  std::atomic<std::uint64_t> read_pos_{0};

  // 생산자가 한 번의 write로 capacity를 초과해 넘긴 프레임 수.
  std::atomic<std::uint64_t> producer_dropped_{0};
  // 소비자가 뒤처져 버려진 프레임 수.
  std::atomic<std::uint64_t> consumer_dropped_{0};
  // 이미 호출자에게 보고한 유실 누적량. 소비자 스레드 전용이므로 원자성이
  // 필요하지 않다.
  std::uint64_t reported_dropped_ = 0;
};

}  // namespace nota
