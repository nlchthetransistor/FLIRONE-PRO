# Servo Motor Integration - Flirone

Tài liệu này mô tả cách servo motor được tích hợp vào ứng dụng flirone.

## 📋 Tổng Quan

Servo motor được tích hợp để điều khiển hướng quay của camera hoặc cảm biến. Người dùng có thể điều khiển góc quay từ 0° đến 180° bằng các phím bàn phím.

## 🎮 Điều Khiển Servo

### Phím Điều Khiển

| Phím | Chức Năng |
|------|----------|
| `,` (dấu phẩy) | Giảm góc quay (sang trái) |
| `.` (dấu chấm) | Tăng góc quay (sang phải) |
| `1-4` | Chọn tham số hiệu chỉnh nhiệt độ |
| `+/-` | Điều chỉnh tham số hiệu chỉnh |
| `0` | Reset tất cả tham số về mặc định |
| `ESC` | Thoát chương trình |

### Điều Khiển Chi Tiết

- **Phím `,` (comma - keycode 44 = 0x2C)**
  - Giảm góc servo mỗi lần nhấn 5°
  - Giới hạn tối thiểu: 0°
  
- **Phím `.` (period - keycode 46 = 0x2E)**
  - Tăng góc servo mỗi lần nhấn 5°
  - Giới hạn tối đa: 180°

## ⚙️ Cấu Hình Phần Cứng

### GPIO Pin

```
Servo GPIO Pin: GPIO 17 (Broadcom BCM)
  - Có thể thay đổi bằng #define SERVO_GPIO_PIN trong flirone.cpp

Kết nối vật lý:
  GPIO 17 (Pin 11) --- Signal --- Servo Control (Orange/Yellow wire)
  GND              --- Ground --- Servo Ground (Black wire)
  5V Power         --- VCC    --- Servo Power (Red wire)
```

### Pulse Width Configuration

```c
// Cấu hình pulse width (trong flirone.cpp)
#define SERVO_MIN_PULSE  1000   // microseconds (0° position)
#define SERVO_MAX_PULSE  2000   // microseconds (180° position)
#define SERVO_INITIAL_ANGLE 90.0 // degrees (center/neutral)
#define SERVO_ANGLE_STEP 5.0     // degrees per keypress
```

## 📊 Side Panel Display

Side panel bên phải của ứng dụng hiển thị:

- **SERVO Section:**
  - Góc hiện tại (°)
  - Trạng thái servo (ON/OFF)

```
┌─────────────────────────┐
│      FLIRONE            │
├─────────────────────────┤
│ AMBIENT                 │
│ Ta    27.5 C            │
│ RH    65%               │
│ ✓ DHT: OK              │
├─────────────────────────┤
│ TUNING                  │
│ ...                     │
├─────────────────────────┤
│ SERVO                   │
│ Position  90.0°        │
│ ✓ Servo: ON            │
├─────────────────────────┤
│ Controls                │
│ , . : servo            │
│ ESC : quit             │
└─────────────────────────┘
```

## 🔧 Thay Đổi Cấu Hình

### Thay Đổi GPIO Pin

Sửa file `flirone.cpp`:

```cpp
#define SERVO_GPIO_PIN 17  // Thay 17 bằng pin GPIO cần dùng
```

Các GPIO pins phổ biến:
- GPIO 17 (Pin 11)
- GPIO 27 (Pin 13)
- GPIO 22 (Pin 15)
- GPIO 23 (Pin 16)
- GPIO 24 (Pin 18)
- GPIO 25 (Pin 22)

### Thay Đổi Tốc Độ Điều Khiển

```cpp
#define SERVO_ANGLE_STEP 5.0    // Thay 5 bằng độ tăng/giảm mong muốn
// Ví dụ: 1.0 = 1° per key press, 10.0 = 10° per key press
```

### Thay Đổi Góc Ban Đầu

```cpp
#define SERVO_INITIAL_ANGLE 90.0 // Thay 90 bằng góc ban đầu mong muốn (0-180)
```

## 🏗️ Cấu Trúc Mã

### Files Liên Quan

```
src/
├── flirone.cpp           # Main application (có servo integration)
├── components/
│   └── servo/
│       ├── servo.h       # Servo API header
│       ├── servo.c       # Servo implementation
│       ├── Makefile      # Servo library build
│       └── lib/
│           ├── libservo.a   # Static library
│           └── libservo.so  # Shared library
├── Makefile              # Main build configuration
└── build_with_servo.sh   # Build script
```

### Servo Integration Points

1. **Includes (line ~55)**
   ```cpp
   extern "C" {
   #include "components/servo/servo.h"
   }
   ```

2. **Global Variables (line ~152)**
   ```cpp
   static Servo* g_servo = NULL;
   static std::atomic<double> g_servo_angle{SERVO_INITIAL_ANGLE};
   static std::atomic<bool>   g_servo_enabled{false};
   ```

3. **Initialization (line ~354)**
   ```cpp
   void init_servo()  // Khởi tạo servo
   void cleanup_servo() // Dọn dẹp servo
   ```

4. **Key Handling (line ~945)**
   ```cpp
   // Servo control: ',' = decrease, '.' = increase
   if (key == KEY_SERVO_DEC || key == KEY_SERVO_INC) {
       // Set servo angle
   }
   ```

5. **UI Display (line ~221)**
   ```cpp
   // Draw servo info on side panel
   draw_side_panel_modern(..., servo_angle, servo_enabled);
   ```

## ✅ Build & Run

### Build Toàn Bộ

```bash
cd ~/v4l2loopback/flirone-v4l2/src
./build_with_servo.sh
# Hoặc
make all
```

### Build Chỉ Servo Library

```bash
cd ~/v4l2loopback/flirone-v4l2/src/components/servo
make all
```

### Build Chỉ Flirone

```bash
cd ~/v4l2loopback/flirone-v4l2/src
make flirone
# Hoặc
make build_flirone
```

### Chạy

```bash
cd ~/v4l2loopback/flirone-v4l2/src
sudo ./flirone ../palettes/Iron2.raw

# Note: Cần sudo vì pigpio yêu cầu quyền root
```

## 🐛 Troubleshooting

### Lỗi: "Failed to initialize servo library"

**Nguyên nhân:** Pigpio không cài đặt hoặc không chạy

**Giải pháp:**
```bash
sudo apt-get install pigpio python3-pigpio
sudo systemctl start pigpiod
sudo systemctl enable pigpiod  # Auto-start on boot
```

### Lỗi: "Failed to create servo object"

**Nguyên nhân:** GPIO pin không hợp lệ hoặc đã bị sử dụng

**Giải pháp:**
1. Kiểm tra GPIO pin không conflict
2. Thử pin khác trong flirone.cpp
3. Đảm bảo chạy với `sudo`

### Servo không xoay

**Nguyên nhân:** Có thể có các nguyên nhân sau:
- Kết nối dây không chắc
- Nguồn điện không đủ
- GPIO pin không đúng
- Servo bị hỏng

**Giải pháp:**
1. Kiểm tra kết nối dây (signal, GND, 5V)
2. Kiểm tra nguồn điện (>500mA for servo)
3. Test với chương trình servo example riêng
4. Xem console output để biết errorcode thực tế

### Servo rung hoặc chuyển động không mượt

**Nguyên nhân:** Voltage không ổn định, noise, hoặc servo quality

**Giải pháp:**
1. Sử dụng regulated power supply (5V, >1A)
2. Thêm capacitor 100nF gần servo
3. Giảm SERVO_ANGLE_STEP nếu quá nhanh
4. Kiểm tra servo pulse width có trong range (1000-2000 µs)

## 📚 API Reference

### Servo Library API

Xem [components/servo/README.md](../components/servo/README.md) để biết chi tiết. Các hàm chính:

```c
// Initialization
int servo_library_init(void);
void servo_library_cleanup(void);

// Create/Destroy
Servo* servo_create(uint8_t gpio, uint16_t min_pulse_us, uint16_t max_pulse_us);
void servo_destroy(Servo *servo);

// Control
int servo_set_angle(Servo *servo, double angle);  // 0-180°
double servo_get_angle(Servo *servo);

// Advanced
int servo_set_pulse(Servo *servo, uint16_t pulse_us);  // Direct pulse control
int servo_get_pulse(Servo *servo);

// Enable/Disable
int servo_enable(Servo *servo);
int servo_disable(Servo *servo);
```

## 💡 Ví Dụ Sử Dụng

### Pan-Tilt Camera

```
Servo 1: Pan (left-right)  - GPIO 17
Servo 2: Tilt (up-down)    - GPIO 27

Điều khiển bằng 4 phím arrow hoặc tùy chỉnh mapping
```

### Robot Arm

```
Servo 1: Base rotation     - GPIO 17
Servo 2: Shoulder joint    - GPIO 27
Servo 3: Elbow joint       - GPIO 22
Servo 4: Wrist rotation    - GPIO 23

Tập hợp nhiều servo với tương tác phức tạp
```

## 🔐 License & Attribution

Servo control library sử dụng pigpio library:
- Pigpio: http://abyz.me.uk/rpi/pigpio/

Flirone được fork từ:
- Original: https://github.com/fnoop/flirone-v4l2

## 📞 Support

Để debug chi tiết, xem console output:

```bash
# Run with debug output
sudo ./flirone ../palettes/Iron2.raw > debug.log 2>&1
tail -f debug.log

# Hoặc redirect stderr
sudo ./flirone ../palettes/Iron2.raw 2>flirone_errors.log
```

Log sẽ chứa:
- Servo initialization messages
- Key press codes (debug output)
- Servo angle changes
- Error messages
