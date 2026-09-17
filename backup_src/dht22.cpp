#include "dht22.hpp"
#include <pigpio.h>
#include <vector>
#include <thread>
#include <chrono>

Dht22Pigpio::Dht22Pigpio(int bcm_gpio)
: gpio_(bcm_gpio), pigpio_inited_(false)
{
    // pigpio daemon đang chạy => có thể dùng pigpio_init kiểu socket.
    // Nhưng pigpio C API đơn giản nhất là gpioInitialise().
    // Nếu bạn chạy pigpiod, vẫn dùng được gpioInitialise() (pigpio will start local).
    if (gpioInitialise() >= 0) {
        pigpio_inited_ = true;
    }
}

Dht22Pigpio::~Dht22Pigpio() {
    if (pigpio_inited_) gpioTerminate();
}

static inline void sleep_us(int us) { gpioDelay(us); }

std::optional<DhtReading> Dht22Pigpio::read_once(int timeout_ms)
{
    if (!pigpio_inited_) return std::nullopt;

    // DHT22 protocol:
    // 1) MCU pulls line low ~1ms
    // 2) release (input, pull-up)
    // 3) sensor responds with pulses then 40 bits

    gpioSetMode(gpio_, PI_OUTPUT);
    gpioWrite(gpio_, 0);
    sleep_us(1100); // 1.1ms
    gpioSetMode(gpio_, PI_INPUT);
    gpioSetPullUpDown(gpio_, PI_PUD_UP);

    // capture edges: wait for level changes with timeout
    auto wait_level = [&](int level, int max_us) -> bool {
        int t = 0;
        while (gpioRead(gpio_) != level) {
            sleep_us(2);
            t += 2;
            if (t >= max_us) return false;
        }
        return true;
    };

    // sensor response: low then high
    if (!wait_level(0, timeout_ms * 1000)) return std::nullopt;
    if (!wait_level(1, timeout_ms * 1000)) return std::nullopt;
    if (!wait_level(0, timeout_ms * 1000)) return std::nullopt; // start of data

    // read 40 bits: each bit = 50us low + (26-28us high=0, ~70us high=1)
    unsigned char data[5] = {0,0,0,0,0};

    for (int i = 0; i < 40; i++) {
        // wait for high
        if (!wait_level(1, 200)) return std::nullopt;
        int width = 0;
        while (gpioRead(gpio_) == 1) {
            sleep_us(2);
            width += 2;
            if (width > 200) break;
        }
        // width > ~50us => 1
        int bit = (width > 50) ? 1 : 0;
        data[i/8] <<= 1;
        data[i/8] |= bit;

        // wait for low (between bits)
        // (some sensors already low by the time we exit high loop)
        // but keep it safe:
        // if (!wait_level(0, 200)) return std::nullopt;
    }

    // checksum
    unsigned char sum = (unsigned char)(data[0] + data[1] + data[2] + data[3]);
    if (sum != data[4]) return std::nullopt;

    // DHT22 format:
    // humidity = (data0<<8 | data1) / 10
    // temp = ((data2&0x7F)<<8 | data3) / 10, sign in data2 bit7
    int rh10 = (data[0] << 8) | data[1];
    int t10  = ((data[2] & 0x7F) << 8) | data[3];
    bool neg = data[2] & 0x80;

    double rh = rh10 / 10.0;
    double tc = t10 / 10.0;
    if (neg) tc = -tc;

    return DhtReading{tc, rh};
}

std::optional<DhtReading> Dht22Pigpio::read_retry(int tries, int delay_ms)
{
    for (int i = 0; i < tries; i++) {
        auto r = read_once();
        if (r) return r;
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
    return std::nullopt;
}

