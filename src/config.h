#pragma once
#include <string>

struct AppConfig {
    // Web server
    int web_port = 8080;
    std::string web_host = "0.0.0.0";
    std::string web_root = "./web";

    // Google Sheets
    std::string google_sheets_url;

    // Servo
    bool servo_use_pca9685 = true;
    int servo_channel = 0;
    int servo_i2c_bus = 1;
    int servo_i2c_addr = 0x40;
    double servo_pwm_freq = 50.0;
    int servo_min_pulse = 1000;
    int servo_max_pulse = 2000;
    double servo_initial_angle = 90.0;
    double servo_angle_step = 10.0;   // internal servo degrees per step

    // Servo display mapping (actual → display)
    double servo_display_min = -45.0;
    double servo_display_max = 45.0;
    double servo_display_step = 5.0;

    // DHT12
    int dht12_i2c_bus = 1;
    int dht12_i2c_addr = 0x5c;

    // Thermal defaults
    double default_emissivity = 0.98;
    double default_raw_scale = 4.0;
    double default_refl_offset = 0.0;
    double default_temp_offset = 0.0;

    // Palette
    std::string palette_path = "../palettes/Iron2.raw";

    // Screenshots
    std::string screenshots_dir = "screenshots";

    // USB
    int usb_vendor_id = 0x09cb;
    int usb_product_id = 0x1996;
};

// Load config from JSON file. Returns defaults if file doesn't exist.
AppConfig load_config(const std::string& path);

// Save default config to a JSON file (for first run)
void save_default_config(const std::string& path);
