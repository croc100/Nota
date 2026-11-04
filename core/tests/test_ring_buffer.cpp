#include <cstddef>
#include <cstdint>
#include <numeric>
#include <thread>
#include <vector>

#include "nota/ring_buffer.hpp"
#include "test_framework.hpp"

namespace {

std::vector<float> Ramp(std::size_t n, float start = 0.0f) {
  std::vector<float> v(n);
  for (std::size_t i = 0; i < n; ++i) v[i] = start + static_cast<float>(i);
  return v;
}

nota::Span<const float> AsSpan(const std::vector<float>& v) {
  return nota::Span<const float>(v.data(), v.size());
}

}  // namespace

NOTA_TEST(CapacityRoundsUpToPowerOfTwo) {
  CHECK_EQ(nota::RingBuffer(1000).capacity(), static_cast<std::size_t>(1024));
  CHECK_EQ(nota::RingBuffer(1024).capacity(), static_cast<std::size_t>(1024));
  CHECK_EQ(nota::RingBuffer(1025).capacity(), static_cast<std::size_t>(2048));
}

NOTA_TEST(WriteThenReadRoundTrip) {
  nota::RingBuffer rb(1024);
  const std::vector<float> in = Ramp(100);
  rb.write(AsSpan(in));

  CHECK_EQ(rb.available(), static_cast<std::size_t>(100));

  std::vector<float> out(100, -1.0f);
  const auto r = rb.read(nota::Span<float>(out.data(), out.size()));
  CHECK_EQ(r.frames_read, static_cast<std::size_t>(100));
  CHECK_EQ(r.frames_dropped, static_cast<std::uint64_t>(0));
  CHECK(out == in);
  CHECK_EQ(rb.available(), static_cast<std::size_t>(0));
}

NOTA_TEST(ReadReturnsOnlyWhatIsAvailable) {
  nota::RingBuffer rb(1024);
  const std::vector<float> in = Ramp(10);
  rb.write(AsSpan(in));

  std::vector<float> out(50, -1.0f);
  const auto r = rb.read(nota::Span<float>(out.data(), out.size()));
  CHECK_EQ(r.frames_read, static_cast<std::size_t>(10));
  for (std::size_t i = 0; i < 10; ++i) CHECK_EQ(out[i], in[i]);
  CHECK_EQ(out[10], -1.0f);  // 나머지는 건드리지 않는다
}

NOTA_TEST(WrapsAroundCorrectly) {
  // 용량을 여러 바퀴 돌려도 데이터가 정확해야 한다.
  nota::RingBuffer rb(64);
  float next_expected = 0.0f;
  float next_write = 0.0f;

  for (int iter = 0; iter < 20; ++iter) {
    const std::vector<float> in = Ramp(37, next_write);
    next_write += 37.0f;
    rb.write(AsSpan(in));

    std::vector<float> out(37, -1.0f);
    const auto r = rb.read(nota::Span<float>(out.data(), out.size()));
    CHECK_EQ(r.frames_read, static_cast<std::size_t>(37));
    CHECK_EQ(r.frames_dropped, static_cast<std::uint64_t>(0));
    for (std::size_t i = 0; i < 37; ++i) {
      CHECK_EQ(out[i], next_expected + static_cast<float>(i));
    }
    next_expected += 37.0f;
  }
}

NOTA_TEST(OverrunDropsOldestAndReportsIt) {
  // 소비자가 뒤처지면 가장 오래된 데이터가 버려지고, 최신 데이터가 남아야 한다.
  nota::RingBuffer rb(64);
  const std::vector<float> in = Ramp(200);  // 용량 64의 3배 이상
  rb.write(AsSpan(in));

  CHECK_EQ(rb.available(), static_cast<std::size_t>(64));

  std::vector<float> out(64, -1.0f);
  const auto r = rb.read(nota::Span<float>(out.data(), out.size()));
  CHECK_EQ(r.frames_read, static_cast<std::size_t>(64));
  CHECK_EQ(r.frames_dropped, static_cast<std::uint64_t>(200 - 64));

  // 남은 것은 마지막 64 프레임: 136..199
  for (std::size_t i = 0; i < 64; ++i) {
    CHECK_EQ(out[i], static_cast<float>(136 + i));
  }
  CHECK_EQ(rb.total_dropped(), static_cast<std::uint64_t>(136));
}

NOTA_TEST(ConsumerLagDropsAreReported) {
  // 각 write는 용량 이내지만 누적이 용량을 넘는다 = 소비자가 뒤처진 경우.
  // 생산자 측 유실(한 번에 용량 초과)과 구분되는 경로다.
  nota::RingBuffer rb(64);
  for (int i = 0; i < 4; ++i) {
    rb.write(AsSpan(Ramp(64, static_cast<float>(i * 64))));
  }

  std::vector<float> out(64, -1.0f);
  const auto r = rb.read(nota::Span<float>(out.data(), out.size()));
  CHECK_EQ(r.frames_read, static_cast<std::size_t>(64));
  CHECK_EQ(r.frames_dropped, static_cast<std::uint64_t>(256 - 64));
  // 남은 것은 마지막 64 프레임: 192..255
  for (std::size_t i = 0; i < 64; ++i) {
    CHECK_EQ(out[i], static_cast<float>(192 + i));
  }
}

NOTA_TEST(DropIsReportedExactlyOnce) {
  // 유실은 한 번만 보고되어야 한다. 매 read마다 재보고하면 호출자가 추정기를
  // 불필요하게 계속 리셋한다.
  nota::RingBuffer rb(64);
  rb.write(AsSpan(Ramp(200)));

  std::vector<float> out(32, -1.0f);
  const auto first = rb.read(nota::Span<float>(out.data(), out.size()));
  CHECK_EQ(first.frames_dropped, static_cast<std::uint64_t>(136));

  const auto second = rb.read(nota::Span<float>(out.data(), out.size()));
  CHECK_EQ(second.frames_read, static_cast<std::size_t>(32));
  CHECK_EQ(second.frames_dropped, static_cast<std::uint64_t>(0));
}

NOTA_TEST(SingleWriteLargerThanCapacityKeepsNewest) {
  nota::RingBuffer rb(32);
  const std::vector<float> in = Ramp(100);
  rb.write(AsSpan(in));

  std::vector<float> out(32, -1.0f);
  const auto r = rb.read(nota::Span<float>(out.data(), out.size()));
  CHECK_EQ(r.frames_read, static_cast<std::size_t>(32));
  // 마지막 32 프레임: 68..99
  for (std::size_t i = 0; i < 32; ++i) {
    CHECK_EQ(out[i], static_cast<float>(68 + i));
  }
  CHECK(rb.total_dropped() > 0);
}

NOTA_TEST(PeekDoesNotConsume) {
  // STFT는 윈도우가 겹치므로 peek/consume 분리가 필요하다.
  nota::RingBuffer rb(1024);
  const std::vector<float> in = Ramp(100);
  rb.write(AsSpan(in));

  std::vector<float> first(50, -1.0f);
  CHECK_EQ(rb.peek(0, nota::Span<float>(first.data(), first.size())),
           static_cast<std::size_t>(50));
  CHECK_EQ(rb.available(), static_cast<std::size_t>(100));  // 변하지 않았다

  std::vector<float> again(50, -1.0f);
  CHECK_EQ(rb.peek(0, nota::Span<float>(again.data(), again.size())),
           static_cast<std::size_t>(50));
  CHECK(first == again);
  for (std::size_t i = 0; i < 50; ++i) CHECK_EQ(first[i], static_cast<float>(i));
}

NOTA_TEST(PeekHonoursOffset) {
  nota::RingBuffer rb(1024);
  rb.write(AsSpan(Ramp(100)));

  std::vector<float> out(10, -1.0f);
  CHECK_EQ(rb.peek(30, nota::Span<float>(out.data(), out.size())),
           static_cast<std::size_t>(10));
  for (std::size_t i = 0; i < 10; ++i) {
    CHECK_EQ(out[i], static_cast<float>(30 + i));
  }

  // 오프셋이 사용 가능 범위를 넘으면 0
  CHECK_EQ(rb.peek(100, nota::Span<float>(out.data(), out.size())),
           static_cast<std::size_t>(0));
  CHECK_EQ(rb.peek(95, nota::Span<float>(out.data(), out.size())),
           static_cast<std::size_t>(5));
}

NOTA_TEST(ConsumeAdvancesReadPosition) {
  nota::RingBuffer rb(1024);
  rb.write(AsSpan(Ramp(100)));

  CHECK_EQ(rb.consume(40), static_cast<std::size_t>(40));
  CHECK_EQ(rb.available(), static_cast<std::size_t>(60));

  std::vector<float> out(10, -1.0f);
  CHECK_EQ(rb.peek(0, nota::Span<float>(out.data(), out.size())),
           static_cast<std::size_t>(10));
  for (std::size_t i = 0; i < 10; ++i) {
    CHECK_EQ(out[i], static_cast<float>(40 + i));
  }

  // 사용 가능한 양보다 많이 소비 요청하면 가능한 만큼만
  CHECK_EQ(rb.consume(1000), static_cast<std::size_t>(60));
  CHECK_EQ(rb.available(), static_cast<std::size_t>(0));
}

NOTA_TEST(StftStylePeekConsumeLoop) {
  // 실제 사용 패턴: 윈도우 2048을 peek하고 홉 512만 consume한다.
  constexpr std::size_t kWindow = 2048;
  constexpr std::size_t kHop = 512;

  nota::RingBuffer rb(1u << 16);
  rb.write(AsSpan(Ramp(kWindow + kHop * 10)));

  std::vector<float> window(kWindow);
  for (std::size_t frame = 0; frame < 10; ++frame) {
    const std::size_t got =
        rb.peek(0, nota::Span<float>(window.data(), window.size()));
    CHECK_EQ(got, kWindow);
    // 윈도우 시작 샘플은 frame * hop 이어야 한다
    CHECK_EQ(window[0], static_cast<float>(frame * kHop));
    CHECK_EQ(rb.consume(kHop), kHop);
  }
}

NOTA_TEST(EmptyWriteIsNoOp) {
  nota::RingBuffer rb(64);
  rb.write(nota::Span<const float>(nullptr, 0));
  CHECK_EQ(rb.available(), static_cast<std::size_t>(0));
  CHECK_EQ(rb.total_written(), static_cast<std::uint64_t>(0));
}

NOTA_TEST(ConcurrentProducerConsumerPreservesOrder) {
  // 생산자/소비자를 별 스레드로 돌려 순서가 깨지지 않는지 본다.
  // 데이터 레이스는 sanitizer 빌드(-DNOTA_SANITIZE=ON)에서 검출한다.
  constexpr std::size_t kTotal = 200000;
  nota::RingBuffer rb(1u << 14);

  std::thread producer([&] {
    std::vector<float> chunk(512);
    std::size_t written = 0;
    while (written < kTotal) {
      const std::size_t n = std::min(chunk.size(), kTotal - written);
      for (std::size_t i = 0; i < n; ++i) {
        chunk[i] = static_cast<float>(written + i);
      }
      rb.write(nota::Span<const float>(chunk.data(), n));
      written += n;
      std::this_thread::yield();
    }
  });

  std::uint64_t dropped_total = 0;
  float last_value = -1.0f;
  bool monotonic = true;
  std::size_t received = 0;

  std::thread consumer([&] {
    std::vector<float> out(1024);
    while (received + static_cast<std::size_t>(dropped_total) < kTotal) {
      const auto r = rb.read(nota::Span<float>(out.data(), out.size()));
      dropped_total += r.frames_dropped;
      for (std::size_t i = 0; i < r.frames_read; ++i) {
        if (out[i] <= last_value) monotonic = false;
        last_value = out[i];
      }
      received += r.frames_read;
      if (r.frames_read == 0) std::this_thread::yield();
    }
  });

  producer.join();
  consumer.join();

  // 값은 항상 증가해야 한다. 드롭이 일어나도 순서는 보존된다.
  CHECK(monotonic);
  CHECK_EQ(received + static_cast<std::size_t>(dropped_total), kTotal);
}
