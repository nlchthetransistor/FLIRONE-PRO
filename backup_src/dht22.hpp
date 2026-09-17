#pragma once
#include <optional>

struct DhtReading {
    double temp_c;
    double hum_rh;
};

class Dht22Pigpio {
public:
    explicit Dht22Pigpio(int bcm_gpio);   // ví dụ GPIO4 => 4
    ~Dht22Pigpio();

    std::optional<DhtReading> read_once(int timeout_ms = 2000);
    std::optional<DhtReading> read_retry(int tries = 5, int delay_ms = 800);

private:
    int gpio_;
    bool pigpio_inited_;
};

