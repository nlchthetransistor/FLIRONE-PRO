#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <libusb.h>
#include <unistd.h>
#include <time.h>
#include <opencv2/opencv.hpp>

#include <vector>
#include <algorithm>
#include <cmath>

#include <fcntl.h>
#include <math.h>

#include "jpeglib.h"

#include "plank.h"

// -- define v4l2 ---------------
#include <linux/videodev2.h>
#include <sys/ioctl.h>
// NOTE: <string.h> and <fcntl.h> already included above — duplicates removed
#include <assert.h>

#include <atomic>
#include <thread>
#include <chrono>
#include "dht12.hpp"

// Servo control (C library)
extern "C" {
#include "components/servo/servo.h"
}

#include "components/google_sheets/google_sheets_logger.hpp"

#include <sstream>
#include <iomanip>
#include <sys/stat.h>
#include <sys/types.h>

// ========== Google Sheets Configuration ==========
#define GOOGLE_SHEETS_DEPLOYMENT_URL "https://script.google.com/macros/s/AKfycbz2TfvPLzgVIyvHZDzXWgKd64Fq5TFxMenrFzKO5xzHjrznV4UKUyFG-4fLmb1m_q0N/exec"

// Servo configuration
#define SERVO_USE_PCA9685 1          // 1 = use PCA9685, 0 = use GPIO pigpio

// GPIO mode (pigpio)
#define SERVO_GPIO_PIN 17            // GPIO17 - adjust as needed for your setup

// PCA9685 mode
#define PCA9685_I2C_BUS 1            // I2C bus number (/dev/i2c-1)
#define PCA9685_I2C_ADDR 0x40        // PCA9685 default address
#define PCA9685_CHANNEL 0            // PCA9685 output channel (0-15)
#define PCA9685_PWM_FREQ 50.0        // PWM frequency in Hz for servo control

#define SERVO_MIN_PULSE 1000         // microseconds
#define SERVO_MAX_PULSE 2000         // microseconds
#define SERVO_INITIAL_ANGLE 90.0     // degrees (center position)

#define SERVO_DISPLAY_MIN -45.0      // software display range
#define SERVO_DISPLAY_MAX 45.0
#define SERVO_DISPLAY_STEP 5.0       // shown angle step per keypress
#define SERVO_ANGLE_STEP (SERVO_DISPLAY_STEP * 2.0) // internal servo degrees per keypress

static inline double servo_actual_to_display(double actual_angle)
{
    return actual_angle * 0.5 - 45.0;
}

static inline double servo_display_to_actual(double display_angle)
{
    return (display_angle + 45.0) * 2.0;
}

#define VIDEO_DEVICE0 "/dev/video1"  // gray scale thermal image
#define FRAME_WIDTH0  160
#define FRAME_HEIGHT0 120

#define VIDEO_DEVICE1 "/dev/video2" // color visible image
#define FRAME_WIDTH1  640
#define FRAME_HEIGHT1 480

#define VIDEO_DEVICE2 "/dev/video3" // colorized thermal image
#define FRAME_WIDTH2  160
#define FRAME_HEIGHT2 128

#define FRAME_FORMAT0 V4L2_PIX_FMT_GREY
#define FRAME_FORMAT1 V4L2_PIX_FMT_MJPEG
#define FRAME_FORMAT2 V4L2_PIX_FMT_RGB24

struct v4l2_capability vid_caps0;
struct v4l2_capability vid_caps1;
struct v4l2_capability vid_caps2;

struct v4l2_format vid_format0;
struct v4l2_format vid_format1;
struct v4l2_format vid_format2;

size_t framesize0;
size_t linewidth0;

size_t framesize1;
size_t linewidth1;

size_t framesize2;
size_t linewidth2;

     
const char *video_device0=VIDEO_DEVICE0;
const char *video_device1=VIDEO_DEVICE1;
const char *video_device2=VIDEO_DEVICE2;

int fdwr0 = 0;
int fdwr1 = 0;
int fdwr2 = 0;

// -- end define v4l2 ---------------

 #define VENDOR_ID 0x09cb
 #define PRODUCT_ID 0x1996

// -- buffer for EP 0x85 chunks (assemble full frame) ---------------
#ifndef BUF85SIZE
#define BUF85SIZE (1048576) // 1MB, size used by original android app / reference implementations
#endif
static int g_buf85pointer = 0;
static unsigned char g_buf85[BUF85SIZE];

 static struct libusb_device_handle *devh = NULL;
 int filecount=0;
 struct timeval t1, t2;
 long long fps_t;
 
 int FFC =   0; // detect FFC
 
// ===== Global for mouse picking =====
static unsigned short g_pix[160 * 120];
static float g_scale = 3;

static int g_canvas_img_w = 160 * g_scale; // phần ảnh (scaled) trong canvas, để ignore click vào panel
// Point measurement
static int g_mouse_x = -1;
static int g_mouse_y = -1;
static bool g_mouse_click = false;

// ROI measurement
static bool g_selecting = false;
static bool g_roi_valid = false;
static int g_x0 = 0, g_y0 = 0;
static int g_x1 = 0, g_y1 = 0;

static std::atomic<double> g_view_s{1.0};
static std::atomic<int>    g_view_cx{0}, g_view_cy{0};

std::atomic<double> g_Ta{27.0};
std::atomic<double> g_RH{50.0};
std::atomic<bool>   g_dht_ok{false};
std::atomic<uint64_t> g_dht_seq{0};  // tăng mỗi lần đọc OK (để invalid LUT nếu muốn)

// Servo control globals
static Servo* g_servo = NULL;
static std::atomic<double> g_servo_angle{SERVO_INITIAL_ANGLE};
static std::atomic<bool>   g_servo_enabled{false};

// Google Sheets Logger
static GoogleSheetsLogger* g_sheets_logger = NULL;

// Screenshot capture globals
static uint64_t g_screenshot_count = 0; // số screenshots đã lưu
static bool g_measurement_pending = false;  // Flag để track xem có measurement cần ghi hay không
static std::string g_pending_measurement_type = "";  // "spot" or "roi"

// Persistent measurement display (hiển thị kết quả trong 2 giây)
static auto g_last_measurement_time = std::chrono::steady_clock::now();  // Khi measurement xảy ra
static double g_last_spot_temp = 0.0;
static double g_last_roi_avg_temp = 0.0;
static std::string g_last_display_type = "";  // "spot" or "roi"
static const int MEASUREMENT_DISPLAY_DURATION_MS = 3000;  // Hiển thị 2 giây

// Patient info globals
static std::string g_patient_name = "Unknown";
static std::string g_patient_age = "Unknown";
static std::string g_patient_gender = "Unknown";

static inline double clampd(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline cv::Scalar rgba(int b,int g,int r){ return cv::Scalar(b,g,r); }

static void draw_badge(cv::Mat& img, cv::Rect rc, const std::string& text, cv::Scalar bg, cv::Scalar fg){
    cv::rectangle(img, rc, bg, cv::FILLED, cv::LINE_AA);
    cv::putText(img, text, {rc.x+10, rc.y + rc.height-10}, cv::FONT_HERSHEY_SIMPLEX, 0.55, fg, 1, cv::LINE_AA);
}

static void draw_hr(cv::Mat& img, int x, int y, int w){
    cv::line(img, {x, y}, {x+w, y}, rgba(70,70,70), 1, cv::LINE_AA);
}

static void draw_kv(cv::Mat& img, int x, int y, const std::string& k, const std::string& v){
    cv::putText(img, k, {x, y}, cv::FONT_HERSHEY_SIMPLEX, 0.55, rgba(200,200,200), 1, cv::LINE_AA);
    cv::putText(img, v, {x+150, y}, cv::FONT_HERSHEY_SIMPLEX, 0.60, rgba(255,255,255), 1, cv::LINE_AA);
}

// mini slider bar for selected parameter
static void draw_slider(cv::Mat& img, int x, int y, int w, double v, double lo, double hi, bool active){
    cv::Scalar bg = active ? rgba(60,60,60) : rgba(45,45,45);
    cv::Scalar fg = active ? rgba(0,180,255) : rgba(140,140,140);
    cv::rectangle(img, {x, y-10, w, 10}, bg, cv::FILLED, cv::LINE_AA);

    double t = (hi <= lo) ? 0.0 : (v - lo) / (hi - lo);
    if (t < 0) 
    {t = 0;}
    if (t > 1) {t = 1;}
    int px = x + (int)(t * w);

    cv::rectangle(img, {x, y-10, px-x, 10}, fg, cv::FILLED, cv::LINE_AA);
    cv::circle(img, {px, y-5}, 6, rgba(255,255,255), cv::FILLED, cv::LINE_AA);
}



struct Tunables {
    // Các tham số hiệu chỉnh nhiệt độ (tune realtime bằng phím)
    double emissivity = 0.98;   // hệ số phát xạ (da người ~0.97-0.99)
    double reflOff    = 0.0;    // °C, cộng vào Ta (ambient) để ra T_refl
    double rawScale   = 4.0;    // hệ số scale RAW (bản gốc thường *4)
    double tempOff    = 0.0;    // °C, offset cộng vào kết quả cuối

    // Giới hạn để tránh chỉnh “điên”
    double emiss_min = 0.70, emiss_max = 1.00;
    double refl_min  = -20.0, refl_max  =  20.0;
    double scale_min = 1.0,  scale_max = 6.0;
    double toff_min  = -10.0, toff_max  =  10.0;
};

static Tunables g_tune;

// g_sel_param is internal 0..3 (0=Emissivity,1=Refl,2=RawScale,3=TempOff)
static int g_sel_param = 0;
// Vẽ HUD có nền bán trong suốt

static inline int clampi(int v, int lo, int hi){ return v<lo?lo:(v>hi?hi:v); }
// selected: 0=eps,1=reflOff,2=rawScale,3=tempOff
static void draw_side_panel_modern(cv::Mat& canvas, int x0, int w,
                                  double Ta, double RH, bool dht_ok,
                                  int selected,
                                  double emissivity, double reflOff, double rawScale, double tempOff,
                                  double servo_angle, bool servo_enabled)
{
    const int H = canvas.rows;
    cv::Rect panel(x0, 0, w, H);

    // panel background
    cv::rectangle(canvas, panel, rgba(22,22,22), cv::FILLED);

    int x = x0 + 16;
    int y = 28;

    // FIX: cache font sizes — H rarely changes, avoid recomputing every frame
    static int   cached_H      = -1;
    static double fs_title = 0.85, fs_hdr = 0.55, fs_row_k = 0.58;
    static double fs_row_v = 0.62, fs_hint = 0.50, fs_footT = 0.58, fs_foot = 0.50;
    if (H != cached_H) {
        cached_H = H;
        fs_title = (H < 650) ? 0.75 : 0.85;
        fs_hdr   = (H < 650) ? 0.50 : 0.55;
        fs_row_k = (H < 650) ? 0.54 : 0.58;
        fs_row_v = (H < 650) ? 0.58 : 0.62;
        fs_hint  = (H < 650) ? 0.45 : 0.50;
        fs_footT = (H < 650) ? 0.52 : 0.58;
        fs_foot  = (H < 650) ? 0.45 : 0.50;
    }

    // Title
    cv::putText(canvas, "FLIRONE", {x, y}, cv::FONT_HERSHEY_SIMPLEX, fs_title, rgba(255,255,255), 2, cv::LINE_AA);
    y += 16;
    draw_hr(canvas, x, y, w-32);
    y += 24;

    // Ambient card
    cv::putText(canvas, "AMBIENT", {x, y}, cv::FONT_HERSHEY_SIMPLEX, fs_hdr, rgba(160,160,160), 1, cv::LINE_AA);
    y += 18;

    char buf[128];
    snprintf(buf, sizeof(buf), "%.1f C", Ta);
    draw_kv(canvas, x, y, "Ta", buf);
    y += 22;

    snprintf(buf, sizeof(buf), "%.0f %%", RH);
    draw_kv(canvas, x, y, "RH", buf);
    y += 24;

    draw_badge(canvas, {x, y-16, 120, 24},
               dht_ok ? "DHT: OK" : "DHT: N/A",
               dht_ok ? rgba(20,110,40) : rgba(90,40,40),
               rgba(255,255,255));
    y += 20;

    y += 10;
    draw_hr(canvas, x, y, w-32);
    y += 24;

    // Tuning card
    cv::putText(canvas, "TUNING", {x, y}, cv::FONT_HERSHEY_SIMPLEX, fs_hdr, rgba(160,160,160), 1, cv::LINE_AA);
    y += 18;

    struct P { const char* key; double v, lo, hi, step; };
    P ps[4] = {
        {"Emissivity", emissivity, 0.70, 1.00, 0.01},
        {"Refl offset", reflOff,  -20.0, 20.0, 0.10},
        {"Raw scale",   rawScale,  1.00, 6.00, 0.05},
        {"Temp offset", tempOff,  -10.0, 10.0, 0.10}
    };

    for(int i=0;i<4;i++){
        bool act = (i==selected);
        cv::Scalar kcol = act ? rgba(255,255,255) : rgba(200,200,200);
        cv::Scalar vcol = act ? rgba(0,180,255) : rgba(255,255,255);

        // row background highlight
        if(act){
            cv::rectangle(canvas, {x-8, y-16, w-32+16, 52}, rgba(35,35,35), cv::FILLED, cv::LINE_AA);
        }

        cv::putText(canvas, ps[i].key, {x, y}, cv::FONT_HERSHEY_SIMPLEX, fs_row_k, kcol, 1, cv::LINE_AA);

        char val[64];
        if(i==0) snprintf(val, sizeof(val), "%.3f", ps[i].v);
        else     snprintf(val, sizeof(val), "%.2f", ps[i].v);
        cv::putText(canvas, val, {x0 + w - 16 - 90, y}, cv::FONT_HERSHEY_SIMPLEX, fs_row_v, vcol, 2, cv::LINE_AA);

        // slider
        draw_slider(canvas, x, y+16, w-32-20, ps[i].v, ps[i].lo, ps[i].hi, act);

        // hint line for active row only
        if(act){
            char hint[120];
            snprintf(hint, sizeof(hint), ",/.: %.2f   0: reset", ps[i].step);
            cv::putText(canvas, hint, {x, y+44}, cv::FONT_HERSHEY_SIMPLEX, fs_hint, rgba(170,170,170), 1, cv::LINE_AA);
        }

        y += 62;
    }

    // Servo control card
    y += 8;
    draw_hr(canvas, x, y, w-32);
    y += 20;
    cv::putText(canvas, "SERVO", {x, y}, cv::FONT_HERSHEY_SIMPLEX, fs_hdr, rgba(160,160,160), 1, cv::LINE_AA);
    y += 20;

    char servo_status[128];
    double servo_display_angle = servo_actual_to_display(servo_angle);
    snprintf(servo_status, sizeof(servo_status), "Angle: %.1f do", servo_display_angle);
    draw_kv(canvas, x, y, "Position", servo_status);
    y += 22;

    draw_badge(canvas, {x, y-14, 140, 22},
               servo_enabled ? "Servo: ON" : "Servo: OFF",
               servo_enabled ? rgba(30,100,70) : rgba(80,60,60),
               rgba(255,255,255));
    y += 20;

    // ---------------- Footer controls (FIXED) ----------------
    // Ước lượng footer cần bao nhiêu chiều cao
    // (hr + title + 3 lines)
    int footer_need = 22 + 20 + 18 + 16 + 16 + 10; // ~102px (an toàn)

    // Vị trí footer lý tưởng: sát đáy
    int footer_y = H - 80;

    // Nếu footer_y đè lên nội dung hiện tại (y), thì đặt footer ngay sau nội dung (không ép xuống đáy)
    // và nếu vẫn không đủ chỗ thì thu nhỏ thêm một chút
    if (footer_y < y + 10) {
        footer_y = y + 10;

        // nếu footer vượt đáy, thu nhỏ spacing bằng cách giảm font/footer line spacing
        if (footer_y + footer_need > H) {
            // cắt bớt 1 dòng note khi quá chật
            // (giữ 2 dòng quan trọng nhất)
            draw_hr(canvas, x, footer_y, w-32);
            footer_y += 22;
            cv::putText(canvas, "Controls", {x, footer_y}, cv::FONT_HERSHEY_SIMPLEX, fs_footT, rgba(255,255,255), 1, cv::LINE_AA);
            footer_y += 18;
            cv::putText(canvas, "1..4 select   +/- adjust", {x, footer_y}, cv::FONT_HERSHEY_SIMPLEX, fs_foot, rgba(190,190,190), 1, cv::LINE_AA);
            footer_y += 16;
            cv::putText(canvas, "Click: spot   Drag: ROI avg", {x, footer_y}, cv::FONT_HERSHEY_SIMPLEX, fs_foot, rgba(190,190,190), 1, cv::LINE_AA);
            return;
        }
    }

    // footer bình thường (đủ chỗ)
    y = footer_y;
    draw_hr(canvas, x, y, w-32);
    y += 22;
    cv::putText(canvas, "Controls", {x, y}, cv::FONT_HERSHEY_SIMPLEX, fs_footT, rgba(255,255,255), 1, cv::LINE_AA);
    y += 20;
    cv::putText(canvas, "1..4 select   +/- adjust", {x, y}, cv::FONT_HERSHEY_SIMPLEX, fs_foot, rgba(190,190,190), 1, cv::LINE_AA);
    y += 18;
    cv::putText(canvas, "Click: spot   Drag: ROI avg", {x, y}, cv::FONT_HERSHEY_SIMPLEX, fs_foot, rgba(190,190,190), 1, cv::LINE_AA);
    y += 16;
    cv::putText(canvas, ", . : servo   ESC: quit", {x, y}, cv::FONT_HERSHEY_SIMPLEX, fs_foot, rgba(190,190,190), 1, cv::LINE_AA);
}

static void draw_spot_overlay(cv::Mat& img, int x, int y, double tempC){
    char t[64];
    snprintf(t, sizeof(t), "%.2f C", tempC);

    int bx = std::max(8, x + 10);
    int by = std::max(28, y - 10);
    
    cv::Size ts = cv::getTextSize(t, cv::FONT_HERSHEY_SIMPLEX, 0.65, 2, nullptr);
    cv::Rect rc(bx, by - ts.height - 12, ts.width + 18, ts.height + 14);

    cv::rectangle(img, rc, rgba(20,20,20), cv::FILLED, cv::LINE_AA);
    cv::rectangle(img, rc, rgba(0,180,255), 1, cv::LINE_AA);
    cv::putText(img, t, {rc.x+9, rc.y+rc.height-8}, cv::FONT_HERSHEY_SIMPLEX, 0.65, rgba(255,255,255), 2, cv::LINE_AA);

    // marker
    cv::drawMarker(img, {x, y}, rgba(0,180,255), cv::MARKER_CROSS, 18, 2, cv::LINE_AA);
}

// ===================== Screenshot Functions =====================
static void ensure_screenshots_folder()
{
    const char* folder = "screenshots";
    struct stat st = {0};
    
    if (stat(folder, &st) == -1) {
        // Folder doesn't exist, create it
        if (mkdir(folder, 0755) == 0) {
            fprintf(stderr, "Created screenshots folder\n");
        } else {
            fprintf(stderr, "Warning: Failed to create screenshots folder\n");
        }
    }
}

static void save_screenshot(const cv::Mat& image, const char* measurement_type)
{
    ensure_screenshots_folder();
    
    // Generate timestamp: YYYYMMDD_HHMMSS
    time_t now = time(NULL);
    struct tm* loctime = localtime(&now);
    
    char filename[256];
    strftime(filename, sizeof(filename), "screenshots/%Y%m%d_%H%M%S", loctime);
    
    // Append patient name and measurement type to filename
    char fullpath[300];
    snprintf(fullpath, sizeof(fullpath), "%s_%s_%s.png", filename, g_patient_name.c_str(), measurement_type);
    
    // Save the image
    bool success = cv::imwrite(fullpath, image);
    
    if (success) {
        g_screenshot_count++;
        fprintf(stderr, "[Screenshot %lu] Saved: %s\n", g_screenshot_count, fullpath);
    } else {
        fprintf(stderr, "Error: Failed to save screenshot: %s\n", fullpath);
    }
}

// ===================== Patient Name Input UI =====================
// Helper function to get single patient info input
static std::string get_patient_input(const std::string& label, const std::string& default_val)
{
    std::string input_text = "";
    bool input_complete = false;
    
    while (!input_complete) {
        cv::Mat dialog(250, 600, CV_8UC3, cv::Scalar(40, 40, 40));
        
        // Draw title
        cv::putText(dialog, label, {100, 50},
                   cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(255, 255, 255), 2, cv::LINE_AA);
        
        // Draw input field
        cv::rectangle(dialog, cv::Point(50, 90), cv::Point(550, 140),
                     cv::Scalar(100, 100, 100), 2, cv::LINE_AA);
        
        // Draw current input with cursor
        cv::putText(dialog, input_text + "_", {75, 125},
                   cv::FONT_HERSHEY_SIMPLEX, 0.9, cv::Scalar(200, 200, 200), 2, cv::LINE_AA);
        
        // Draw instructions
        cv::putText(dialog, "Press ENTER to confirm / ESC for default", {70, 190},
                   cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(150, 150, 150), 1, cv::LINE_AA);
        
        cv::imshow("Patient Info Input", dialog);
        
        int key = cv::waitKey(0);
        
        if (key == 27) {  // ESC - use default
            cv::destroyWindow("Patient Info Input");
            return default_val;
        } else if (key == 13) {  // ENTER - confirm
            if (!input_text.empty()) {
                cv::destroyWindow("Patient Info Input");
                return input_text;
            }
        } else if (key >= 32 && key < 127) {  // Printable character
            if (input_text.length() < 50) {
                input_text += (char)key;
            }
        } else if (key == 8 || key == 127) {  // Backspace
            if (!input_text.empty()) {
                input_text.pop_back();
            }
        }
    }
    
    return default_val;
}

// Get patient info: name, age, gender
static void get_patient_info_from_ui()
{
    fprintf(stderr, "Opening patient info input dialog...\n");
    
    // Input name
    g_patient_name = get_patient_input("Enter Patient Name:", "Unknown");
    
    // Input age
    g_patient_age = get_patient_input("Enter Patient Age:", "Unknown");
    
    // Input gender
    g_patient_gender = get_patient_input("Enter Patient Gender (M/F/O):", "Unknown");
    
    fprintf(stderr, "Patient Info: Name=%s, Age=%s, Gender=%s\n", 
            g_patient_name.c_str(), g_patient_age.c_str(), g_patient_gender.c_str());
}

static void init_servo()
{
#if SERVO_USE_PCA9685
    fprintf(stderr, "Initializing servo on PCA9685 channel %d (I2C bus %d, addr 0x%02x)...\n",
            PCA9685_CHANNEL, PCA9685_I2C_BUS, PCA9685_I2C_ADDR);

    g_servo = servo_create_pca9685(PCA9685_CHANNEL,
                                   PCA9685_I2C_BUS,
                                   PCA9685_I2C_ADDR,
                                   PCA9685_PWM_FREQ,
                                   SERVO_MIN_PULSE,
                                   SERVO_MAX_PULSE);
    if (g_servo == NULL) {
        fprintf(stderr, "Error: Failed to create PCA9685 servo object\n");
        g_servo_enabled.store(false, std::memory_order_relaxed);
        return;
    }
#else
    fprintf(stderr, "Initializing servo on GPIO %d...\n", SERVO_GPIO_PIN);

    // Initialize servo library
    if (servo_library_init() != 0) {
        fprintf(stderr, "Error: Failed to initialize servo library\n");
        g_servo_enabled.store(false, std::memory_order_relaxed);
        return;
    }

    // Create servo object
    g_servo = servo_create(SERVO_GPIO_PIN, SERVO_MIN_PULSE, SERVO_MAX_PULSE);
    if (g_servo == NULL) {
        fprintf(stderr, "Error: Failed to create servo object\n");
        g_servo_enabled.store(false, std::memory_order_relaxed);
        return;
    }
#endif

    // Set initial angle
    if (servo_set_angle(g_servo, SERVO_INITIAL_ANGLE) == 0) {
        g_servo_angle.store(SERVO_INITIAL_ANGLE, std::memory_order_relaxed);
        g_servo_enabled.store(true, std::memory_order_relaxed);
        fprintf(stderr, "Servo initialized successfully at %.1f do\n", SERVO_INITIAL_ANGLE);
    } else {
        fprintf(stderr, "Error: Failed to set initial servo angle\n");
        servo_destroy(g_servo);
        g_servo = NULL;
        g_servo_enabled.store(false, std::memory_order_relaxed);
    }
}

static void cleanup_servo()
{
    if (g_servo != NULL) {
        fprintf(stderr, "Cleaning up servo...\n");
        servo_destroy(g_servo);
        if (!SERVO_USE_PCA9685) {
            servo_library_cleanup();
        }
        g_servo = NULL;
        g_servo_enabled.store(false, std::memory_order_relaxed);
    }
}

static void init_logger()
{
    fprintf(stderr, "Initializing Google Sheets Logger...\n");
    
    try {
        g_sheets_logger = new GoogleSheetsLogger(GOOGLE_SHEETS_DEPLOYMENT_URL);
        
        // Get patient info from UI (name, age, gender)
        get_patient_info_from_ui();
        g_sheets_logger->set_patient_name(g_patient_name);
        g_sheets_logger->set_patient_age(g_patient_age);
        g_sheets_logger->set_patient_gender(g_patient_gender);
        
        fprintf(stderr, "✓ Logger initialized successfully\n");
    } catch (const std::exception& e) {
        fprintf(stderr, "Error initializing logger: %s\n", e.what());
        g_sheets_logger = NULL;
    }
}

static void cleanup_logger()
{
    if (g_sheets_logger != NULL) {
        fprintf(stderr, "Cleaning up Google Sheets Logger...\n");
        g_sheets_logger->flush();  // Wait for pending logs
        delete g_sheets_logger;
        g_sheets_logger = NULL;
    }
}

static void start_dht12_thread()
{
    std::thread([]{
        Dht12I2C dht(1, 0x5c);
        double last_t = 27.0, last_h = 50.0;

        while (true) {
            auto r = dht.read_once();
            bool ok = false;

            if (r) {
                const double t = r->temp_c;
                const double h = r->hum_rh;

                // sanity check (lọc lỗi 0/0 + dải hợp lệ)
                if (!(t == 0.0 && h == 0.0) &&
                    (t > -40.0 && t < 85.0) &&
                    (h >= 0.0  && h <= 100.0))
                {
                    last_t = t;
                    last_h = h;
                    ok = true;
                }
            }

            // publish "last known good" để UI luôn có số
            g_Ta.store(last_t, std::memory_order_relaxed);
            g_RH.store(last_h, std::memory_order_relaxed);
            g_dht_ok.store(ok, std::memory_order_relaxed);
            if (ok) g_dht_seq.fetch_add(1, std::memory_order_relaxed);

            usleep(500000); // 0.5s/lần là thoải mái cho DHT12
        }
    }).detach();
}

void on_mouse(int event, int x, int y, int flags, void* userdata)
{
    (void)flags; (void)userdata;

    // bỏ qua nếu click vào panel bên phải
    const int availW = g_canvas_img_w;
    if (x < 0 || x >= availW) return;

    // (khuyến nghị) nếu bạn có lưu availH, bật check này để không nhận click ngoài vùng ảnh
    // const int availH = g_view_availH.load(std::memory_order_relaxed);
    // if (y < 0 || y >= availH) return;

    // ---- MAP window coords -> source thermal coords (160x120) ----
    const double s = g_view_s.load(std::memory_order_relaxed);
    const int cx   = g_view_cx.load(std::memory_order_relaxed);
    const int cy   = g_view_cy.load(std::memory_order_relaxed);

    if (s <= 1e-9) return;

    int px = (int)std::floor((x + cx) / s);
    int py = (int)std::floor((y + cy) / s);

    // clamp về thermal image 160x120
    px = clampi(px, 0, 159);
    py = clampi(py, 0, 119);

    if (event == cv::EVENT_LBUTTONDOWN) {
        g_selecting = true;
        g_roi_valid = false;

        g_x0 = px; g_y0 = py;
        g_x1 = px; g_y1 = py;

        // Mark mouse position for potential spot measurement
        g_mouse_x = px;
        g_mouse_y = py;
        g_mouse_click = false;  // Don't set to true yet - wait for LBUTTONUP
    }
    else if (event == cv::EVENT_MOUSEMOVE && g_selecting) {
        g_x1 = px;
        g_y1 = py;
    }
    else if (event == cv::EVENT_LBUTTONUP) {
        g_selecting = false;

        g_x1 = px;
        g_y1 = py;

        // Determine if this is a spot click or ROI drag
        if (std::abs(g_x1 - g_x0) > 2 && std::abs(g_y1 - g_y0) > 2) {
            // ROI detected
            g_roi_valid = true;
            g_mouse_click = false;
            g_measurement_pending = true;
            g_pending_measurement_type = "roi";
        } else {
            // Spot click detected
            g_mouse_click = true;
            g_roi_valid = false;
            g_measurement_pending = true;
            g_pending_measurement_type = "spot";
        }
    }
}
 
 void print_format(struct v4l2_format*vid_format) {
  printf("     vid_format->type                =%d\n",     vid_format->type );
  printf("     vid_format->fmt.pix.width       =%d\n",     vid_format->fmt.pix.width );
  printf("     vid_format->fmt.pix.height      =%d\n",     vid_format->fmt.pix.height );
  printf("     vid_format->fmt.pix.pixelformat =%d\n",     vid_format->fmt.pix.pixelformat);
  printf("     vid_format->fmt.pix.sizeimage   =%u\n",     vid_format->fmt.pix.sizeimage );
  printf("     vid_format->fmt.pix.field       =%d\n",     vid_format->fmt.pix.field );
  printf("     vid_format->fmt.pix.bytesperline=%d\n",     vid_format->fmt.pix.bytesperline );
  printf("     vid_format->fmt.pix.colorspace  =%d\n",     vid_format->fmt.pix.colorspace );
}

//#include "font.h" 
#include "font5x7.h" 
void font_write(unsigned char *fb, int x, int y, const char *string)
{
    // Draw 5x7 font horizontally. Transparent background: only set pixels where font bit==1
    while (*string) {
        int ch = ((*string) & 0x7F) - CHAR_OFFSET;
        if (ch >= 0) {
            for (int ry = 0; ry < 5; ++ry) {
                for (int rx = 0; rx < 7; ++rx) {
                    int v = (font5x7_basic[ch][ry] >> rx) & 1;
                    int px = x + rx;
                    int py = y + ry;
                    if (px < 0 || px >= 160 || py < 0 || py >= 128) continue;
                    int idx = py * 160 + px;
                    if (v) {
                        fb[idx] = 0; // draw black pixel for font
                    }
                }
            }
        }
        ++string;
        x += 6;
    }
}

static inline double raw2temperature_core(unsigned short RAW, double T_refl_c, double emissivity, double raw_scale)
{
    // giữ đúng hệ số gốc
    double raw = (double)RAW * raw_scale;

    const double RAWrefl =
        PlanckR1 / (PlanckR2 * (exp(PlanckB / (T_refl_c + 273.15)) - PlanckF)) - PlanckO;

    if (emissivity < 1e-6) emissivity = 1e-6;

    const double RAWobj = (raw - (1.0 - emissivity) * RAWrefl) / emissivity;

    return PlanckB / log(PlanckR1 / (PlanckR2 * (RAWobj + PlanckO)) + PlanckF) - 273.15;
}

double raw2temperature(unsigned short RAW, double T_refl_c, double emissivity /*=0.98*/, double raw_scale /*=4.0*/, double temp_offset /*=0.0*/)
{
    return raw2temperature_core(RAW, T_refl_c, emissivity, raw_scale) + temp_offset;
}



void startv4l2()
{
     int ret_code = 0;

//open video_device1
     printf("using output device: %s\n", video_device1);
     
     fdwr1 = open(video_device1, O_RDWR);
     assert(fdwr1 >= 0);

     ret_code = ioctl(fdwr1, VIDIOC_QUERYCAP, &vid_caps1);
     assert(ret_code != -1);

     memset(&vid_format1, 0, sizeof(vid_format1));

     ret_code = ioctl(fdwr1, VIDIOC_G_FMT, &vid_format1);

     linewidth1=FRAME_WIDTH1;
     framesize1=FRAME_WIDTH1*FRAME_HEIGHT1*1; // 8 Bit ??

     vid_format1.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
     vid_format1.fmt.pix.width = FRAME_WIDTH1;
     vid_format1.fmt.pix.height = FRAME_HEIGHT1;
     vid_format1.fmt.pix.pixelformat = FRAME_FORMAT1;
     vid_format1.fmt.pix.sizeimage = framesize1;
     vid_format1.fmt.pix.field = V4L2_FIELD_NONE;
     vid_format1.fmt.pix.bytesperline = linewidth1;
     vid_format1.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;

     // set data format
     ret_code = ioctl(fdwr1, VIDIOC_S_FMT, &vid_format1);
     assert(ret_code != -1);

     print_format(&vid_format1);


//open video_device2
     printf("using output device: %s\n", video_device2);
     
     fdwr2 = open(video_device2, O_RDWR);
     assert(fdwr2 >= 0);

     ret_code = ioctl(fdwr2, VIDIOC_QUERYCAP, &vid_caps2);
     assert(ret_code != -1);

     memset(&vid_format2, 0, sizeof(vid_format2));

     ret_code = ioctl(fdwr2, VIDIOC_G_FMT, &vid_format2);

     linewidth2=FRAME_WIDTH2;
     framesize2=FRAME_WIDTH2*FRAME_HEIGHT2*3; // 8x8x8 Bit

     vid_format2.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
     vid_format2.fmt.pix.width = FRAME_WIDTH2;
     vid_format2.fmt.pix.height = FRAME_HEIGHT2;
     vid_format2.fmt.pix.pixelformat = FRAME_FORMAT2;
     vid_format2.fmt.pix.sizeimage = framesize2;
     vid_format2.fmt.pix.field = V4L2_FIELD_NONE;
     vid_format2.fmt.pix.bytesperline = linewidth2;
     vid_format2.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;

     // set data format
     ret_code = ioctl(fdwr2, VIDIOC_S_FMT, &vid_format2);
     assert(ret_code != -1);

     print_format(&vid_format2);
}


// unused
void closev4l2()
{
//     close(fdwr0);
     close(fdwr1);
     close(fdwr2);

}

void vframe(char ep[], char EP_error[], int r, int actual_length, unsigned char buf[], unsigned char *colormap)
{
    (void)ep; (void)EP_error; (void)r; // giữ signature cũ

    // ===================== 1) Lấy Ta/RH (DHT12 thread publish) =====================
    // FIX: use memory_order_relaxed — no mutex/sync needed, avoids unnecessary memory barriers
    const double Ta = g_Ta.load(std::memory_order_relaxed);
    const double RH = g_RH.load(std::memory_order_relaxed);
    const bool dht_ok = g_dht_ok.load(std::memory_order_relaxed);

    // ===================== 2) Tunables (clamped) =====================
    double emissivity = clampd(g_tune.emissivity, g_tune.emiss_min, g_tune.emiss_max);
    double reflOff    = clampd(g_tune.reflOff,    g_tune.refl_min,  g_tune.refl_max);
    double rawScale   = clampd(g_tune.rawScale,   g_tune.scale_min, g_tune.scale_max);
    double tempOff    = clampd(g_tune.tempOff,    g_tune.toff_min,  g_tune.toff_max);

    g_tune.emissivity = emissivity;
    g_tune.reflOff    = reflOff;
    g_tune.rawScale   = rawScale;
    g_tune.tempOff    = tempOff;

    const double T_refl = Ta + reflOff;

    // ===================== 3) Assemble EP 0x85 chunks into a full frame =====================
    static const unsigned char magicbyte[4] = {0xEF, 0xBE, 0x00, 0x00};

    if ((actual_length >= 4 && memcmp(buf, magicbyte, 4) == 0) ||
        (g_buf85pointer + actual_length) >= BUF85SIZE)
    {
        g_buf85pointer = 0;
    }

    if (actual_length > 0) {
        memcpy(g_buf85 + g_buf85pointer, buf, (size_t)actual_length);
        g_buf85pointer += actual_length;
    }

    if (g_buf85pointer < 4 || memcmp(g_buf85, magicbyte, 4) != 0) {
        g_buf85pointer = 0;
        return;
    }

    if (g_buf85pointer < 28) return;

    auto u32le = [&](int off)->uint32_t {
        return (uint32_t)g_buf85[off] |
               ((uint32_t)g_buf85[off+1] << 8) |
               ((uint32_t)g_buf85[off+2] << 16) |
               ((uint32_t)g_buf85[off+3] << 24);
    };

    const uint32_t FrameSize  = u32le(8);
    const uint32_t NeedBytes = FrameSize + 28;
    if (NeedBytes > (uint32_t)BUF85SIZE) { g_buf85pointer = 0; return; }
    if ((uint32_t)g_buf85pointer < NeedBytes) return;

    g_buf85pointer = 0;

    // ===================== 4) Extract thermal RAW from assembled frame =====================
    bool have_thermal = true;
    for (int y = 0; y < 120; ++y) {
        for (int x = 0; x < 160; ++x) {
            int idx;
            if (x < 80) idx = 2 * (y * 164 + x) + 32;
            else        idx = 2 * (y * 164 + x) + 32 + 4;
            if (idx + 1 >= (int)NeedBytes) { have_thermal = false; break; }
            unsigned short v = (unsigned short)(g_buf85[idx] | (g_buf85[idx+1] << 8));
            g_pix[y * 160 + x] = v;
        }
    }
    if (!have_thermal) return;

    // ===================== 5) Min/Max/MaxPoint =====================
    int minv = 65535, maxv = 0;
    int maxx = 0, maxy = 0;
    for (int y = 0; y < 120; ++y) {
        for (int x = 0; x < 160; ++x) {
            int v = (int)g_pix[y * 160 + x];
            if (v < minv) minv = v;
            if (v > maxv) { maxv = v; maxx = x; maxy = y; }
        }
    }
    if (minv >= maxv) { maxv = minv + 1; }

    // ===================== 5) Temperature LUT (fill quanh min/max) =====================
    static std::vector<float> tempLUT(65536);
    static bool lut_init = false;
    static double lut_Trefl = 1e9, lut_eps = 0.98, lut_scale = 4.0, lut_to = 0.0;
    static int lut_lo = 65535, lut_hi = 0;

    int want_lo = std::max(0,     minv - 512);
    int want_hi = std::min(65535, maxv + 512);

    if (!lut_init ||
        std::fabs(lut_Trefl - T_refl) > 0.05 ||
        std::fabs(lut_eps   - emissivity) > 1e-9 ||
        std::fabs(lut_scale - rawScale)   > 1e-9 ||
        std::fabs(lut_to    - tempOff)    > 1e-9)
    {
        lut_init = true;
        lut_Trefl = T_refl; lut_eps = emissivity; lut_scale = rawScale; lut_to = tempOff;
        lut_lo = 65535; lut_hi = 0;
    }

    if (want_lo < lut_lo) {
        for (int raw = want_lo; raw < lut_lo; ++raw) {
            tempLUT[raw] = (float)(raw2temperature_core((unsigned short)raw, T_refl, emissivity, rawScale) + tempOff);
        }
        lut_lo = want_lo;
    }
    if (want_hi > lut_hi) {
        for (int raw = lut_hi + 1; raw <= want_hi; ++raw) {
            tempLUT[raw] = (float)(raw2temperature_core((unsigned short)raw, T_refl, emissivity, rawScale) + tempOff);
        }
        lut_hi = want_hi;
    }

    // ===================== 6) RAW -> 8-bit gray (160x128) + colorize =====================
    static unsigned char fb_proc[160 * 128];
    static unsigned char fb_proc2[160 * 128 * 3];

    int delta = maxv - minv;
    if (delta <= 0) delta = 1;
    int scale = 0x10000 / delta;

    for (int y = 0; y < 120; ++y) {
        for (int x = 0; x < 160; ++x) {
            int vv = (((int)g_pix[y * 160 + x] - minv) * scale) >> 8;
            vv = clampi(vv, 0, 255);
            fb_proc[y * 160 + x] = (unsigned char)vv;
        }
    }
    for (int y = 120; y < 128; ++y) {
        memcpy(&fb_proc[y * 160], &fb_proc[(119) * 160], 160);
    }

    // FIX: precompute dst/src pointers instead of computing 3*(y*160+x) three times per pixel
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 160; ++x) {
            const int vv = fb_proc[y * 160 + x];
            unsigned char* dst       = &fb_proc2[3 * (y * 160 + x)];
            const unsigned char* src = &colormap[3 * vv];
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
        }
    }

    // ===================== 7) Overlay (legacy 8-bit font) =====================
    {
        time_t now1 = time(NULL);
        struct tm *loctime = localtime(&now1);

        char st_time[64];
        char st_line[128];
        strftime(st_time, sizeof(st_time), "%H:%M:%S", loctime);

        int med_raw = (g_pix[59 * 160 + 79] + g_pix[59 * 160 + 80] + g_pix[60 * 160 + 79] + g_pix[60 * 160 + 80]) / 4;

        snprintf(st_line, sizeof(st_line),
                 " %s Ta=%.1f RH=%.0f%% %s  %.1f/%.1f/%.1fC",
                 st_time, Ta, RH, dht_ok ? "OK" : "--",
                 tempLUT[(unsigned short)minv],
                 tempLUT[(unsigned short)med_raw],
                 tempLUT[(unsigned short)maxv]);

        constexpr int OVERLAY_MAX = 26;
        char st2[OVERLAY_MAX];
        strncpy(st2, st_line, OVERLAY_MAX - 1);
        st2[OVERLAY_MAX - 1] = '\0';

        font_write(fb_proc, 1, 120, st2);
        font_write(fb_proc, 80 - 2, 60 - 3, "+");

        int mx = clampi(maxx - 4, 0, 150);
        int my = clampi(maxy - 4, 0, 110);
        font_write(fb_proc, 160 - 6, my, "<");
        font_write(fb_proc, mx, 120 - 8, "|");
    }

    // ===================== 8) OpenCV view + side panel (COVER mode) =====================
    cv::Mat rgb(128, 160, CV_8UC3, fb_proc2);
    cv::Mat bgr;
    cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);

    static bool win_init = false;
    static const int PANEL_W = 380;
    static const int TARGET_W = 1280;
    static const int TARGET_H = 720;
    const char* WIN = "FLIRONE";

    if (!win_init) {
        cv::namedWindow(WIN, cv::WINDOW_NORMAL);
        cv::setMouseCallback(WIN, on_mouse, nullptr);
        cv::resizeWindow(WIN, TARGET_W, TARGET_H);
        win_init = true;
    }

    const int winW = TARGET_W;
    const int winH = TARGET_H;

    const int availW = std::max(1, winW - PANEL_W);
    const int availH = std::max(1, winH);

    // COVER scale
    double sx = (double)availW / (double)bgr.cols;
    double sy = (double)availH / (double)bgr.rows;
    double s  = std::max(sx, sy);

    cv::Mat bgr_scaled;
    cv::resize(bgr, bgr_scaled,
               cv::Size((int)std::round(bgr.cols * s), (int)std::round(bgr.rows * s)),
               0, 0, cv::INTER_NEAREST);

    // crop center
    int cx = std::max(0, (bgr_scaled.cols - availW) / 2);
    int cy = std::max(0, (bgr_scaled.rows - availH) / 2);

    g_view_s.store(s, std::memory_order_relaxed);
    g_view_cx.store(cx, std::memory_order_relaxed);
    g_view_cy.store(cy, std::memory_order_relaxed);
    
    cv::Rect cropR(cx, cy,
                   std::min(availW, bgr_scaled.cols - cx),
                   std::min(availH, bgr_scaled.rows - cy));
    cv::Mat bgr_crop = bgr_scaled(cropR);

    cv::Mat canvas(winH, winW, CV_8UC3, rgba(10,10,10));
    bgr_crop.copyTo(canvas(cv::Rect(0, 0, bgr_crop.cols, bgr_crop.rows)));

    // ===== Spot click overlay (draw on canvas) =====
    if (g_mouse_click &&
        g_mouse_x >= 0 && g_mouse_x < 160 &&
        g_mouse_y >= 0 && g_mouse_y < 120)
    {
        unsigned short raw = g_pix[g_mouse_y * 160 + g_mouse_x];
        double temp = tempLUT[raw];

        // src -> window coords (COVER): wx = src*s - cx, wy = src*s - cy
        int wx = (int)std::lround(g_mouse_x * s - cx);
        int wy = (int)std::lround(g_mouse_y * s - cy);
        wx = clampi(wx, 0, availW - 1);
        wy = clampi(wy, 0, availH - 1);

        char tbuf[64];
        snprintf(tbuf, sizeof(tbuf), "%.2f C", temp);

        cv::circle(canvas, cv::Point(wx, wy), 8, cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
        cv::putText(canvas, tbuf,
                    cv::Point(wx + 10, std::max(18, wy - 10)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
    }

    // ===== ROI avg overlay (draw on canvas) =====
    if (g_roi_valid) {
        int x0 = clampi(std::min(g_x0, g_x1), 0, 159);
        int x1 = clampi(std::max(g_x0, g_x1), 0, 159);
        int y0 = clampi(std::min(g_y0, g_y1), 0, 119);
        int y1 = clampi(std::max(g_y0, g_y1), 0, 119);

        double sum = 0.0;
        int count = 0;
        for (int yy = y0; yy <= y1; ++yy)
            for (int xx = x0; xx <= x1; ++xx) {
                sum += tempLUT[g_pix[yy * 160 + xx]];
                count++;
            }

        if (count > 0) {
            double avg = sum / count;

            int wx0 = (int)std::lround(x0 * s - cx);
            int wy0 = (int)std::lround(y0 * s - cy);
            int wx1 = (int)std::lround((x1 + 1) * s - cx);
            int wy1 = (int)std::lround((y1 + 1) * s - cy);

            wx0 = clampi(wx0, 0, availW - 1);
            wy0 = clampi(wy0, 0, availH - 1);
            wx1 = clampi(wx1, 0, availW - 1);
            wy1 = clampi(wy1, 0, availH - 1);

            cv::rectangle(canvas, cv::Point(wx0, wy0), cv::Point(wx1, wy1),
                        cv::Scalar(0, 255, 0), 2, cv::LINE_AA);

            char abuf[64];
            snprintf(abuf, sizeof(abuf), "AVG: %.2f C", avg);
            cv::putText(canvas, abuf,
                        cv::Point(wx0 + 6, std::max(18, wy0 - 8)),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
        }
    }
    g_canvas_img_w = availW;

    draw_side_panel_modern(
        canvas,
        g_canvas_img_w, PANEL_W,
        Ta, RH, dht_ok,
        g_sel_param,
        emissivity, reflOff, rawScale, tempOff,
        g_servo_angle.load(std::memory_order_relaxed),
        g_servo_enabled.load(std::memory_order_relaxed)
    );

    // ===================== Persistent Measurement Display (2 seconds) =====================
    // Display measurement result for 2 seconds after measurement is taken
    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - g_last_measurement_time
    ).count();
    
    if (elapsed_ms < MEASUREMENT_DISPLAY_DURATION_MS && !g_last_display_type.empty()) {
        char result_text[64];
        cv::Scalar color;
        
        if (g_last_display_type == "spot") {
            snprintf(result_text, sizeof(result_text), "SPOT: %.2f do C", g_last_spot_temp);
            color = cv::Scalar(0, 180, 255);  // Cyan
        } else {
            snprintf(result_text, sizeof(result_text), "ROI AVG: %.2f do C", g_last_roi_avg_temp);
            color = cv::Scalar(0, 255, 0);    // Green
        }
        
        // Draw result with semi-transparent background
        int text_x = availW / 2 - 80;
        int text_y = 50;
        
        cv::putText(canvas, result_text, cv::Point(text_x, text_y),
                    cv::FONT_HERSHEY_SIMPLEX, 1.2, color, 3, cv::LINE_AA);
        
        // Draw countdown bar
        int bar_width = 200;
        int bar_x = availW / 2 - bar_width / 2;
        int bar_y = text_y + 30;
        
        // Background bar
        cv::rectangle(canvas, cv::Point(bar_x, bar_y), cv::Point(bar_x + bar_width, bar_y + 8),
                     cv::Scalar(50, 50, 50), cv::FILLED, cv::LINE_AA);
        
        // Foreground bar (remaining time)
        int remaining_width = (int)(bar_width * (1.0 - (double)elapsed_ms / MEASUREMENT_DISPLAY_DURATION_MS));
        cv::rectangle(canvas, cv::Point(bar_x, bar_y), cv::Point(bar_x + remaining_width, bar_y + 8),
                     color, cv::FILLED, cv::LINE_AA);
    }

    cv::imshow(WIN, canvas);

    // ===================== 8.5) Screenshot capture & logging on measurement =====================
    // Only process when measurement is pending and ready
    if (g_measurement_pending) {
        g_measurement_pending = false;
        g_last_measurement_time = std::chrono::steady_clock::now();  // Record time

        if (g_pending_measurement_type == "spot") {
            save_screenshot(canvas, "spot");
            
            // Get spot temperature for display
            if (g_mouse_x >= 0 && g_mouse_x < 160 && g_mouse_y >= 0 && g_mouse_y < 120) {
                unsigned short raw = g_pix[g_mouse_y * 160 + g_mouse_x];
                g_last_spot_temp = tempLUT[raw];
                g_last_display_type = "spot";
            }
            
            // Log to Google Sheets
            if (g_sheets_logger != NULL && g_mouse_x >= 0 && g_mouse_x < 160 && 
                g_mouse_y >= 0 && g_mouse_y < 120) {
                unsigned short raw = g_pix[g_mouse_y * 160 + g_mouse_x];
                double temp = tempLUT[raw];
                
                g_sheets_logger->log_measurement(
                    "spot",
                    temp,
                    emissivity, reflOff, rawScale, tempOff,
                    Ta, RH
                );
            }
        } else if (g_pending_measurement_type == "roi") {
            save_screenshot(canvas, "roi");
            
            // Log to Google Sheets
            if (g_sheets_logger != NULL) {
                int x0 = clampi(std::min(g_x0, g_x1), 0, 159);
                int x1 = clampi(std::max(g_x0, g_x1), 0, 159);
                int y0 = clampi(std::min(g_y0, g_y1), 0, 119);
                int y1 = clampi(std::max(g_y0, g_y1), 0, 119);
                
                double sum = 0.0;
                int count = 0;
                for (int yy = y0; yy <= y1; ++yy)
                    for (int xx = x0; xx <= x1; ++xx) {
                        sum += tempLUT[g_pix[yy * 160 + xx]];
                        count++;
                    }
                
                if (count > 0) {
                    double avg = sum / count;
                    g_last_roi_avg_temp = avg;
                    g_last_display_type = "roi";
                    
                    g_sheets_logger->log_measurement(
                        "roi",
                        avg,
                        emissivity, reflOff, rawScale, tempOff,
                        Ta, RH
                    );
                }
            }
        }
        
        // Clear flags
        g_mouse_click = false;
        g_roi_valid = false;
    }

    // ===================== 9) Keys =====================
    int key = cv::waitKeyEx(1);
    if (key == 27) exit(0);

    if (key >= 0) {
        fprintf(stderr, "Key pressed: %d (0x%X)\n", key, key);
    }

    if (key == '1') g_sel_param = 0;
    if (key == '2') g_sel_param = 1;
    if (key == '3') g_sel_param = 2;
    if (key == '4') g_sel_param = 3;

    if (key == '0') g_tune = Tunables{};

    auto apply_delta = [&](int dir) {
        if (g_sel_param == 0) {
            g_tune.emissivity = clampd(g_tune.emissivity + dir * 0.01, g_tune.emiss_min, g_tune.emiss_max);
        } else if (g_sel_param == 1) {
            g_tune.reflOff    = clampd(g_tune.reflOff    + dir * 0.10, g_tune.refl_min,  g_tune.refl_max);
        } else if (g_sel_param == 2) {
            g_tune.rawScale   = clampd(g_tune.rawScale   + dir * 0.05, g_tune.scale_min, g_tune.scale_max);
        } else if (g_sel_param == 3) {
            g_tune.tempOff    = clampd(g_tune.tempOff    + dir * 0.10, g_tune.toff_min,  g_tune.toff_max);
        }
    };

    const int KEY_PLUS  = 61;
    const int KEY_MINUS = 45;

    if (key == KEY_PLUS)  apply_delta(+1);
    if (key == KEY_MINUS) apply_delta(-1);

    // ===================== Servo control: ',' = decrease, '.' = increase =====================
    const int KEY_SERVO_DEC = 44;
    const int KEY_SERVO_INC = 46;

    if (key == KEY_SERVO_DEC || key == KEY_SERVO_INC) {
        if (g_servo == NULL) {
            fprintf(stderr, "Warning: Servo not initialized\n");
        } else {
            double current_angle = g_servo_angle.load(std::memory_order_relaxed);
            double deltaA = (key == KEY_SERVO_DEC) ? -SERVO_ANGLE_STEP : +SERVO_ANGLE_STEP;
            double new_angle = clampd(current_angle + deltaA, 0.0, 180.0);

            if (servo_set_angle(g_servo, new_angle) == 0) {
                g_servo_angle.store(new_angle, std::memory_order_relaxed);
                double disp = servo_actual_to_display(new_angle);
                fprintf(stderr, "Servo angle set to %.1f deg display (actual %.1f)\n", disp, new_angle);
            } else {
                fprintf(stderr, "Error: Failed to set servo angle\n");
            }
        }
    }
}
 static int find_lvr_flirusb(void)
 {
 	devh = libusb_open_device_with_vid_pid(NULL, VENDOR_ID, PRODUCT_ID);
 	return devh ? 0 : -EIO;
 }
 
 void print_bulk_result(char ep[],char EP_error[], int r, int actual_length, unsigned char buf[])
 {
         time_t now1;
         int i;

         now1 = time(NULL);
         // FIX: ctime() uses a static internal buffer — not thread-safe.
         // Use ctime_r() with a local buffer instead.
         char timebuf[64];
         ctime_r(&now1, timebuf);

         if (r < 0) {
                if (strcmp (EP_error, libusb_error_name(r))!=0)
                {       
                    strcpy(EP_error, libusb_error_name(r));
                    fprintf(stderr, "\n: %s >>>>>>>>>>>>>>>>>bulk transfer (in) %s:%i %s\n", timebuf, ep , r, libusb_error_name(r));
                    sleep(1);
                }
                //return 1;
        } else
        {           
            printf("\n: %s bulk read EP %s, actual length %d\nHEX:\n", timebuf, ep ,actual_length);

            for (i = 0; i <  (((200)<(actual_length))?(200):(actual_length)); i++) {
                    printf(" %02x", buf[i]);
            }
                 
            printf("\nSTRING:\n");	
            for (i = 0; i <  (((200)<(actual_length))?(200):(actual_length)); i++) {
                    if(buf[i]>31) {printf("%c", buf[i]);}
            }
            printf("\n");	
            
        } 
 }       

int EPloop(unsigned char *colormap)
{
    // ===== variables MUST be at top for C++ (goto-safe) =====
    int i = 0;
    int r = 1;
    int state = 1;
    int ct = 0;

    // FIX: was 1MB on stack — risk of stack overflow on ARM/embedded.
    // Now static: safe, same lifetime as before (EPloop is called in a loop from main).
    static unsigned char buf[1048576];
    int actual_length = 0;

    time_t now;
    unsigned char data[2] = {0, 0};

    char EP85_error[50] = "";

    // ========================================================

    r = libusb_init(NULL);
    if (r < 0) {
        fprintf(stderr, "failed to initialise libusb\n");
        return r;
    }

    r = find_lvr_flirusb();
    if (r < 0) {
        fprintf(stderr, "Could not find/open device\n");
        goto out;
    }
    printf("Successfully find the Flir One G2 device\n");

    r = libusb_set_configuration(devh, 3);
    if (r < 0) {
        fprintf(stderr, "libusb_set_configuration error %d\n", r);
        goto out;
    }
    printf("Successfully set usb configuration 3\n");

    r = libusb_claim_interface(devh, 0);
    if (r < 0) {
        fprintf(stderr, "libusb_claim_interface 0 error %d\n", r);
        goto out;
    }

    r = libusb_claim_interface(devh, 1);
    if (r < 0) {
        fprintf(stderr, "libusb_claim_interface 1 error %d\n", r);
        goto out;
    }

    r = libusb_claim_interface(devh, 2);
    if (r < 0) {
        fprintf(stderr, "libusb_claim_interface 2 error %d\n", r);
        goto out;
    }

    printf("Successfully claimed interface 0,1,2\n");

    // ======================= MAIN LOOP =======================
    while (1)
    {
        switch (state)
        {
        case 1: {
            printf("stop interface 2 FRAME\n");
            r = libusb_control_transfer(devh, 1, 0x0b, 0, 2, data, 0, 100);
            if (r < 0) goto out;

            printf("stop interface 1 FILEIO\n");
            r = libusb_control_transfer(devh, 1, 0x0b, 0, 1, data, 0, 100);
            if (r < 0) goto out;

            printf("start interface 1 FILEIO\n");
            r = libusb_control_transfer(devh, 1, 0x0b, 1, 1, data, 0, 100);
            if (r < 0) goto out;

            now = time(NULL);
            // FIX: ctime_r — thread-safe version
            { char _tb[64]; ctime_r(&now, _tb); printf("\n: %s", _tb); }

            state = 3;
            break;
        }

        case 2: {
            printf("\nask for CameraFiles.zip on EP 0x83\n");

            int transferred = 0;
            char my_string[128];
            unsigned char *my_string1;

            unsigned char my_string2[16] = {
                0xcc,0x01,0x00,0x00,0x01,0x00,0x00,0x00,
                0x41,0x00,0x00,0x00,0xF8,0xB3,0xF7,0x00
            };

            int length = 16;
            r = libusb_bulk_transfer(devh, 2, my_string2, length, &transferred, 0);

            strcpy(my_string, "{\"type\":\"openFile\",\"data\":{\"mode\":\"r\",\"path\":\"CameraFiles.zip\"}}");
            my_string1 = (unsigned char*)my_string;
            length = strlen(my_string) + 1;
            r = libusb_bulk_transfer(devh, 2, my_string1, length, &transferred, 0);

            unsigned char my_string3[16] = {
                0xcc,0x01,0x00,0x00,0x01,0x00,0x00,0x00,
                0x33,0x00,0x00,0x00,0xef,0xdb,0xc1,0xc1
            };

            r = libusb_bulk_transfer(devh, 2, my_string3, 16, &transferred, 0);

            strcpy(my_string, "{\"type\":\"readFile\",\"data\":{\"streamIdentifier\":10}}");
            my_string1 = (unsigned char*)my_string;
            length = strlen(my_string) + 1;
            r = libusb_bulk_transfer(devh, 2, my_string1, length, &transferred, 0);

            state = 3;
            break;
        }

        case 3: {
            printf("\nAsk for video stream, start EP 0x85\n");
            r = libusb_control_transfer(devh, 1, 0x0b, 1, 2, data, 2, 200);
            if (r < 0) goto out;

            state = 4;
            break;
        }

        case 4: {
            r = libusb_bulk_transfer(devh, 0x85, buf, sizeof(buf), &actual_length, 100);
            if (actual_length > 0)
                vframe((char*)"0x85", EP85_error, r, actual_length, buf, colormap);
            break;
        }
        }

        r = libusb_bulk_transfer(devh, 0x81, buf, sizeof(buf), &actual_length, 10);
        r = libusb_bulk_transfer(devh, 0x83, buf, sizeof(buf), &actual_length, 10);
        // FIX: use integer comparison instead of strcmp(libusb_error_name(...)) — faster, correct
        if (r == LIBUSB_ERROR_NO_DEVICE) {
            fprintf(stderr, "USB disconnected\n");
            goto out;
        }
    }

out:
    if (devh) {
        libusb_release_interface(devh, 0);
        libusb_release_interface(devh, 1);
        libusb_release_interface(devh, 2);
        libusb_reset_device(devh);
        libusb_close(devh);
        devh = NULL;
    }
    libusb_exit(NULL);
    return (r >= 0) ? r : -r;

}


int main(int argc, char **argv)
{
    unsigned char colormap[768];
    FILE *fp;

    if (argc < 2) {
        fprintf(stderr, "\nUsage: flirone palette.raw\n");
        return 1;
    }

    fp = fopen(argv[1], "rb");
    if (!fp) {
        perror("fopen palette");
        return 1;
    }
    fread(colormap, sizeof(unsigned char), 768, fp);  // read 256 rgb values
    fclose(fp);

    // DHT12 chạy nền, publish Ta/RH qua atomics (không block luồng frame)
    start_dht12_thread();

    // Initialize servo motor control
    init_servo();

    // Initialize Google Sheets Logger
    init_logger();

    while (1) {
        EPloop(colormap);
    }

    // Cleanup (only reached if EPloop exits, which shouldn't happen normally)
    cleanup_logger();
    cleanup_servo();

    return 0;
}