#include "usb_thermal.h"
#include <libusb.h>
#include <cstring>
#include <iostream>
#include <chrono>
#include <thread>

FlirOneReader::FlirOneReader(int vendor_id, int product_id)
    : vendor_id_(vendor_id), product_id_(product_id), devh_(nullptr), buf85_ptr_(0)
{
    std::memset(buf85_, 0, sizeof(buf85_));
}

FlirOneReader::~FlirOneReader() {
    close_device();
}

void FlirOneReader::request_stop() {
    stop_requested_.store(true, std::memory_order_relaxed);
}

bool FlirOneReader::open_device() {
    int r = libusb_init(NULL);
    if (r < 0) {
        std::cerr << "Failed to initialise libusb" << std::endl;
        return false;
    }

    if (!find_and_open()) {
        std::cerr << "Could not find/open FLIR One device" << std::endl;
        libusb_exit(NULL);
        return false;
    }

    if (!claim_interfaces()) {
        close_device();
        return false;
    }

    if (!start_video_stream()) {
        close_device();
        return false;
    }

    return true;
}

bool FlirOneReader::find_and_open() {
    devh_ = libusb_open_device_with_vid_pid(NULL, vendor_id_, product_id_);
    return devh_ != nullptr;
}

bool FlirOneReader::claim_interfaces() {
    int r = libusb_set_configuration(devh_, 3);
    if (r < 0) {
        std::cerr << "libusb_set_configuration error " << r << std::endl;
        return false;
    }

    for (int i = 0; i < 3; ++i) {
        r = libusb_claim_interface(devh_, i);
        if (r < 0) {
            std::cerr << "libusb_claim_interface " << i << " error " << r << std::endl;
            return false;
        }
    }
    return true;
}

bool FlirOneReader::start_video_stream() {
    unsigned char data[2] = {0, 0};
    int r;

    // stop interface 2 FRAME
    r = libusb_control_transfer(devh_, 1, 0x0b, 0, 2, data, 0, 100);
    if (r < 0) return false;

    // stop interface 1 FILEIO
    r = libusb_control_transfer(devh_, 1, 0x0b, 0, 1, data, 0, 100);
    if (r < 0) return false;

    // start interface 1 FILEIO
    r = libusb_control_transfer(devh_, 1, 0x0b, 1, 1, data, 0, 100);
    if (r < 0) return false;

    // Ask for video stream, start EP 0x85
    r = libusb_control_transfer(devh_, 1, 0x0b, 1, 2, data, 2, 200);
    if (r < 0) return false;

    return true;
}

bool FlirOneReader::try_extract_frame(ThermalFrame& frame) {
    if (buf85_ptr_ < 28) return false;

    auto u32le = [&](int off) -> uint32_t {
        return (uint32_t)buf85_[off] |
               ((uint32_t)buf85_[off+1] << 8) |
               ((uint32_t)buf85_[off+2] << 16) |
               ((uint32_t)buf85_[off+3] << 24);
    };

    const uint32_t FrameSize = u32le(8);
    const uint32_t NeedBytes = FrameSize + 28;

    if (NeedBytes > (uint32_t)BUF85_SIZE) {
        buf85_ptr_ = 0;
        return false;
    }
    
    if ((uint32_t)buf85_ptr_ < NeedBytes) {
        return false;
    }

    buf85_ptr_ = 0;

    bool have_thermal = true;
    for (int y = 0; y < THERMAL_HEIGHT; ++y) {
        for (int x = 0; x < THERMAL_WIDTH; ++x) {
            int idx;
            if (x < 80) idx = 2 * (y * 164 + x) + 32;
            else        idx = 2 * (y * 164 + x) + 32 + 4;
            
            if (idx + 1 >= (int)NeedBytes) { 
                have_thermal = false; 
                break; 
            }
            
            unsigned short v = (unsigned short)(buf85_[idx] | (buf85_[idx+1] << 8));
            frame.raw[y * THERMAL_WIDTH + x] = v;
        }
    }
    
    frame.valid = have_thermal;
    return have_thermal;
}

bool FlirOneReader::read_frame(ThermalFrame& frame) {
    static const unsigned char magicbyte[4] = {0xEF, 0xBE, 0x00, 0x00};
    int actual_length = 0;

    while (!is_stop_requested()) {
        int r = libusb_bulk_transfer(devh_, 0x85, bulk_buf_, sizeof(bulk_buf_), &actual_length, 100);
        
        if (r == 0 && actual_length > 0) {
            if ((actual_length >= 4 && std::memcmp(bulk_buf_, magicbyte, 4) == 0) ||
                (buf85_ptr_ + actual_length) >= BUF85_SIZE)
            {
                buf85_ptr_ = 0;
            }

            std::memcpy(buf85_ + buf85_ptr_, bulk_buf_, actual_length);
            buf85_ptr_ += actual_length;

            if (buf85_ptr_ >= 4 && std::memcmp(buf85_, magicbyte, 4) == 0) {
                if (try_extract_frame(frame)) {
                    // Drain EP 0x81 and 0x83
                    libusb_bulk_transfer(devh_, 0x81, bulk_buf_, sizeof(bulk_buf_), &actual_length, 10);
                    libusb_bulk_transfer(devh_, 0x83, bulk_buf_, sizeof(bulk_buf_), &actual_length, 10);
                    return true;
                }
            } else {
                buf85_ptr_ = 0;
            }
        }

        // Drain EP 0x81 and 0x83
        r = libusb_bulk_transfer(devh_, 0x81, bulk_buf_, sizeof(bulk_buf_), &actual_length, 10);
        r = libusb_bulk_transfer(devh_, 0x83, bulk_buf_, sizeof(bulk_buf_), &actual_length, 10);

        if (r == LIBUSB_ERROR_NO_DEVICE) {
            std::cerr << "USB disconnected" << std::endl;
            return false;
        }
    }

    return false;
}

void FlirOneReader::close_device() {
    if (devh_) {
        libusb_release_interface(devh_, 0);
        libusb_release_interface(devh_, 1);
        libusb_release_interface(devh_, 2);
        libusb_reset_device(devh_);
        libusb_close(devh_);
        devh_ = nullptr;
        libusb_exit(NULL);
    }
}
