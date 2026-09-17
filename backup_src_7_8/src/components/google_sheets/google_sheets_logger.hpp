#ifndef GOOGLE_SHEETS_LOGGER_HPP
#define GOOGLE_SHEETS_LOGGER_HPP

#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <memory>

// =====================================================================
// GoogleSheetsLogger: Manages data logging to Google Sheets via Apps Script
// =====================================================================
// Usage:
//   GoogleSheetsLogger logger(deployment_url);
//   logger.set_patient_name("John Doe");
//   logger.log_measurement("spot", 37.5, 0.98, 0.0, 4.0, 0.0, 28.0, 50.0);

struct MeasurementData {
    std::string timestamp;
    std::string patient_name;
    std::string patient_age;
    std::string patient_gender;
    std::string measurement_type;  // "spot" or "roi"
    double temperature;
    double emissivity;
    double refl_offset;
    double raw_scale;
    double temp_offset;
    double ambient_temp;
    double humidity;

    std::string to_json() const;
};

class GoogleSheetsLogger {
public:
    GoogleSheetsLogger(const std::string& deployment_url);
    ~GoogleSheetsLogger();

    // Set patient name (required before logging)
    void set_patient_name(const std::string& name);
    std::string get_patient_name() const;

    // Set patient age
    void set_patient_age(const std::string& age);
    std::string get_patient_age() const;

    // Set patient gender
    void set_patient_gender(const std::string& gender);
    std::string get_patient_gender() const;

    // Log a measurement (asynchronous - queued for background thread)
    void log_measurement(
        const std::string& measurement_type,  // "spot" or "roi"
        double temperature,
        double emissivity,
        double refl_offset,
        double raw_scale,
        double temp_offset,
        double ambient_temp,
        double humidity
    );

    // Flush pending logs (wait for all queued items to be sent)
    void flush();

    // Get number of pending logs
    size_t pending_count() const;

    // Check if logger is running
    bool is_running() const;

    // Enable/disable logging
    void set_enabled(bool enabled);
    bool is_enabled() const;

private:
    std::string deployment_url_;
    std::string patient_name_;
    std::string patient_age_;
    std::string patient_gender_;
    bool enabled_;
    bool running_;

    std::queue<MeasurementData> queue_;
    mutable std::mutex queue_mutex_;
    std::unique_ptr<std::thread> worker_thread_;

    void worker_loop();
    bool send_to_sheets(const MeasurementData& data);
    static size_t curl_write_callback(void* contents, size_t size, size_t nmemb, std::string* s);
};

#endif // GOOGLE_SHEETS_LOGGER_HPP
