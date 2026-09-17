#include "dht12.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

Dht12I2C::Dht12I2C(int bus, int addr) : addr_(addr)
{
    char dev[32];
    snprintf(dev, sizeof(dev), "/dev/i2c-%d", bus);

    fd_ = open(dev, O_RDWR);
    if (fd_ < 0) {
        perror("DHT12 open i2c");
        return;
    }
    if (ioctl(fd_, I2C_SLAVE, addr_) < 0) {
        perror("DHT12 ioctl I2C_SLAVE");
        close(fd_);
        fd_ = -1;
        return;
    }
}

std::optional<Dht12Reading> Dht12I2C::read_once()
{
    if (fd_ < 0) return std::nullopt;

    // DHT12: write start register 0x00 then read 5 bytes
    uint8_t reg = 0x00;
    if (write(fd_, &reg, 1) != 1) return std::nullopt;

    uint8_t b[5] = {0};
    if (read(fd_, b, 5) != 5) return std::nullopt;

    uint8_t sum = (uint8_t)(b[0] + b[1] + b[2] + b[3]);
    if (sum != b[4]) return std::nullopt;

    double hum = b[0] + b[1] / 10.0;

    // temp sign bit often stored in bit7 of b[3]
    bool neg = (b[3] & 0x80) != 0;
    double t = b[2] + (b[3] & 0x7F) / 10.0;
    if (neg) t = -t;

    // sanity
    if (hum < 0.0 || hum > 100.0) return std::nullopt;
    if (t < -40.0 || t > 80.0) return std::nullopt;

    return Dht12Reading{t, hum};
}
