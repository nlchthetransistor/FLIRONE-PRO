#include "dht22.hpp"

#include <pigpiod_if2.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>

namespace {

// DHT protocol thresholds (microseconds)
constexpr int kBit1HighUs = 50;       // >50us = 1, else 0
constexpr int kEdgesMax   = 96;       // enough for full frame
constexpr int kMinEdges   = 70;       // sanity lower bound

struct EdgeBuf {
  std::mutex m;
  std::condition_variable cv;
  std::array<uint32_t, kEdgesMax> ticks{};
  std::array<int,      kEdgesMax> levels{};
  int n = 0;
  bool done = false;
};

void edge_cb(int /*pi*/, unsigned /*gpio*/, unsigned level, uint32_t tick, void* user) {
  auto* b = static_cast<EdgeBuf*>(user);
  if (level > 1) return; // ignore watchdog (level==2)
  std::lock_guard<std::mutex> lk(b->m);
  if (b->done) return;
  if (b->n < (int)b->ticks.size()) {
    b->ticks[b->n]  = tick;
    b->levels[b->n] = (int)level;
    b->n++;
    if (b->n >= 84) { // usually enough edges for 40 bits
      b->done = true;
      b->cv.notify_one();
    }
  } else {
    b->done = true;
    b->cv.notify_one();
  }
}

static inline uint32_t tick_diff(uint32_t newer, uint32_t older) {
  // pigpio ticks wrap around ~72 minutes (2^32 us). unsigned subtraction handles wrap.
  return newer - older;
}

} // namespace

Dht22Pigpio::Dht22Pigpio(int gpio, const char* host, const char* port)
: gpio_(gpio) {
  pi_ = pigpio_start(host, port); // connects to pigpiod (daemon)
  if (pi_ < 0) return;

  // idle state: input + pull-up
  set_mode(pi_, gpio_, PI_INPUT);
  set_pull_up_down(pi_, gpio_, PI_PUD_UP);
}

Dht22Pigpio::~Dht22Pigpio() {
  if (pi_ >= 0) {
    pigpio_stop(pi_);
    pi_ = -1;
  }
}

std::optional<Dht22Reading> Dht22Pigpio::read_retry(int tries, int delay_ms, int timeout_ms) {
  for (int i = 0; i < tries; ++i) {
    if (auto r = read_once(timeout_ms)) return r;
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
  }
  return std::nullopt;
}

std::optional<Dht22Reading> Dht22Pigpio::read_once(int timeout_ms) {
  if (pi_ < 0) return std::nullopt;

  // enforce >=2s between successful reads (DHT22 spec)
  // use pigpio's tick for monotonic microsecond time
  uint32_t now_tick = get_current_tick(pi_);
  if (last_read_tick_ != 0 && tick_diff(now_tick, last_read_tick_) < 2000000) {
    return std::nullopt;
  }

  EdgeBuf buf;

  // Register callback (edges are timestamped on daemon side)
  int cb_id = callback_ex(pi_, gpio_, EITHER_EDGE, edge_cb, &buf);
  if (cb_id < 0) return std::nullopt;

  // --- start signal: pull low for >= 18ms, then release ---
  set_mode(pi_, gpio_, PI_OUTPUT);
  gpio_write(pi_, gpio_, 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(18));
  set_mode(pi_, gpio_, PI_INPUT);
  set_pull_up_down(pi_, gpio_, PI_PUD_UP);

  // Wait for edges or timeout
  {
    std::unique_lock<std::mutex> lk(buf.m);
    buf.cv.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&]{ return buf.done; });
  }

  callback_cancel(cb_id);

  if (buf.n < kMinEdges) return std::nullopt;

  auto reading = parse_edges_(buf.ticks.data(), buf.levels.data(), buf.n);
  if (reading) {
    last_read_tick_ = get_current_tick(pi_);
  }
  return reading;
}

std::optional<Dht22Reading> Dht22Pigpio::parse_edges_(const uint32_t* ticks, const int* levels, int n) const {
  // Build list of HIGH pulse widths (rise -> fall)
  std::array<uint32_t, 48> high_us{};
  int high_n = 0;

  for (int i = 0; i + 1 < n; ++i) {
    if (levels[i] == 1 && levels[i + 1] == 0) {
      high_us[high_n++] = tick_diff(ticks[i + 1], ticks[i]);
      if (high_n >= (int)high_us.size()) break;
    }
  }

  // Expect: first high pulse ~80us (handshake), followed by 40 data highs
  if (high_n < 41) return std::nullopt;

  // Skip handshake high (index 0), take next 40
  uint8_t bytes[5] = {0,0,0,0,0};

  for (int bit = 0; bit < 40; ++bit) {
    const uint32_t w = high_us[bit + 1];
    const int b = (w > (uint32_t)kBit1HighUs) ? 1 : 0;
    bytes[bit / 8] <<= 1;
    bytes[bit / 8] |= (uint8_t)b;
  }

  const uint8_t chk = (uint8_t)((bytes[0] + bytes[1] + bytes[2] + bytes[3]) & 0xFF);
  if (chk != bytes[4]) return std::nullopt;

  const int hum10 = (bytes[0] << 8) | bytes[1];
  int temp10 = (bytes[2] << 8) | bytes[3];
  bool neg = false;
  if (temp10 & 0x8000) {
    neg = true;
    temp10 &= 0x7FFF;
  }

  double hum = hum10 / 10.0;
  double temp = temp10 / 10.0;
  if (neg) temp = -temp;

  // sanity
  if (!(temp > -40.0 && temp < 80.0)) return std::nullopt;
  if (!(hum >= 0.0 && hum <= 100.0)) return std::nullopt;

  return Dht22Reading{temp, hum};
}
