/**
 * @file servo.c
 * @brief Servo Motor Control Library Implementation (pigpiod client mode)
 */

#define _POSIX_C_SOURCE 200112L

#include "servo.h"
#include <pigpiod_if2.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <errno.h>
#include <string.h>

/* Servo PWM frequency in Hz */
#define SERVO_FREQUENCY 50  /* 50 Hz = 20ms period for standard servo */

/* PCA9685 register definitions */
#define PCA9685_REG_MODE1         0x00
#define PCA9685_REG_MODE2         0x01
#define PCA9685_REG_PRE_SCALE     0xFE
#define PCA9685_REG_LED0_ON_L     0x06
#define PCA9685_REG_ALL_LED_ON_L  0xFA

#define PCA9685_MODE1_SLEEP       (1 << 4)
#define PCA9685_MODE1_AI          (1 << 5)
#define PCA9685_MODE2_OUTDRV      (1 << 2)

/* Global connection handle to pigpiod */
static int g_pi = -1;

static int pca9685_write_reg(int fd, uint8_t reg, uint8_t value) {
    uint8_t buf[2] = { reg, value };
    return (write(fd, buf, 2) == 2) ? 0 : -1;
}

static int pca9685_read_reg(int fd, uint8_t reg, uint8_t *value) {
    if (write(fd, &reg, 1) != 1) return -1;
    if (read(fd, value, 1) != 1) return -1;
    return 0;
}

static int pca9685_set_pwm(int fd, uint8_t channel, uint16_t on, uint16_t off) {
    if (channel > 15) return -1;
    uint8_t buf[5];
    uint8_t reg = PCA9685_REG_LED0_ON_L + 4 * channel;
    buf[0] = reg;
    buf[1] = on & 0xFF;
    buf[2] = (on >> 8) & 0x0F;
    buf[3] = off & 0xFF;
    buf[4] = (off >> 8) & 0x0F;
    return (write(fd, buf, 5) == 5) ? 0 : -1;
}

static int pca9685_open_bus(uint8_t bus, uint8_t addr) {
    char path[32];
    snprintf(path, sizeof(path), "/dev/i2c-%u", bus);
    int fd = open(path, O_RDWR);
    if (fd < 0) return -1;
    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int pca9685_set_pwm_freq(int fd, double freq_hz) {
    if (freq_hz <= 0.0) return -1;

    double prescaleval = 25000000.0 / (4096.0 * freq_hz) - 1.0;
    uint8_t prescale = (uint8_t)(floor(prescaleval + 0.5));

    uint8_t oldmode;
    if (pca9685_read_reg(fd, PCA9685_REG_MODE1, &oldmode) < 0) return -1;

    uint8_t sleepmode = (oldmode & 0x7F) | PCA9685_MODE1_SLEEP;
    if (pca9685_write_reg(fd, PCA9685_REG_MODE1, sleepmode) < 0) return -1;
    if (pca9685_write_reg(fd, PCA9685_REG_PRE_SCALE, prescale) < 0) return -1;
    if (pca9685_write_reg(fd, PCA9685_REG_MODE1, oldmode) < 0) return -1;
    usleep(5000);
    if (pca9685_write_reg(fd, PCA9685_REG_MODE1, oldmode | PCA9685_MODE1_AI) < 0) return -1;
    return 0;
}

/**
 * Initialize the servo library (connect to pigpiod daemon)
 */
int servo_library_init(void) {
    if (g_pi >= 0) return 0; /* already connected */

    /* Connect to local pigpiod (NULL,NULL) => localhost:8888 by default */
    g_pi = pigpio_start(NULL, NULL);
    if (g_pi < 0) {
        fprintf(stderr,
                "Error: Failed to connect to pigpiod (is pigpiod running?)\n");
        fprintf(stderr,
                "Hint: sudo systemctl enable --now pigpiod\n");
        return -1;
    }
    return 0;
}

/**
 * Cleanup the servo library (disconnect from pigpiod daemon)
 */
void servo_library_cleanup(void) {
    if (g_pi >= 0) {
        pigpio_stop(g_pi);
        g_pi = -1;
    }
}

/**
 * Create and initialize a new servo object using pigpio GPIO mode
 */
Servo* servo_create(uint8_t gpio, uint16_t min_pulse_us, uint16_t max_pulse_us) {
    if (g_pi < 0) {
        fprintf(stderr, "Error: servo_library_init() was not called or failed\n");
        return NULL;
    }

    Servo *servo = (Servo*)malloc(sizeof(Servo));
    if (servo == NULL) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        return NULL;
    }

    servo->driver = SERVO_DRIVER_PIGPIO;
    servo->gpio = gpio;
    servo->min_pulse_us = min_pulse_us;
    servo->max_pulse_us = max_pulse_us;
    servo->current_pulse_us = (min_pulse_us + max_pulse_us) / 2;
    servo->pi = g_pi;
    servo->i2c_fd = -1;
    servo->i2c_addr = 0;
    servo->i2c_bus = 0;
    servo->pwm_freq_hz = SERVO_FREQUENCY;

    if (set_mode(servo->pi, gpio, PI_OUTPUT) != 0) {
        fprintf(stderr, "Error: Failed to set GPIO %d as output\n", gpio);
        free(servo);
        return NULL;
    }

    if (set_servo_pulsewidth(servo->pi, gpio, servo->current_pulse_us) != 0) {
        fprintf(stderr, "Error: Failed to initialize servo on GPIO %d\n", gpio);
        free(servo);
        return NULL;
    }

    printf("Servo initialized on GPIO %d (pulse: %u-%u us) via pigpiod\n",
           gpio, min_pulse_us, max_pulse_us);

    return servo;
}

/**
 * Create and initialize a new servo object using PCA9685 I2C driver
 */
Servo* servo_create_pca9685(uint8_t channel, uint8_t i2c_bus, uint8_t i2c_addr,
                            double pwm_freq_hz,
                            uint16_t min_pulse_us, uint16_t max_pulse_us) {
    int fd = pca9685_open_bus(i2c_bus, i2c_addr);
    if (fd < 0) {
        fprintf(stderr, "Error: Failed to open I2C bus %u for PCA9685 at 0x%02x\n",
                i2c_bus, i2c_addr);
        return NULL;
    }

    if (pca9685_write_reg(fd, PCA9685_REG_MODE2, PCA9685_MODE2_OUTDRV) < 0 ||
        pca9685_write_reg(fd, PCA9685_REG_MODE1, PCA9685_MODE1_AI) < 0 ||
        pca9685_set_pwm_freq(fd, pwm_freq_hz) < 0) {
        fprintf(stderr, "Error: Failed to initialize PCA9685 device\n");
        close(fd);
        return NULL;
    }

    Servo *servo = (Servo*)malloc(sizeof(Servo));
    if (servo == NULL) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        close(fd);
        return NULL;
    }

    servo->driver = SERVO_DRIVER_PCA9685;
    servo->gpio = channel;
    servo->min_pulse_us = min_pulse_us;
    servo->max_pulse_us = max_pulse_us;
    servo->current_pulse_us = (min_pulse_us + max_pulse_us) / 2;
    servo->pi = -1;
    servo->i2c_fd = fd;
    servo->i2c_addr = i2c_addr;
    servo->i2c_bus = i2c_bus;
    servo->pwm_freq_hz = pwm_freq_hz;

    double period_us = 1000000.0 / pwm_freq_hz;
    uint16_t count = (uint16_t)(round((servo->current_pulse_us * 4096.0) / period_us));
    if (count > 4095) count = 4095;

    if (pca9685_set_pwm(fd, channel, 0, count) < 0) {
        fprintf(stderr, "Error: Failed to set initial servo position on PCA9685 channel %u\n",
                channel);
        close(fd);
        free(servo);
        return NULL;
    }

    printf("Servo initialized on PCA9685 channel %u at 0x%02x (bus %u) pulse: %u-%u us\n",
           channel, i2c_addr, i2c_bus, min_pulse_us, max_pulse_us);

    return servo;
}

/**
 * Destroy a servo object
 */
void servo_destroy(Servo *servo) {
    if (servo == NULL) return;

    if (servo->driver == SERVO_DRIVER_PIGPIO) {
        set_servo_pulsewidth(servo->pi, servo->gpio, 0);
        printf("Servo on GPIO %d destroyed\n", servo->gpio);
    } else if (servo->driver == SERVO_DRIVER_PCA9685) {
        pca9685_set_pwm(servo->i2c_fd, servo->gpio, 0, 0);
        close(servo->i2c_fd);
        printf("Servo on PCA9685 channel %d destroyed\n", servo->gpio);
    }

    free(servo);
}

/**
 * Set servo pulse width directly
 */
int servo_set_pulse(Servo *servo, uint16_t pulse_us) {
    if (servo == NULL) {
        fprintf(stderr, "Error: Servo pointer is NULL\n");
        return -1;
    }

    if (pulse_us < servo->min_pulse_us) {
        pulse_us = servo->min_pulse_us;
    } else if (pulse_us > servo->max_pulse_us) {
        pulse_us = servo->max_pulse_us;
    }

    if (servo->driver == SERVO_DRIVER_PIGPIO) {
        if (set_servo_pulsewidth(servo->pi, servo->gpio, pulse_us) != 0) {
            fprintf(stderr, "Error: Failed to set servo pulse on GPIO %d\n",
                    servo->gpio);
            return -1;
        }
    } else if (servo->driver == SERVO_DRIVER_PCA9685) {
        double period_us = 1000000.0 / servo->pwm_freq_hz;
        double count_double = (pulse_us * 4096.0) / period_us;
        uint16_t count = (uint16_t)(round(count_double));
        if (count > 4095) count = 4095;
        if (pca9685_set_pwm(servo->i2c_fd, servo->gpio, 0, count) != 0) {
            fprintf(stderr, "Error: Failed to set PCA9685 pulse on channel %d\n",
                    servo->gpio);
            return -1;
        }
    } else {
        fprintf(stderr, "Error: Unknown servo driver\n");
        return -1;
    }

    servo->current_pulse_us = pulse_us;
    return 0;
}

/**
 * Set servo angle (0-180 degrees)
 */
int servo_set_angle(Servo *servo, double angle) {
    if (servo == NULL) {
        fprintf(stderr, "Error: Servo pointer is NULL\n");
        return -1;
    }

    if (angle < 0.0 || angle > 180.0) {
        fprintf(stderr, "Error: Angle must be between 0 and 180 degrees\n");
        return -1;
    }

    double angle_fraction = angle / 180.0;
    uint16_t pulse_us = servo->min_pulse_us +
                        (uint16_t)(angle_fraction *
                                   (servo->max_pulse_us - servo->min_pulse_us));

    return servo_set_pulse(servo, pulse_us);
}

/**
 * Get current servo angle
 */
double servo_get_angle(Servo *servo) {
    if (servo == NULL) {
        fprintf(stderr, "Error: Servo pointer is NULL\n");
        return -1;
    }

    if (servo->current_pulse_us < servo->min_pulse_us) {
        return 0.0;
    }

    uint16_t pulse_range = servo->max_pulse_us - servo->min_pulse_us;
    if (pulse_range == 0) return 0.0;

    double angle = ((double)(servo->current_pulse_us - servo->min_pulse_us) /
                    pulse_range) * 180.0;
    return angle;
}

/**
 * Get current servo pulse width
 */
int servo_get_pulse(Servo *servo) {
    if (servo == NULL) {
        fprintf(stderr, "Error: Servo pointer is NULL\n");
        return -1;
    }
    return (int)servo->current_pulse_us;
}

/**
 * Stop servo
 */
int servo_stop(Servo *servo) {
    if (servo == NULL) {
        fprintf(stderr, "Error: Servo pointer is NULL\n");
        return -1;
    }

    if (servo->driver == SERVO_DRIVER_PIGPIO) {
        if (set_servo_pulsewidth(servo->pi, servo->gpio, 0) != 0) {
            fprintf(stderr, "Error: Failed to stop servo on GPIO %d\n",
                    servo->gpio);
            return -1;
        }
    } else if (servo->driver == SERVO_DRIVER_PCA9685) {
        if (pca9685_set_pwm(servo->i2c_fd, servo->gpio, 0, 0) != 0) {
            fprintf(stderr, "Error: Failed to stop PCA9685 servo on channel %d\n",
                    servo->gpio);
            return -1;
        }
    } else {
        fprintf(stderr, "Error: Unknown servo driver\n");
        return -1;
    }

    servo->current_pulse_us = 0;
    return 0;
}

/**
 * Enable servo output
 */
int servo_enable(Servo *servo) {
    if (servo == NULL) {
        fprintf(stderr, "Error: Servo pointer is NULL\n");
        return -1;
    }

    if (servo->driver == SERVO_DRIVER_PIGPIO) {
        if (set_servo_pulsewidth(servo->pi, servo->gpio, servo->current_pulse_us) != 0) {
            fprintf(stderr, "Error: Failed to enable servo on GPIO %d\n",
                    servo->gpio);
            return -1;
        }
    } else if (servo->driver == SERVO_DRIVER_PCA9685) {
        if (servo->current_pulse_us == 0) {
            servo->current_pulse_us = servo->min_pulse_us;
        }
        if (servo_set_pulse(servo, servo->current_pulse_us) != 0) {
            return -1;
        }
    } else {
        fprintf(stderr, "Error: Unknown servo driver\n");
        return -1;
    }

    return 0;
}

/**
 * Disable servo output
 */
int servo_disable(Servo *servo) {
    return servo_stop(servo);
}
