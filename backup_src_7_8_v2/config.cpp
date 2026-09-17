#include "config.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

// Helper: safely get nested JSON value
template<typename T>
static T jget(const json& j, const std::string& section, const std::string& key, T default_val) {
    try {
        if (j.contains(section) && j[section].contains(key))
            return j[section][key].get<T>();
    } catch (...) {}
    return default_val;
}

template<typename T>
static T jget_top(const json& j, const std::string& key, T default_val) {
    try {
        if (j.contains(key))
            return j[key].get<T>();
    } catch (...) {}
    return default_val;
}

AppConfig load_config(const std::string& path) {
    AppConfig cfg;
    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "Config file not found (" << path << "), using defaults." << std::endl;
            return cfg;
        }

        json j;
        file >> j;

        // Web server
        cfg.web_port = jget(j, "web_server", "port", cfg.web_port);
        cfg.web_host = jget<std::string>(j, "web_server", "host", cfg.web_host);
        cfg.web_root = jget<std::string>(j, "web_server", "web_root", cfg.web_root);

        // Google Sheets
        cfg.google_sheets_url = jget<std::string>(j, "google_sheets", "url", cfg.google_sheets_url);

        // Servo
        cfg.servo_use_pca9685 = jget(j, "servo", "use_pca9685", cfg.servo_use_pca9685);
        cfg.servo_channel     = jget(j, "servo", "channel", cfg.servo_channel);
        cfg.servo_i2c_bus     = jget(j, "servo", "i2c_bus", cfg.servo_i2c_bus);
        cfg.servo_i2c_addr    = jget(j, "servo", "i2c_addr", cfg.servo_i2c_addr);
        cfg.servo_pwm_freq    = jget(j, "servo", "pwm_freq", cfg.servo_pwm_freq);
        cfg.servo_min_pulse   = jget(j, "servo", "min_pulse", cfg.servo_min_pulse);
        cfg.servo_max_pulse   = jget(j, "servo", "max_pulse", cfg.servo_max_pulse);
        cfg.servo_initial_angle = jget(j, "servo", "initial_angle", cfg.servo_initial_angle);
        cfg.servo_angle_step  = jget(j, "servo", "angle_step", cfg.servo_angle_step);
        cfg.servo_display_min = jget(j, "servo", "display_min", cfg.servo_display_min);
        cfg.servo_display_max = jget(j, "servo", "display_max", cfg.servo_display_max);
        cfg.servo_display_step = jget(j, "servo", "display_step", cfg.servo_display_step);

        // DHT12
        cfg.dht12_i2c_bus  = jget(j, "dht12", "i2c_bus", cfg.dht12_i2c_bus);
        cfg.dht12_i2c_addr = jget(j, "dht12", "i2c_addr", cfg.dht12_i2c_addr);

        // Thermal defaults
        cfg.default_emissivity  = jget(j, "thermal", "default_emissivity", cfg.default_emissivity);
        cfg.default_raw_scale   = jget(j, "thermal", "default_raw_scale", cfg.default_raw_scale);
        cfg.default_refl_offset = jget(j, "thermal", "default_refl_offset", cfg.default_refl_offset);
        cfg.default_temp_offset = jget(j, "thermal", "default_temp_offset", cfg.default_temp_offset);

        // Top-level keys
        cfg.palette_path     = jget_top<std::string>(j, "palette_path", cfg.palette_path);
        cfg.screenshots_dir  = jget_top<std::string>(j, "screenshots_dir", cfg.screenshots_dir);

        // USB
        cfg.usb_vendor_id  = jget(j, "usb", "vendor_id", cfg.usb_vendor_id);
        cfg.usb_product_id = jget(j, "usb", "product_id", cfg.usb_product_id);

    } catch (const std::exception& e) {
        std::cerr << "Error loading config: " << e.what() << std::endl;
    }
    return cfg;
}

void save_default_config(const std::string& path) {
    AppConfig cfg;
    try {
        json j;
        j["web_server"]["port"]     = cfg.web_port;
        j["web_server"]["host"]     = cfg.web_host;
        j["web_server"]["web_root"] = cfg.web_root;

        j["google_sheets"]["url"]   = cfg.google_sheets_url;

        j["servo"]["use_pca9685"]   = cfg.servo_use_pca9685;
        j["servo"]["channel"]       = cfg.servo_channel;
        j["servo"]["i2c_bus"]       = cfg.servo_i2c_bus;
        j["servo"]["i2c_addr"]      = cfg.servo_i2c_addr;
        j["servo"]["pwm_freq"]      = cfg.servo_pwm_freq;
        j["servo"]["min_pulse"]     = cfg.servo_min_pulse;
        j["servo"]["max_pulse"]     = cfg.servo_max_pulse;
        j["servo"]["initial_angle"] = cfg.servo_initial_angle;
        j["servo"]["angle_step"]    = cfg.servo_angle_step;
        j["servo"]["display_min"]   = cfg.servo_display_min;
        j["servo"]["display_max"]   = cfg.servo_display_max;
        j["servo"]["display_step"]  = cfg.servo_display_step;

        j["dht12"]["i2c_bus"]       = cfg.dht12_i2c_bus;
        j["dht12"]["i2c_addr"]      = cfg.dht12_i2c_addr;

        j["thermal"]["default_emissivity"]  = cfg.default_emissivity;
        j["thermal"]["default_raw_scale"]   = cfg.default_raw_scale;
        j["thermal"]["default_refl_offset"] = cfg.default_refl_offset;
        j["thermal"]["default_temp_offset"] = cfg.default_temp_offset;

        j["palette_path"]    = cfg.palette_path;
        j["screenshots_dir"] = cfg.screenshots_dir;

        j["usb"]["vendor_id"]  = cfg.usb_vendor_id;
        j["usb"]["product_id"] = cfg.usb_product_id;

        std::ofstream file(path);
        if (file.is_open()) {
            file << j.dump(4);
            std::cerr << "Saved default config to " << path << std::endl;
        } else {
            std::cerr << "Failed to open config file for writing: " << path << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error saving default config: " << e.what() << std::endl;
    }
}
