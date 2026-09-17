#pragma once
#include <optional>

struct Dht12Reading {
    double temp_c;
    double hum_rh;
};

class Dht12I2C {
public:
    Dht12I2C(int bus = 1, int addr = 0x5c);
    std::optional<Dht12Reading> read_once();

private:
    int fd_ = -1;
    int addr_ = 0x5c;
};
