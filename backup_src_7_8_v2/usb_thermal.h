#pragma once
#include "thermal_proc.h"
#include <atomic>
#include <cstdint>

// Forward declaration (avoid including libusb.h in header)
struct libusb_device_handle;

// ============================================================
// FlirOneReader — manages USB communication with FLIR One Gen2
// ============================================================
class FlirOneReader {
public:
    FlirOneReader(int vendor_id = 0x09cb, int product_id = 0x1996);
    ~FlirOneReader();

    // Non-copyable
    FlirOneReader(const FlirOneReader&) = delete;
    FlirOneReader& operator=(const FlirOneReader&) = delete;

    // Open and configure the USB device
    bool open_device();

    // Read one complete thermal frame (blocking)
    // Keeps reading USB chunks until a full frame is assembled.
    // Returns false if stop requested or unrecoverable error.
    bool read_frame(ThermalFrame& frame);

    // Signal the reader to stop (thread-safe)
    void request_stop();

    // Close device, release interfaces
    void close_device();

    bool is_open() const { return devh_ != nullptr; }
    bool is_stop_requested() const { return stop_requested_.load(std::memory_order_relaxed); }

private:
    int vendor_id_;
    int product_id_;
    libusb_device_handle* devh_ = nullptr;
    std::atomic<bool> stop_requested_{false};

    // Frame assembly buffer (EP 0x85 chunks)
    static constexpr int BUF85_SIZE = 1048576;  // 1 MB
    uint8_t buf85_[BUF85_SIZE];
    int buf85_ptr_ = 0;

    // Bulk transfer buffer
    uint8_t bulk_buf_[1048576];

    // USB init helpers
    bool find_and_open();
    bool claim_interfaces();
    bool start_video_stream();

    // Frame extraction
    bool try_extract_frame(ThermalFrame& frame);
};
