#pragma once
#include <optional>
#include <cstdint>

/*
  DHT22 reader using pigpiod daemon (pigpiod_if2).
  - Works even when pigpiod is already running (no /var/run/pigpio.pid lock conflict).
  - Timing is captured on the daemon side and delivered with microsecond ticks.
  - Read interval: DHT22 requires >= 2s between successful reads.
*/

struct Dht22Reading {
  double temp_c;
  double hum_rh;
};

class Dht22Pigpio {
 public:
  explicit Dht22Pigpio(int gpio, const char* host = "localhost", const char* port = "8888");
  ~Dht22Pigpio();

  Dht22Pigpio(const Dht22Pigpio&) = delete;
  Dht22Pigpio& operator=(const Dht22Pigpio&) = delete;

  // Single attempt (non-blocking relative to your main loop; may wait up to ~250ms for edges).
  std::optional<Dht22Reading> read_once(int timeout_ms = 250);

  // Convenience: retry a few times (this may block, so call it in a background thread).
  std::optional<Dht22Reading> read_retry(int tries = 3, int delay_ms = 200, int timeout_ms = 250);

  bool ok() const { return pi_ >= 0; }

 private:
  int gpio_ = -1;
  int pi_   = -1;
  uint32_t last_read_tick_ = 0; // microsecond tick (daemon)

  std::optional<Dht22Reading> parse_edges_(const uint32_t* ticks, const int* levels, int n) const;
};
