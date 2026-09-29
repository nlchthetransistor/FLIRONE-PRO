#include "config.h"
#include "thermal_proc.h"
#include "usb_thermal.h"
#include "web_server.h"

#include <atomic>
#include <thread>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <mutex>
#include <iostream>
#include <unistd.h>

// Components
#include "dht12.hpp"
extern "C" { 
#include "components/servo/servo.h" 
}
#include "components/google_sheets/google_sheets_logger.hpp"

#include <nlohmann/json.hpp>

static std::atomic<bool> g_running{true};
static void signal_handler(int sig) {
    fprintf(stderr, "\nReceived signal %d, shutting down...\n", sig);
    g_running.store(false, std::memory_order_relaxed);
}

// Global state
static std::atomic<double> g_Ta{27.0};
static std::atomic<double> g_RH{50.0};
static std::atomic<bool>   g_dht_ok{false};

static Tunables g_tune;
static std::mutex g_tune_mutex;

static Servo* g_servo = nullptr;
static std::atomic<double> g_servo_angle{90.0};
static std::atomic<bool>   g_servo_enabled{false};

static GoogleSheetsLogger* g_logger = nullptr;

static std::string g_patient_name = "Unknown";
static std::string g_patient_age = "Unknown";
static std::string g_patient_gender = "Unknown";
static std::mutex g_patient_mutex;

static ProcessedFrame g_latest_frame;
static std::mutex g_frame_mutex;

static void dht12_thread_func(int bus, int addr) {
    Dht12I2C dht(bus, addr);
    while (g_running.load(std::memory_order_relaxed)) {
        auto r = dht.read_once();
        if (r && !(r->temp_c == 0 && r->hum_rh == 0) &&
            r->temp_c > -40 && r->temp_c < 85 &&
            r->hum_rh >= 0 && r->hum_rh <= 100) {
            g_Ta.store(r->temp_c, std::memory_order_relaxed);
            g_RH.store(r->hum_rh, std::memory_order_relaxed);
            g_dht_ok.store(true, std::memory_order_relaxed);
        } else {
            g_dht_ok.store(false, std::memory_order_relaxed);
        }
        for (int i = 0; i < 10 && g_running; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

static void handle_client_message(const std::string& msg, WebServer& server, const AppConfig& cfg) {
    try {
        auto j = nlohmann::json::parse(msg);
        std::string type = j.value("type", "");

        if (type == "spot") {
            int x = j.value("x", -1);
            int y = j.value("y", -1);
            if (x >= 0 && x < THERMAL_WIDTH && y >= 0 && y < THERMAL_HEIGHT) {
                ProcessedFrame pf;
                { std::lock_guard<std::mutex> lk(g_frame_mutex); pf = g_latest_frame; }
                
                double t = get_spot_temp(pf, x, y);
                
                std::string pname, page, pgender;
                {
                    std::lock_guard<std::mutex> lk(g_patient_mutex);
                    pname = g_patient_name;
                    page = g_patient_age;
                    pgender = g_patient_gender;
                }

                if (g_logger) g_logger->log_measurement("spot", t, g_tune.emissivity, g_tune.refl_offset, g_tune.raw_scale, g_tune.temp_offset, g_Ta.load(), g_RH.load());
                
                std::string fname = save_screenshot(pf.rgb, THERMAL_WIDTH, THERMAL_HEIGHT, cfg.screenshots_dir, pname, "spot");
                
                nlohmann::json res;
                res["type"] = "spot_result";
                res["temp"] = t;
                res["x"] = x;
                res["y"] = y;
                res["screenshot"] = fname;
                server.broadcast(res.dump());
            }
        }
        else if (type == "roi") {
            int x0 = j.value("x0", -1);
            int y0 = j.value("y0", -1);
            int x1 = j.value("x1", -1);
            int y1 = j.value("y1", -1);
            if (x0 >= 0 && x0 < THERMAL_WIDTH && y0 >= 0 && y0 < THERMAL_HEIGHT &&
                x1 >= 0 && x1 < THERMAL_WIDTH && y1 >= 0 && y1 < THERMAL_HEIGHT) {
                ProcessedFrame pf;
                { std::lock_guard<std::mutex> lk(g_frame_mutex); pf = g_latest_frame; }
                
                double avg_t = get_roi_avg_temp(pf, x0, y0, x1, y1);
                
                std::string pname, page, pgender;
                {
                    std::lock_guard<std::mutex> lk(g_patient_mutex);
                    pname = g_patient_name;
                    page = g_patient_age;
                    pgender = g_patient_gender;
                }

                if (g_logger) g_logger->log_measurement("roi", avg_t, g_tune.emissivity, g_tune.refl_offset, g_tune.raw_scale, g_tune.temp_offset, g_Ta.load(), g_RH.load());
                
                std::string fname = save_screenshot(pf.rgb, THERMAL_WIDTH, THERMAL_HEIGHT, cfg.screenshots_dir, pname, "roi");
                
                nlohmann::json res;
                res["type"] = "roi_result";
                res["avg_temp"] = avg_t;
                res["x0"] = x0;
                res["y0"] = y0;
                res["x1"] = x1;
                res["y1"] = y1;
                res["screenshot"] = fname;
                server.broadcast(res.dump());
            }
        }
        else if (type == "tune") {
            std::string param = j.value("param", "");
            double delta = j.value("delta", 0.0);
            std::lock_guard<std::mutex> lk(g_tune_mutex);
            if (param == "emissivity") g_tune.emissivity += delta;
            else if (param == "refl_offset") g_tune.refl_offset += delta;
            else if (param == "raw_scale") g_tune.raw_scale += delta;
            else if (param == "temp_offset") g_tune.temp_offset += delta;
            g_tune.clamp();
        }
        else if (type == "tune_set") {
            // Absolute value from slider (frontend sends this)
            std::string param = j.value("param", "");
            double value = j.value("value", 0.0);
            std::lock_guard<std::mutex> lk(g_tune_mutex);
            if (param == "emissivity") g_tune.emissivity = value;
            else if (param == "refl_offset") g_tune.refl_offset = value;
            else if (param == "raw_scale") g_tune.raw_scale = value;
            else if (param == "temp_offset") g_tune.temp_offset = value;
            g_tune.clamp();
        }
        else if (type == "tune_reset") {
            std::lock_guard<std::mutex> lk(g_tune_mutex);
            g_tune.reset();
        }
        else if (type == "servo") {
            std::string dir = j.value("direction", "");
            if (g_servo) {
                double a = g_servo_angle.load();
                if (dir == "left") a -= cfg.servo_angle_step;
                else if (dir == "right") a += cfg.servo_angle_step;
                
                if (a < 0) a = 0;
                if (a > 180) a = 180;
                
                servo_set_angle(g_servo, a);
                g_servo_angle.store(a);
            }
        }
        else if (type == "patient") {
            std::lock_guard<std::mutex> lk(g_patient_mutex);
            g_patient_name = j.value("name", g_patient_name);
            g_patient_age = j.value("age", g_patient_age);
            g_patient_gender = j.value("gender", g_patient_gender);
            
            if (g_logger) {
                g_logger->set_patient_name(g_patient_name);
                g_logger->set_patient_age(g_patient_age);
                g_logger->set_patient_gender(g_patient_gender);
            }
        }
        else if (type == "screenshot") {
            ProcessedFrame pf;
            { std::lock_guard<std::mutex> lk(g_frame_mutex); pf = g_latest_frame; }
            
            std::string pname;
            { std::lock_guard<std::mutex> lk(g_patient_mutex); pname = g_patient_name; }
            
            std::string fname = save_screenshot(pf.rgb, THERMAL_WIDTH, THERMAL_HEIGHT, cfg.screenshots_dir, pname, "screenshot");
            
            nlohmann::json res;
            res["type"] = "screenshot_saved";
            res["filename"] = fname;
            server.broadcast(res.dump());
        }
        else if (type == "shutdown") {
            fprintf(stderr, "Shutdown requested from Web UI\n");
            // Broadcast goodbye
            nlohmann::json res;
            res["type"] = "shutdown_ack";
            server.broadcast(res.dump());
            // Give time for the message to send
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            // Stop the main loop
            g_running.store(false, std::memory_order_relaxed);
            // Schedule system shutdown (runs after cleanup)
            std::thread([]() {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                system("sudo shutdown now");
            }).detach();
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "Error parsing message: %s\n", e.what());
    }
}

int main(int argc, char** argv) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::string config_path = "flirone_config.json";
    if (argc > 1) {
        config_path = argv[1];
    }
    
    AppConfig cfg = load_config(config_path);

    // Initialize runtime tunables from config
    {
        std::lock_guard<std::mutex> lk(g_tune_mutex);
        g_tune.emissivity  = cfg.default_emissivity;
        g_tune.refl_offset = cfg.default_refl_offset;
        g_tune.raw_scale   = cfg.default_raw_scale;
        g_tune.temp_offset = cfg.default_temp_offset;
        g_tune.clamp();
    }
    
    uint8_t colormap[768];
    if (!load_palette(cfg.palette_path, colormap)) {
        fprintf(stderr, "Failed to load palette from %s\n", cfg.palette_path.c_str());
        return 1;
    }

    std::thread dht12_thread(dht12_thread_func, cfg.dht12_i2c_bus, cfg.dht12_i2c_addr);

    if (servo_library_init() == 0) {
        if (cfg.servo_use_pca9685) {
            g_servo = servo_create_pca9685(cfg.servo_channel, cfg.servo_i2c_bus, cfg.servo_i2c_addr, cfg.servo_pwm_freq, cfg.servo_min_pulse, cfg.servo_max_pulse);
        } else {
            g_servo = servo_create(cfg.servo_channel, cfg.servo_min_pulse, cfg.servo_max_pulse);
        }
        if (g_servo) {
            servo_set_angle(g_servo, cfg.servo_initial_angle);
            servo_enable(g_servo);
            g_servo_angle.store(cfg.servo_initial_angle);
            g_servo_enabled.store(true);
        } else {
            fprintf(stderr, "Failed to create servo\n");
        }
    } else {
        fprintf(stderr, "Failed to initialize servo library\n");
    }
    
    if (!cfg.google_sheets_url.empty()) {
        g_logger = new GoogleSheetsLogger(cfg.google_sheets_url);
    }

    WebServer server(cfg.web_port, cfg.web_root, cfg.screenshots_dir);
    // Wrap palette in init message: {type:"init", palette:[r0,g0,b0,...,r255,g255,b255]}
    std::string palette_init = "{\"type\":\"init\",\"palette\":" + encode_palette_json(colormap) + "}";
    server.set_palette_json(palette_init);
    server.set_on_message([&server, &cfg](const std::string& msg) {
        handle_client_message(msg, server, cfg);
    });

    if (!server.start()) {
        fprintf(stderr, "Failed to start web server on port %d\n", cfg.web_port);
        g_running = false;
    } else {
        printf("Web server running on port %d...\n", cfg.web_port);
    }

    FlirOneReader reader(cfg.usb_vendor_id, cfg.usb_product_id);

    // Phase 2: Warm-up tracking
    auto startup_time = std::chrono::steady_clock::now();
    bool warmup_complete = false;
    
    while (g_running) {
        if (!reader.open_device()) {
            fprintf(stderr, "Waiting for FLIR One device...\n");
            sleep(2);
            continue;
        }
        ThermalFrame frame;
        while (g_running && reader.is_open()) {
            if (!reader.read_frame(frame)) break;
            if (!frame.valid) continue;
            
            Tunables tune;
            { std::lock_guard<std::mutex> lk(g_tune_mutex); tune = g_tune; }
            double Ta = g_Ta.load();
            double RH = g_RH.load();
            bool dht_ok = g_dht_ok.load();

            // Phase 1: Build atmospheric compensation params from DHT12 + config
            AtmosphericParams atm;
            atm.distance     = cfg.default_distance;
            atm.humidity     = RH;       // NOW USED in calculation!
            atm.atm_temp     = Ta;
            atm.enable_compensation = cfg.atmospheric_compensation;
            atm.ema_alpha    = cfg.ema_alpha;
            // Pre-compute atmospheric transmittance
            if (atm.enable_compensation) {
                atm.tau = compute_tau(atm.distance, atm.humidity, atm.atm_temp);
            }

            // Process frame with full radiometric compensation
            ProcessedFrame pf = process_frame(frame, tune, Ta, colormap, atm);

            // Phase 1: Apply EMA smoothing filter
            if (atm.ema_alpha < 1.0) {
                apply_ema_filter(pf, (float)atm.ema_alpha);
            }
            
            { std::lock_guard<std::mutex> lk(g_frame_mutex); g_latest_frame = pf; }

            // Phase 2: Check warm-up status
            auto elapsed = std::chrono::steady_clock::now() - startup_time;
            int warmup_elapsed_min = (int)std::chrono::duration_cast<std::chrono::minutes>(elapsed).count();
            int warmup_elapsed_sec = (int)std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
            if (!warmup_complete && warmup_elapsed_min >= cfg.warmup_minutes) {
                warmup_complete = true;
                fprintf(stderr, "✓ Camera warm-up complete (%d minutes)\n", cfg.warmup_minutes);
            }
            
            nlohmann::json msg;
            msg["type"] = "frame";
            msg["width"] = THERMAL_WIDTH;
            msg["height"] = THERMAL_HEIGHT;
            msg["min_temp"] = pf.min_temp;
            msg["max_temp"] = pf.max_temp;
            msg["center_temp"] = pf.center_temp;
            msg["ambient_temp"] = Ta;
            msg["humidity"] = RH;
            msg["dht_ok"] = dht_ok;
            msg["servo_angle"] = g_servo_angle.load();
            msg["servo_enabled"] = g_servo_enabled.load();

            // Phase 1: Atmospheric compensation info
            msg["tau"] = atm.tau;
            msg["distance"] = atm.distance;
            msg["atm_compensation"] = atm.enable_compensation;
            msg["ema_alpha"] = atm.ema_alpha;

            // Phase 2: Warm-up status
            msg["warmup_complete"] = warmup_complete;
            msg["warmup_elapsed_sec"] = warmup_elapsed_sec;
            msg["warmup_required_min"] = cfg.warmup_minutes;

            msg["tunables"] = {
                {"emissivity", tune.emissivity},
                {"refl_offset", tune.refl_offset},
                {"raw_scale", tune.raw_scale},
                {"temp_offset", tune.temp_offset}
            };
            msg["image"] = encode_frame_base64(pf.rgb, THERMAL_WIDTH, THERMAL_HEIGHT);
            
            server.broadcast(msg.dump());
        }
        reader.close_device();
        if (g_running) {
            fprintf(stderr, "Device disconnected, retrying...\n");
            sleep(2);
        }
    }
    
    server.stop();

    if (g_logger) {
        delete g_logger;
        g_logger = nullptr;
    }
    
    if (g_servo) {
        servo_disable(g_servo);
        servo_destroy(g_servo);
        g_servo = nullptr;
    }
    servo_library_cleanup();

    if (dht12_thread.joinable()) {
        dht12_thread.join();
    }
    
    printf("Shutdown complete.\n");
    return 0;
}
