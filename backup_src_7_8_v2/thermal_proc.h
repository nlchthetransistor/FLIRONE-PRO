#pragma once
#include <cstdint>
#include <string>

// ============================================================
// Thermal image dimensions (FLIR One Gen2)
// ============================================================
#define THERMAL_WIDTH  160
#define THERMAL_HEIGHT 120

// ============================================================
// Planck radiometric constants (from FLIR One calibration data)
// ============================================================
#define PLANCK_R1  16528.178
#define PLANCK_B   1427.5
#define PLANCK_F   1.0
#define PLANCK_O   (-1307.0)
#define PLANCK_R2  0.012258549

// ============================================================
// Tunables — realtime adjustable calibration parameters
// ============================================================
struct Tunables {
    double emissivity  = 0.98;
    double refl_offset = 0.0;
    double raw_scale   = 4.0;
    double temp_offset = 0.0;

    // Clamp limits
    static constexpr double EMISS_MIN = 0.70, EMISS_MAX = 1.00;
    static constexpr double REFL_MIN  = -20.0, REFL_MAX  = 20.0;
    static constexpr double SCALE_MIN = 1.0,   SCALE_MAX = 6.0;
    static constexpr double TOFF_MIN  = -10.0, TOFF_MAX  = 10.0;

    // Steps per adjustment
    static constexpr double EMISS_STEP = 0.01;
    static constexpr double REFL_STEP  = 0.10;
    static constexpr double SCALE_STEP = 0.05;
    static constexpr double TOFF_STEP  = 0.10;

    void clamp();
    void reset();
};

// ============================================================
// ThermalFrame — raw 16-bit data from FLIR One
// ============================================================
struct ThermalFrame {
    uint16_t raw[THERMAL_WIDTH * THERMAL_HEIGHT];
    bool valid = false;
};

// ============================================================
// ProcessedFrame — colorized + temperature data
// ============================================================
struct ProcessedFrame {
    uint8_t  rgb[THERMAL_WIDTH * THERMAL_HEIGHT * 3];   // colorized image
    float    temps[THERMAL_WIDTH * THERMAL_HEIGHT];      // temperature per pixel (°C)
    float    min_temp, max_temp, center_temp;
    int      max_x, max_y;                               // hottest pixel location
    uint16_t min_raw, max_raw;
};

// ============================================================
// API
// ============================================================

// Load colormap palette from .raw file (256 RGB entries = 768 bytes)
bool load_palette(const std::string& path, uint8_t colormap[768]);

// Process a raw thermal frame into colorized RGB + temperatures
ProcessedFrame process_frame(const ThermalFrame& frame,
                             const Tunables& tune,
                             double ambient_temp,
                             const uint8_t colormap[768]);

// Get spot temperature at (x, y)
double get_spot_temp(const ProcessedFrame& pf, int x, int y);

// Get average temperature in ROI rectangle [x0,y0]–[x1,y1]
double get_roi_avg_temp(const ProcessedFrame& pf,
                        int x0, int y0, int x1, int y1);

// Encode RGB image as JPEG and return base64 string
std::string encode_frame_base64(const uint8_t* rgb, int w, int h,
                                int quality = 80);

// Encode palette (768 bytes) as JSON array string: [r0,g0,b0,...,r255,g255,b255]
std::string encode_palette_json(const uint8_t colormap[768]);

// Save screenshot as JPEG file
// Returns the filename on success, empty string on failure
std::string save_screenshot(const uint8_t* rgb, int w, int h,
                            const std::string& dir,
                            const std::string& patient_name,
                            const std::string& measurement_type);
