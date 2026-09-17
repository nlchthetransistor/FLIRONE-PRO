#include "google_sheets_logger.hpp"
#include <curl/curl.h>
#include <ctime>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cstdio>
#include <unistd.h>

// =====================================================================
// Helper: Convert MeasurementData to JSON
// =====================================================================
std::string MeasurementData::to_json() const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    
    oss << "{"
        << "\"timestamp\":\"" << timestamp << "\","
        << "\"patient_name\":\"" << patient_name << "\","
        << "\"patient_age\":\"" << patient_age << "\","
        << "\"patient_gender\":\"" << patient_gender << "\","
        << "\"measurement_type\":\"" << measurement_type << "\","
        << "\"temperature\":" << temperature << ","
        << "\"emissivity\":" << emissivity << ","
        << "\"refl_offset\":" << refl_offset << ","
        << "\"raw_scale\":" << raw_scale << ","
        << "\"temp_offset\":" << temp_offset << ","
        << "\"ambient_temp\":" << ambient_temp << ","
        << "\"humidity\":" << humidity
        << "}";
    
    return oss.str();
}

// =====================================================================
// GoogleSheetsLogger Implementation
// =====================================================================

GoogleSheetsLogger::GoogleSheetsLogger(const std::string& deployment_url)
    : deployment_url_(deployment_url),
      patient_name_("Unknown"),
      patient_age_("Unknown"),
      patient_gender_("Unknown"),
      enabled_(true),
      running_(true)
{
    // Start background worker thread
    worker_thread_ = std::make_unique<std::thread>([this]() { worker_loop(); });
    fprintf(stderr, "[GoogleSheets] Logger initialized with URL: %s\n", deployment_url_.c_str());
}

GoogleSheetsLogger::~GoogleSheetsLogger()
{
    // Stop worker thread
    running_ = false;
    if (worker_thread_ && worker_thread_->joinable()) {
        worker_thread_->join();
    }
    fprintf(stderr, "[GoogleSheets] Logger destroyed\n");
}

void GoogleSheetsLogger::set_patient_name(const std::string& name)
{
    std::lock_guard<std::mutex> lock(queue_mutex_);
    patient_name_ = name;
    fprintf(stderr, "[GoogleSheets] Patient name set to: %s\n", name.c_str());
}

std::string GoogleSheetsLogger::get_patient_name() const
{
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return patient_name_;
}

void GoogleSheetsLogger::set_patient_age(const std::string& age)
{
    std::lock_guard<std::mutex> lock(queue_mutex_);
    patient_age_ = age;
    fprintf(stderr, "[GoogleSheets] Patient age set to: %s\n", age.c_str());
}

std::string GoogleSheetsLogger::get_patient_age() const
{
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return patient_age_;
}

void GoogleSheetsLogger::set_patient_gender(const std::string& gender)
{
    std::lock_guard<std::mutex> lock(queue_mutex_);
    patient_gender_ = gender;
    fprintf(stderr, "[GoogleSheets] Patient gender set to: %s\n", gender.c_str());
}

std::string GoogleSheetsLogger::get_patient_gender() const
{
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return patient_gender_;
}

void GoogleSheetsLogger::log_measurement(
    const std::string& measurement_type,
    double temperature,
    double emissivity,
    double refl_offset,
    double raw_scale,
    double temp_offset,
    double ambient_temp,
    double humidity)
{
    if (!enabled_) return;

    // Generate timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t_val = std::chrono::system_clock::to_time_t(now);
    struct tm* timeinfo = localtime(&time_t_val);
    
    char timestamp_buf[32];
    strftime(timestamp_buf, sizeof(timestamp_buf), "%Y-%m-%d %H:%M:%S", timeinfo);

    // Create measurement data
    MeasurementData data;
    data.timestamp = timestamp_buf;
    data.patient_name = patient_name_;
    data.patient_age = patient_age_;
    data.patient_gender = patient_gender_;
    data.measurement_type = measurement_type;
    data.temperature = temperature;
    data.emissivity = emissivity;
    data.refl_offset = refl_offset;
    data.raw_scale = raw_scale;
    data.temp_offset = temp_offset;
    data.ambient_temp = ambient_temp;
    data.humidity = humidity;

    // Queue for background thread
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.push(data);
    }

    fprintf(stderr, "[GoogleSheets] Measurement queued: %s (%.2f°C) - Queue size: %lu\n",
            measurement_type.c_str(), temperature, queue_.size());
}

void GoogleSheetsLogger::flush()
{
    // Wait until queue is empty
    while (true) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (queue_.empty()) break;
        }
        usleep(100000);  // 100ms
    }
    fprintf(stderr, "[GoogleSheets] All pending logs flushed\n");
}

size_t GoogleSheetsLogger::pending_count() const
{
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return queue_.size();
}

bool GoogleSheetsLogger::is_running() const
{
    return running_;
}

void GoogleSheetsLogger::set_enabled(bool enabled)
{
    enabled_ = enabled;
    fprintf(stderr, "[GoogleSheets] Logging %s\n", enabled ? "ENABLED" : "DISABLED");
}

bool GoogleSheetsLogger::is_enabled() const
{
    return enabled_;
}

// =====================================================================
// CURL callback for response handling
// =====================================================================
size_t GoogleSheetsLogger::curl_write_callback(void* contents, size_t size, size_t nmemb, std::string* s)
{
    size_t newLength = size * nmemb;
    try {
        s->append((char*)contents, newLength);
        return newLength;
    } catch (std::bad_alloc&) {
        return 0;
    }
}

// =====================================================================
// Worker thread: Process queued measurements
// =====================================================================
void GoogleSheetsLogger::worker_loop()
{
    fprintf(stderr, "[GoogleSheets] Worker thread started\n");
    
    while (running_) {
        MeasurementData data;
        bool has_data = false;
        
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            
            if (!queue_.empty() && enabled_) {
                data = queue_.front();
                queue_.pop();
                has_data = true;
            }
        }
        
        // Send data outside of lock to avoid blocking
        if (has_data) {
            send_to_sheets(data);
        }
        
        usleep(500000);  // 500ms check interval
    }
    
    fprintf(stderr, "[GoogleSheets] Worker thread stopped\n");
}

// =====================================================================
// Send data to Google Sheets via Apps Script
// =====================================================================
bool GoogleSheetsLogger::send_to_sheets(const MeasurementData& data)
{
    CURL* curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "[GoogleSheets] ERROR: Failed to initialize CURL\n");
        return false;
    }

    std::string json_data = data.to_json();
    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, deployment_url_.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data.c_str());
    
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    
    bool success = false;
    if (res != CURLE_OK) {
        fprintf(stderr, "[GoogleSheets] ERROR: CURL request failed: %s\n", curl_easy_strerror(res));
    } else {
        fprintf(stderr, "[GoogleSheets] ✓ Data sent: %s (%.2f°C) - Patient: %s\n",
                data.measurement_type.c_str(), data.temperature, data.patient_name.c_str());
        success = true;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    return success;
}
