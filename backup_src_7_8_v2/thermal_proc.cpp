#include "thermal_proc.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <sys/stat.h>
#include <jpeglib.h>
#include <cstring>

static inline int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void Tunables::clamp() {
    if (emissivity < EMISS_MIN) emissivity = EMISS_MIN;
    if (emissivity > EMISS_MAX) emissivity = EMISS_MAX;
    if (refl_offset < REFL_MIN) refl_offset = REFL_MIN;
    if (refl_offset > REFL_MAX) refl_offset = REFL_MAX;
    if (raw_scale < SCALE_MIN) raw_scale = SCALE_MIN;
    if (raw_scale > SCALE_MAX) raw_scale = SCALE_MAX;
    if (temp_offset < TOFF_MIN) temp_offset = TOFF_MIN;
    if (temp_offset > TOFF_MAX) temp_offset = TOFF_MAX;
}

void Tunables::reset() {
    emissivity = 0.98;
    refl_offset = 0.0;
    raw_scale = 4.0;
    temp_offset = 0.0;
}

bool load_palette(const std::string& path, uint8_t colormap[768]) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open palette file " << path << std::endl;
        return false;
    }
    file.read(reinterpret_cast<char*>(colormap), 768);
    bool success = (file.gcount() == 768);
    if (!success) {
        std::cerr << "Error: Failed to read 768 bytes from palette file" << std::endl;
    }
    return success;
}

static inline double raw2temperature_core(unsigned short RAW, double T_refl_c, double emissivity, double raw_scale) {
    double raw = (double)RAW * raw_scale;
    const double RAWrefl = PLANCK_R1 / (PLANCK_R2 * (exp(PLANCK_B / (T_refl_c + 273.15)) - PLANCK_F)) - PLANCK_O;
    if (emissivity < 1e-6) emissivity = 1e-6;
    const double RAWobj = (raw - (1.0 - emissivity) * RAWrefl) / emissivity;
    return PLANCK_B / log(PLANCK_R1 / (PLANCK_R2 * (RAWobj + PLANCK_O)) + PLANCK_F) - 273.15;
}

ProcessedFrame process_frame(const ThermalFrame& frame,
                             const Tunables& tune,
                             double ambient_temp,
                             const uint8_t colormap[768]) 
{
    ProcessedFrame pf;
    if (!frame.valid) return pf;

    double T_refl = ambient_temp + tune.refl_offset;

    // 1) Find min/max RAW values
    int minv = 65535, maxv = 0;
    int maxx = 0, maxy = 0;
    for (int y = 0; y < THERMAL_HEIGHT; ++y) {
        for (int x = 0; x < THERMAL_WIDTH; ++x) {
            int v = (int)frame.raw[y * THERMAL_WIDTH + x];
            if (v < minv) minv = v;
            if (v > maxv) { maxv = v; maxx = x; maxy = y; }
        }
    }
    if (minv >= maxv) maxv = minv + 1;
    
    pf.min_raw = minv;
    pf.max_raw = maxv;
    pf.max_x = maxx;
    pf.max_y = maxy;

    // 2) Temperature LUT
    static std::vector<float> tempLUT(65536);
    static bool lut_init = false;
    static double lut_Trefl = 1e9, lut_eps = 0.98, lut_scale = 4.0, lut_to = 0.0;
    static int lut_lo = 65535, lut_hi = 0;

    int want_lo = std::max(0, minv - 512);
    int want_hi = std::min(65535, maxv + 512);

    if (!lut_init ||
        std::fabs(lut_Trefl - T_refl) > 0.05 ||
        std::fabs(lut_eps   - tune.emissivity) > 1e-9 ||
        std::fabs(lut_scale - tune.raw_scale)   > 1e-9 ||
        std::fabs(lut_to    - tune.temp_offset)    > 1e-9)
    {
        lut_init = true;
        lut_Trefl = T_refl; lut_eps = tune.emissivity; lut_scale = tune.raw_scale; lut_to = tune.temp_offset;
        lut_lo = 65535; lut_hi = 0;
    }

    if (want_lo < lut_lo) {
        for (int raw = want_lo; raw < lut_lo; ++raw) {
            tempLUT[raw] = (float)(raw2temperature_core((unsigned short)raw, T_refl, tune.emissivity, tune.raw_scale) + tune.temp_offset);
        }
        lut_lo = want_lo;
    }
    if (want_hi > lut_hi) {
        for (int raw = lut_hi + 1; raw <= want_hi; ++raw) {
            tempLUT[raw] = (float)(raw2temperature_core((unsigned short)raw, T_refl, tune.emissivity, tune.raw_scale) + tune.temp_offset);
        }
        lut_hi = want_hi;
    }

    // 3) Convert RAW to 8-bit grayscale & colorize, and fill temps[] array
    int delta = maxv - minv;
    if (delta <= 0) delta = 1;
    int scale = 0x10000 / delta;

    float min_t = 1e9, max_t = -1e9;
    
    for (int y = 0; y < THERMAL_HEIGHT; ++y) {
        for (int x = 0; x < THERMAL_WIDTH; ++x) {
            int idx = y * THERMAL_WIDTH + x;
            uint16_t raw_val = frame.raw[idx];
            
            // Fill temps
            float t = tempLUT[raw_val];
            pf.temps[idx] = t;
            if (t < min_t) min_t = t;
            if (t > max_t) max_t = t;

            // Grayscale
            int vv = (((int)raw_val - minv) * scale) >> 8;
            vv = clampi(vv, 0, 255);
            
            // Colorize
            unsigned char* dst = &pf.rgb[3 * idx];
            const unsigned char* src = &colormap[3 * vv];
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
        }
    }
    
    pf.min_temp = min_t;
    pf.max_temp = max_t;
    pf.center_temp = pf.temps[(THERMAL_HEIGHT/2) * THERMAL_WIDTH + (THERMAL_WIDTH/2)];

    return pf;
}

double get_spot_temp(const ProcessedFrame& pf, int x, int y) {
    x = clampi(x, 0, THERMAL_WIDTH - 1);
    y = clampi(y, 0, THERMAL_HEIGHT - 1);
    return pf.temps[y * THERMAL_WIDTH + x];
}

double get_roi_avg_temp(const ProcessedFrame& pf, int x0, int y0, int x1, int y1) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    
    x0 = clampi(x0, 0, THERMAL_WIDTH - 1);
    x1 = clampi(x1, 0, THERMAL_WIDTH - 1);
    y0 = clampi(y0, 0, THERMAL_HEIGHT - 1);
    y1 = clampi(y1, 0, THERMAL_HEIGHT - 1);

    double sum = 0.0;
    int count = 0;
    
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            sum += pf.temps[y * THERMAL_WIDTH + x];
            count++;
        }
    }
    
    return count > 0 ? (sum / count) : 0.0;
}

static std::string base64_encode(const unsigned char* bytes_to_encode, unsigned int in_len) {
    static const std::string base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
        
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for(i = 0; (i <4) ; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for(j = i; j < 3; j++) char_array_3[j] = '\0';
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;
        for (j = 0; (j < i + 1); j++)
            ret += base64_chars[char_array_4[j]];
        while((i++ < 3)) ret += '=';
    }
    return ret;
}

std::string encode_frame_base64(const uint8_t* rgb, int w, int h, int quality) {
    unsigned char* mem = nullptr;
    unsigned long mem_size = 0;
    
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_mem_dest(&cinfo, &mem, &mem_size);
    
    cinfo.image_width = w;
    cinfo.image_height = h;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    
    JSAMPROW row_pointer[1];
    int row_stride = w * 3;
    
    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = const_cast<JSAMPROW>(&rgb[cinfo.next_scanline * row_stride]);
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }
    
    jpeg_finish_compress(&cinfo);
    
    std::string b64 = base64_encode(mem, mem_size);
    
    jpeg_destroy_compress(&cinfo);
    if (mem) {
        free(mem);
    }
    
    return b64;
}

std::string encode_palette_json(const uint8_t colormap[768]) {
    std::ostringstream oss;
    oss << "[";
    for (int i = 0; i < 768; ++i) {
        if (i > 0) oss << ",";
        oss << (int)colormap[i];
    }
    oss << "]";
    return oss.str();
}

std::string save_screenshot(const uint8_t* rgb, int w, int h,
                            const std::string& dir,
                            const std::string& patient_name,
                            const std::string& measurement_type)
{
    struct stat st = {0};
    if (stat(dir.c_str(), &st) == -1) {
        mkdir(dir.c_str(), 0755);
    }

    time_t now = time(NULL);
    struct tm* loctime = localtime(&now);
    
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y%m%d_%H%M%S", loctime);
    
    std::ostringstream filename;
    filename << dir << "/" << timebuf << "_" << patient_name << "_" << measurement_type << ".jpg";
    
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE * outfile;
    
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    
    if ((outfile = fopen(filename.str().c_str(), "wb")) == NULL) {
        std::cerr << "Error: Can't open " << filename.str() << " for writing" << std::endl;
        jpeg_destroy_compress(&cinfo);
        return "";
    }
    
    jpeg_stdio_dest(&cinfo, outfile);
    
    cinfo.image_width = w;
    cinfo.image_height = h;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 95, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    
    JSAMPROW row_pointer[1];
    int row_stride = w * 3;
    
    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = const_cast<JSAMPROW>(&rgb[cinfo.next_scanline * row_stride]);
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }
    
    jpeg_finish_compress(&cinfo);
    fclose(outfile);
    jpeg_destroy_compress(&cinfo);
    
    return filename.str();
}
