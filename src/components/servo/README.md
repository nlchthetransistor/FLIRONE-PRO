# Servo Control Library for Raspberry Pi

Một thư viện C hoàn chỉnh để điều khiển động cơ Servo trên Raspberry Pi bằng **PWM GPIO** và **pigpio**.

A complete C library for controlling servo motors on Raspberry Pi using **PWM GPIO** and **pigpio**.

---

## Tính Năng / Features

- ✅ Điều khiển một hoặc nhiều servo cùng lúc / Control one or multiple servos simultaneously
- ✅ Hỗ trợ servo tiêu chuẩn 50-300Hz PWM / Standard 50-300Hz PWM servo support
- ✅ Điều khiển góc (0-180°) hoặc độ rộng xung (microseconds) directly / Angle (0-180°) or pulse width control
- ✅ API đơn giản, dễ sử dụng / Simple and easy-to-use API
- ✅ Hỗ trợ cả thư viện tĩnh và động / Both static and shared libraries
- ✅ Có ví dụ đầy đủ / Comprehensive examples included
- ✅ Xử lý lỗi toàn diện / Comprehensive error handling

---

## Yêu Cầu / Requirements

### Hardware
- Raspberry Pi (bất kỳ mẫu nào / any model)
- Một hoặc nhiều động cơ Servo (SG90, MG90S, v.v.)
- Nguồn điện (5V, >= 500mA cho mỗi servo)

### Software
```bash
# Cài đặt pigpio library
sudo apt-get update
sudo apt-get install -y pigpio python3-pigpio
```

---

## Cài Đặt / Installation

### 1. Clone hoặc tải thư viện
```bash
cd ~/servo-library
```

### 2. Biên dịch
```bash
# Biên dịch tất cả
make all

# Hoặc từng phần riêng
make libservo.a              # Static library
make libservo.so             # Shared library
make servo_example           # Example program
```

### 3. Cài đặt toàn hệ thống (tuỳ chọn)
```bash
sudo make install
```

---

## Cấu Trúc Thư Viện / Library Structure

```
servo-library/
├── servo.h           # Header file với API
├── servo.c           # Implementation
├── example.c         # Ví dụ sử dụng
├── Makefile         # Build configuration
├── README.md        # Documentation (this file)
└── build/           # Build output directory (created after compilation)
```

---

## API Reference

### Khởi Tạo / Initialization

```c
int servo_library_init(void);
void servo_library_cleanup(void);
```

Phải gọi `servo_library_init()` trước khi tạo bất kỳ servo nào.
Call `servo_library_init()` before creating any servo objects.

### Tạo và Hủy Servo / Create and Destroy

```c
Servo* servo_create(uint8_t gpio, uint16_t min_pulse_us, uint16_t max_pulse_us);
void servo_destroy(Servo *servo);
```

**Parameters:**
- `gpio`: GPIO pin number (BCM numbering)
- `min_pulse_us`: Minimum pulse width in microseconds (typically 1000)
- `max_pulse_us`: Maximum pulse width in microseconds (typically 2000)

**Example:**
```c
Servo *servo = servo_create(17, 1000, 2000);  // GPIO 17, standard servo
```

### Điều Khiển Góc / Angle Control

```c
int servo_set_angle(Servo *servo, double angle);
double servo_get_angle(Servo *servo);
```

**Parameters:**
- `angle`: Angle in degrees (0-180)

**Example:**
```c
servo_set_angle(servo, 0.0);    // Move to leftmost position
servo_set_angle(servo, 90.0);   // Move to center (neutral)
servo_set_angle(servo, 180.0);  // Move to rightmost position
```

### Điều Khiển Độ Rộng Xung / Pulse Width Control

```c
int servo_set_pulse(Servo *servo, uint16_t pulse_us);
int servo_get_pulse(Servo *servo);
```

**Parameters:**
- `pulse_us`: Pulse width in microseconds

**Example:**
```c
servo_set_pulse(servo, 1500);  // Center position
servo_set_pulse(servo, 1000);  // Leftmost position
servo_set_pulse(servo, 2000);  // Rightmost position
```

### Bật/Tắt Servo / Enable/Disable

```c
int servo_enable(Servo *servo);
int servo_disable(Servo *servo);
int servo_stop(Servo *servo);
```

**Example:**
```c
servo_disable(servo);  // Stop servo
servo_enable(servo);   // Re-enable servo
```

---

## Ví Dụ Sử Dụng / Usage Examples

### Ví Dụ 1: Điều Khiển Cơ Bản / Basic Control

```c
#include "servo.h"
#include <unistd.h>

int main() {
    // Initialize
    servo_library_init();
    
    // Create servo on GPIO 17
    Servo *servo = servo_create(17, 1000, 2000);
    
    // Move servo
    servo_set_angle(servo, 0.0);
    sleep(1);
    servo_set_angle(servo, 90.0);
    sleep(1);
    servo_set_angle(servo, 180.0);
    sleep(1);
    
    // Cleanup
    servo_destroy(servo);
    servo_library_cleanup();
    
    return 0;
}
```

### Ví Dụ 2: Điều Khiển Nhiều Servo / Multiple Servos

```c
#include "servo.h"
#include <unistd.h>

int main() {
    servo_library_init();
    
    // Create two servos on different GPIO pins
    Servo *servo1 = servo_create(17, 1000, 2000);
    Servo *servo2 = servo_create(27, 1000, 2000);
    
    // Move both servos
    servo_set_angle(servo1, 0.0);
    servo_set_angle(servo2, 180.0);
    sleep(1);
    
    servo_set_angle(servo1, 180.0);
    servo_set_angle(servo2, 0.0);
    sleep(1);
    
    servo_destroy(servo1);
    servo_destroy(servo2);
    servo_library_cleanup();
    
    return 0;
}
```

### Ví Dụ 3: Sweep (Quét)

```c
for (int angle = 0; angle <= 180; angle += 10) {
    servo_set_angle(servo, (double)angle);
    sleep(1);
}
```

---

## Chạy Ví Dụ / Running Examples

```bash
# Run với tất cả ví dụ
sudo make run_example

# Hoặc chạy từng ví dụ riêng
sudo make run_example_basic       # Example 1: Basic control
sudo make run_example_multiple    # Example 2: Multiple servos
sudo make run_example_pulse       # Example 3: Pulse width control

# Hoặc trực tiếp
sudo ./servo_example
sudo ./servo_example 1
sudo ./servo_example 2
sudo ./servo_example 3
```

**Lưu ý:** Cần chạy với `sudo` vì pigpio yêu cầu quyền root.
Note: Must run with `sudo` because pigpio requires root privileges.

---

## GPIO Pin Mapping (Raspberry Pi)

Các GPIO pins hữu ích cho servo:
Useful GPIO pins for servo:

```
Physical Pin -> BCM GPIO
=====================================
Pin 11    ->  GPIO 17  (ví dụ: Camera)
Pin 13    ->  GPIO 27
Pin 15    ->  GPIO 22
Pin 16    ->  GPIO 23
Pin 18    ->  GPIO 24
Pin 22    ->  GPIO 25
Pin 29    ->  GPIO 5
Pin 31    ->  GPIO 6
Pin 32    ->  GPIO 12
Pin 33    ->  GPIO 13
Pin 35    ->  GPIO 19
Pin 36    ->  GPIO 16
Pin 37    ->  GPIO 26
Pin 38    ->  GPIO 20
Pin 40    ->  GPIO 21
```

Được hỗ trợ trên Raspberry Pi 3, 4, Zero, v.v.
Supported on Raspberry Pi 3, 4, Zero, etc.

---

## Sơ Đồ Kết Nối / Wiring Diagram

```
Servo Motor:
  - Red wire    -> 5V Power Supply
  - Black wire  -> GND (Ground)
  - Orange/Yellow wire -> GPIO PIN (e.g., GPIO 17)

Raspberry Pi GPIO Pin 11 (GPIO 17)
                    |
                    R
  GPIO 17 ----[100Ω]---+----- Servo Control Signal (Orange)
                        |
         (Optional Level Shifter for 5V servos)
                        |
                       GND
```

---

## Troubleshooting

### Lỗi "Failed to initialize pigpio"
```bash
# Đảm bảo pigpio data launchpad đang chạy
sudo systemctl start pigpiod
sudo systemctl enable pigpiod  # Auto-start on boot
```

### Servo không hoạt động / Servo not working
1. Kiểm tra kết nối dây / Check wiring
2. Kiểm tra GPIO pin / Verify GPIO pin number
3. Kiểm tra nguồn điện / Check power supply
4. Chạy với `sudo` / Run with `sudo`

### Servo rung hoặc không mượt / Servo jittering or jerky
- Kiểm tra nguồn điện có ổn định không / Check power supply stability
- Tăng giá trị delay trong code / Increase delay values in code
- Thử điều chỉnh min_pulse_us và max_pulse_us / Try adjusting pulse width values

---

## Ứng Dụng Tiêu Biểu / Typical Applications

- 🤖 Robot arms / Tay cánh tay robot
- 🎥 Pan-tilt camera systems / Hệ thống camera quay theo dõi
- 🚗 RC vehicles / Xe điều khiển từ xa
- 🦾 Quadruped robots / Robot tứ chân
- 📡 Antenna positioners / Định vị ăng-ten
- 🎮 Interactive displays / Màn hình tương tác

---

## Ghi Chú / Notes

### Giới Hạn Pulse Width
- Tiêu chuẩn: 1000-2000 µs (50Hz PWM)
- Min: 500 µs, Max: 2500 µs
- Standard: 1000-2000 µs (50Hz PWM)
- Min: 500 µs, Max: 2500 µs

### Số Lượng Servo Tối Đa
Có thể điều khiển up to **28 servos** ( tương ứng với GPIO pins có sẵn)
Can control up to **28 servos** (depending on available GPIO pins)

---

## License

Thư viện này được phát hành dưới Apache License 2.0.
This library is released under Apache License 2.0.

---

## Hỗ Trợ / Support

Nếu bạn gặp vấn đề, hãy kiểm tra:
For issues, please check:
- Pigpio documentation: http://abyz.me.uk/rpi/pigpio/
- Raspberry Pi documentation: https://www.raspberrypi.com/documentation/

---

## Tác Giả / Authors

Servo Control Library Contributors

---

**Happy Servo Control! 🎉**
