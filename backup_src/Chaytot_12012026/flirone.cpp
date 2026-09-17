/*
 * Copyright (C) 2015-2016 Thomas <tomas123 @ EEVblog Electronics Community Forum>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

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
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#include <atomic>
#include <thread>
#include "dht12.hpp"

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

 static struct libusb_device_handle *devh = NULL;
 int filecount=0;
 struct timeval t1, t2;
 long long fps_t;
 
 int FFC =   0; // detect FFC

// -- buffer for EP 0x85 chunks ---------------
 #define BUF85SIZE 1048576  // size got from android app
 int buf85pointer = 0;
 unsigned char buf85[BUF85SIZE];
 
// ===== Global for mouse picking =====
static unsigned short g_pix[160 * 120];
static int g_scale = 2;

// Point measurement
static int g_mouse_x = -1;
static int g_mouse_y = -1;
static bool g_mouse_click = false;

// ROI measurement
static bool g_selecting = false;
static bool g_roi_valid = false;
static int g_x0 = 0, g_y0 = 0;
static int g_x1 = 0, g_y1 = 0;


std::atomic<double> g_Ta{27.0};
std::atomic<double> g_RH{50.0};
std::atomic<bool>   g_dht_ok{false};
std::atomic<uint64_t> g_dht_seq{0};  // tăng mỗi lần đọc OK để vframe biết rebuild LUT

static void start_dht12_thread()
{
    std::thread([]{
        Dht12I2C dht(1, 0x5c);
        double last_t = 27.0, last_h = 50.0;

        while (true) {
            auto r = dht.read_once();
            bool ok = false;

            if (r) {
                last_t = r->temp_c;
                last_h = r->hum_rh;
                ok = true;
            }
            g_Ta.store(last_t);
            g_RH.store(last_h);
            g_dht_ok.store(ok);

            usleep(500000); // 0.5s/lần là thoải mái cho DHT12
        }
    }).detach();
}

void on_mouse(int event, int x, int y, int flags, void* userdata)
{
    int px = x / g_scale;
    int py = y / g_scale;

    if (event == cv::EVENT_LBUTTONDOWN) {
        // Start ROI
        g_selecting = true;
        g_roi_valid = false;

        g_x0 = px;
        g_y0 = py;

        // Also mark for point (tentative)
        g_mouse_x = px;
        g_mouse_y = py;
        g_mouse_click = true;
    }
    else if (event == cv::EVENT_MOUSEMOVE && g_selecting) {
        g_x1 = px;
        g_y1 = py;
    }
    else if (event == cv::EVENT_LBUTTONUP) {
        g_selecting = false;

        g_x1 = px;
        g_y1 = py;

        // Nếu kéo đủ lớn → ROI
        if (abs(g_x1 - g_x0) > 2 && abs(g_y1 - g_y0) > 2) {
            g_roi_valid = true;
            g_mouse_click = false; // ưu tiên ROI
        } else {
            // Nếu gần như click → đo điểm
            g_roi_valid = false;
            g_mouse_click = true;
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
  int rx, ry;
  while (*string) {
    for (ry = 0; ry < 5; ++ry) {
      for (rx = 0; rx < 7; ++rx) {
        int v = (font5x7_basic[((*string) & 0x7F) - CHAR_OFFSET][ry] >> (rx)) & 1;
//	fb[(y+ry) * 160 + (x + rx)] = v ? 0 : 0xFF;                       // black / white
//	fb[(y+rx) * 160 + (x + ry)] = v ? 0 : 0xFF;                       // black / white

        fb[(y+rx) * 160 + (x + ry)] = v ? 0 : fb[(y+rx) * 160 + (x + ry)];  // transparent
      }
    }
    string++;
    x += 6;
  }
}

static inline double raw2temperature_core(unsigned short RAW, double T_refl_c, double emissivity)
{
    // giữ đúng hệ số gốc
    double raw = (double)RAW * 4.0;

    const double RAWrefl =
        PlanckR1 / (PlanckR2 * (exp(PlanckB / (T_refl_c + 273.15)) - PlanckF)) - PlanckO;

    if (emissivity < 1e-6) emissivity = 1e-6;

    const double RAWobj = (raw - (1.0 - 0.95) * RAWrefl) / 1.0;

    return PlanckB / log(PlanckR1 / (PlanckR2 * (RAWobj + PlanckO)) + PlanckF) - 273.15;
}

double raw2temperature(unsigned short RAW, double T_refl_c, double emissivity /*=0.98*/)
{
    return raw2temperature_core(RAW, T_refl_c, emissivity);
}



void startv4l2()
{
     int ret_code = 0;

     int i;
     int k=1;
/*     
//open video_device0
     printf("using output device: %s\n", video_device0);
     
     fdwr0 = open(video_device0, O_RDWR);
     assert(fdwr0 >= 0);

     ret_code = ioctl(fdwr0, VIDIOC_QUERYCAP, &vid_caps0);
     assert(ret_code != -1);

     memset(&vid_format0, 0, sizeof(vid_format0));

     ret_code = ioctl(fdwr0, VIDIOC_G_FMT, &vid_format0);

     linewidth0=FRAME_WIDTH0;
     framesize0=FRAME_WIDTH0*FRAME_HEIGHT0*1; // 8 Bit

     vid_format0.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
     vid_format0.fmt.pix.width = FRAME_WIDTH0;
     vid_format0.fmt.pix.height = FRAME_HEIGHT0;
     vid_format0.fmt.pix.pixelformat = FRAME_FORMAT0;
     vid_format0.fmt.pix.sizeimage = framesize0;
     vid_format0.fmt.pix.field = V4L2_FIELD_NONE;
     vid_format0.fmt.pix.bytesperline = linewidth0;
     vid_format0.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;

     // set data format
     ret_code = ioctl(fdwr0, VIDIOC_S_FMT, &vid_format0);
     assert(ret_code != -1);

     print_format(&vid_format0);
*/     
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
    // ======= LẤY Ta/RH (DHT12 thread publish) =======
    const double Ta = g_Ta.load();
    const double RH = g_RH.load();
    const bool dht_ok = g_dht_ok.load();

    // ======= USB ERROR HANDLER =======
    time_t now1 = time(NULL);
    if (r < 0) {
        if (strcmp(EP_error, libusb_error_name(r)) != 0) {
            strcpy(EP_error, libusb_error_name(r));
            fprintf(stderr, "\n: %s >>>>>>>>>>>>>>>>>bulk transfer (in) %s:%i %s\n",
                    ctime(&now1), ep, r, libusb_error_name(r));
            sleep(1);
        }
        return;
    }

    // ======= FRAME BUFFER ASSEMBLY =======
    unsigned char magicbyte[4] = {0xEF, 0xBE, 0x00, 0x00};

    // reset buffer if new frame begins or size overflow
    if ((memcmp(buf, magicbyte, 4) == 0) || ((buf85pointer + actual_length) >= BUF85SIZE)) {
        buf85pointer = 0;
    }

    memmove(buf85 + buf85pointer, buf, actual_length);
    buf85pointer += actual_length;

    if (memcmp(buf85, magicbyte, 4) != 0) {
        buf85pointer = 0;
        // giảm spam log nếu muốn: comment dòng dưới
        // printf("Reset buffer because of bad Magic Byte!\n");
        return;
    }

    uint32_t FrameSize   = buf85[ 8] + (buf85[ 9] << 8) + (buf85[10] << 16) + (buf85[11] << 24);
    uint32_t ThermalSize = buf85[12] + (buf85[13] << 8) + (buf85[14] << 16) + (buf85[15] << 24);
    uint32_t JpgSize     = buf85[16] + (buf85[17] << 8) + (buf85[18] << 16) + (buf85[19] << 24);
    uint32_t StatusSize  = buf85[20] + (buf85[21] << 8) + (buf85[22] << 16) + (buf85[23] << 24);
    (void)ThermalSize; (void)JpgSize; (void)StatusSize;

    if ((FrameSize + 28) > (uint32_t)buf85pointer) {
        return; // wait next chunk
    }

    // ======= FPS bookkeeping =======
    t1 = t2;
    gettimeofday(&t2, NULL);
    filecount++;

    // reset pointer for next frame
    buf85pointer = 0;

    // ======= BUFFERS (NO malloc/free per frame) =======
    static std::vector<unsigned short> pix_buf(160 * 120);
    static std::vector<unsigned char>  fb_proc_buf(160 * 128);       // grayscale
    static std::vector<unsigned char>  fb_proc2_buf(160 * 128 * 3);  // rgb

    unsigned short* pix = pix_buf.data();
    unsigned char* fb_proc = fb_proc_buf.data();
    unsigned char* fb_proc2 = fb_proc2_buf.data();

    memset(fb_proc, 128, 160 * 128);

    // ======= PARSE RAW + MIN/MAX =======
    int minv = 0x10000;
    int maxv = 0;
    int maxx = 0, maxy = 0;

    for (int y = 0; y < 120; ++y) {
        for (int x = 0; x < 160; ++x) {
            int v;
            if (x < 80)
                v = buf85[2*(y * 164 + x) + 32] + 256 * buf85[2*(y * 164 + x) + 33];
            else
                v = buf85[2*(y * 164 + x) + 32 + 4] + 256 * buf85[2*(y * 164 + x) + 33 + 4];

            pix[y * 160 + x] = (unsigned short)v;

            if (v < minv) minv = v;
            if (v > maxv) { maxv = v; maxx = x; maxy = y; }
        }
    }

    memcpy(g_pix, pix, 160 * 120 * sizeof(unsigned short));

    // ======= LUT TEMPERATURE (nhanh) =======
    static std::vector<float> tempLUT(65536);
    static bool lut_init = false;
    static double lut_Ta = 1e9;
    static double lut_eps = 0.98;
    static int lut_lo = 65535, lut_hi = 0;

    const double emissivity = 0.98;

    // chỉ fill LUT quanh min/max để tiết kiệm
    int want_lo = std::max(0,     minv - 512);
    int want_hi = std::min(65535, maxv + 512);

    if (!lut_init || std::fabs(lut_Ta - Ta) > 0.1 || std::fabs(lut_eps - emissivity) > 1e-9) {
        lut_init = true;
        lut_Ta = Ta;
        lut_eps = emissivity;
        lut_lo = 65535;
        lut_hi = 0;
    }

    if (want_lo < lut_lo) {
        for (int raw = want_lo; raw < lut_lo; ++raw) {
            tempLUT[raw] = (float)raw2temperature((unsigned short)raw, Ta, emissivity);
        }
        lut_lo = want_lo;
    }
    if (want_hi > lut_hi) {
        for (int raw = lut_hi + 1; raw <= want_hi; ++raw) {
            tempLUT[raw] = (float)raw2temperature((unsigned short)raw, Ta, emissivity);
        }
        lut_hi = want_hi;
    }

    // ======= SCALE RAW -> 8bit gray =======
    int delta = maxv - minv;
    if (!delta) delta = 1;
    int scale = 0x10000 / delta;

    for (int y = 0; y < 120; ++y) {
        for (int x = 0; x < 160; ++x) {
            int vv = ((int)pix[y * 160 + x] - minv) * scale >> 8;
            fb_proc[y * 160 + x] = (unsigned char)vv;
        }
    }

    // ======= OVERLAY TEXT =======
    char st1[128];
    char st2[128];

    struct tm *loctime = localtime(&now1);
    strftime(st1, 60, "%H:%M:%S", loctime);

    int med = (pix[59 * 160 + 79] + pix[59 * 160 + 80] + pix[60 * 160 + 79] + pix[60 * 160 + 80]) / 4;

    snprintf(st2, sizeof(st2),
             " Ta=%.1f RH=%.0f%% %s  %.1f/%.1f/%.1f'C",
             Ta, RH, dht_ok ? "OK" : "NO",
             tempLUT[(unsigned short)minv],
             tempLUT[(unsigned short)med],
             tempLUT[(unsigned short)maxv]);

    strncat(st1, st2, sizeof(st1) - strlen(st1) - 1);

    // giới hạn ký tự để font_write không tràn
    constexpr int OVERLAY_MAX = 26;
    strncpy(st2, st1, OVERLAY_MAX);
    st2[OVERLAY_MAX - 1] = '\0';

    font_write(fb_proc, 1, 120, st2);

    // crosshair
    font_write(fb_proc, 80 - 2, 60 - 3, "+");

    // marker max
    maxx -= 4; maxy -= 4;
    if (maxx < 0) maxx = 0;
    if (maxy < 0) maxy = 0;
    if (maxx > 150) maxx = 150;
    if (maxy > 110) maxy = 110;

    font_write(fb_proc, 160 - 6, maxy, "<");
    font_write(fb_proc, maxx, 120 - 8, "|");

    // ======= COLORMAP -> RGB =======
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 160; ++x) {
            int vv = fb_proc[y * 160 + x];
            fb_proc2[3 * (y * 160 + x) + 0] = colormap[3 * vv + 0];
            fb_proc2[3 * (y * 160 + x) + 1] = colormap[3 * vv + 1];
            fb_proc2[3 * (y * 160 + x) + 2] = colormap[3 * vv + 2];
        }
    }

    // ======= VIEW MODE (OpenCV) =======
    static bool win_init = false;
    if (!win_init) {
        cv::namedWindow("Nguyen Le Cong Hieu - VVLYSH", cv::WINDOW_NORMAL);
        cv::resizeWindow("Nguyen Le Cong Hieu - VVLYSH", 160 * g_scale, 128 * g_scale);
        cv::setMouseCallback("Nguyen Le Cong Hieu - VVLYSH", on_mouse);
        win_init = true;
    }

    cv::Mat rgb(128, 160, CV_8UC3, fb_proc2);
    cv::Mat bgr;
    cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);

    cv::Mat bgr_big;
    cv::resize(bgr, bgr_big, cv::Size(), g_scale, g_scale, cv::INTER_NEAREST);

    // ======= CLICK TEMP (LUT) =======
    if (g_mouse_click &&
        g_mouse_x >= 0 && g_mouse_x < 160 &&
        g_mouse_y >= 0 && g_mouse_y < 120)
    {
        unsigned short raw = g_pix[g_mouse_y * 160 + g_mouse_x];
        double temp = tempLUT[raw];

        char tbuf[64];
        snprintf(tbuf, sizeof(tbuf), "%.2f C", temp);

        cv::circle(bgr_big,
                   cv::Point(g_mouse_x * g_scale, g_mouse_y * g_scale),
                   6, cv::Scalar(0, 0, 255), 2);

        cv::putText(bgr_big, tbuf,
                    cv::Point(g_mouse_x * g_scale + 8, g_mouse_y * g_scale - 8),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 2);
    }

    // ======= ROI AVG (tính mỗi 3 frame) =======
    static int roi_skip = 0;
    roi_skip = (roi_skip + 1) % 3;

    if (g_roi_valid && roi_skip == 0) {
        int x0 = std::min(g_x0, g_x1);
        int x1 = std::max(g_x0, g_x1);
        int y0 = std::min(g_y0, g_y1);
        int y1 = std::max(g_y0, g_y1);

        x0 = std::max(0, std::min(159, x0));
        x1 = std::max(0, std::min(159, x1));
        y0 = std::max(0, std::min(119, y0));
        y1 = std::max(0, std::min(119, y1));

        double sum = 0.0;
        int count = 0;

        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                sum += tempLUT[g_pix[y * 160 + x]];
                count++;
            }
        }

        if (count > 0) {
            double avg = sum / count;

            cv::rectangle(bgr_big,
                          cv::Point(x0 * g_scale, y0 * g_scale),
                          cv::Point(x1 * g_scale, y1 * g_scale),
                          cv::Scalar(0, 255, 0), 2);

            char abuf[64];
            snprintf(abuf, sizeof(abuf), "AVG: %.2f C", avg);

            cv::putText(bgr_big, abuf,
                        cv::Point(x0 * g_scale + 5, y0 * g_scale - 8),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
        }
    }

    cv::imshow("Nguyen Le Cong Hieu - VVLYSH", bgr_big);

    int key = cv::waitKey(1);
    if (key == 27) { // ESC
        exit(0);
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
         if (r < 0) {
                if (strcmp (EP_error, libusb_error_name(r))!=0)
                {       
                    strcpy(EP_error, libusb_error_name(r));
                    fprintf(stderr, "\n: %s >>>>>>>>>>>>>>>>>bulk transfer (in) %s:%i %s\n", ctime(&now1), ep , r, libusb_error_name(r));
                    sleep(1);
                }
                //return 1;
        } else
        {           
            printf("\n: %s bulk read EP %s, actual length %d\nHEX:\n",ctime(&now1), ep ,actual_length);
            // write frame to file          
  /*
            char filename[100];
            sprintf(filename, "EP%s#%05i.bin",ep,filecount);
            filecount++;
            FILE *file = fopen(filename, "wb");
            fwrite(buf, 1, actual_length, file);
            fclose(file);
  */         
          // hex print of first byte
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

    unsigned char buf[1048576];
    int actual_length = 0;

    time_t now;
    unsigned char data[2] = {0, 0};

    char EP81_error[50] = "";
    char EP83_error[50] = "";
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
            printf("\n: %s", ctime(&now));

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
        if (strcmp(libusb_error_name(r), "LIBUSB_ERROR_NO_DEVICE") == 0) {
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
    size_t n = fread(colormap, sizeof(unsigned char), 768, fp);
    fclose(fp);
    if (n != 768) {
        fprintf(stderr, "Palette read error (need 768 bytes)\n");
        return 1;
    }

    // ---- Start DHT12 thread (I2C addr 0x5C) ----
    start_dht12_thread();

    while (1) {
        EPloop(colormap);
    }
    return 0;
}
 
