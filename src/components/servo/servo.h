/**
 * @file servo.h
 * @brief Servo Motor Control Library for Raspberry Pi using pigpio
 * @author Servo Control Library
 * @version 1.0.0
 *
 * This library provides simple PWM-based servo motor control for Raspberry Pi
 * using the pigpio library. It supports controlling multiple servo motors
 * on different GPIO pins simultaneously.
 */

#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>

/**
 * Servo structure to hold motor information
 */
typedef enum {
    SERVO_DRIVER_PIGPIO = 0,
    SERVO_DRIVER_PCA9685 = 1
} ServoDriver;

typedef struct {
    ServoDriver driver;
    uint8_t gpio;              /**< GPIO pin number or PCA9685 channel */
    uint16_t min_pulse_us;     /**< Minimum pulse width in microseconds (default: 1000) */
    uint16_t max_pulse_us;     /**< Maximum pulse width in microseconds (default: 2000) */
    uint16_t current_pulse_us; /**< Current pulse width in microseconds */
    int pi;                    /**< pigpiod handle for GPIO mode */
    int i2c_fd;                /**< I2C file descriptor for PCA9685 mode */
    uint8_t i2c_addr;          /**< I2C address for PCA9685 */
    uint8_t i2c_bus;           /**< I2C bus number for PCA9685 */
    double pwm_freq_hz;        /**< PWM frequency for PCA9685 mode */
} Servo;

/**
 * Initialize the servo library
 * Must be called before creating any servo objects
 * @return 0 on success, -1 on failure
 */
int servo_library_init(void);

/**
 * Cleanup the servo library
 * Should be called at the end to release resources
 */
void servo_library_cleanup(void);

/**
 * Create and initialize a new servo object using pigpio GPIO mode
 * @param gpio GPIO pin number (e.g., 17, 18, 27, etc.)
 * @param min_pulse_us Minimum pulse width in microseconds (typically 1000)
 * @param max_pulse_us Maximum pulse width in microseconds (typically 2000)
 * @return Pointer to Servo object, or NULL on failure
 */
Servo* servo_create(uint8_t gpio, uint16_t min_pulse_us, uint16_t max_pulse_us);

/**
 * Create and initialize a new servo object using PCA9685 I2C driver
 * @param channel PCA9685 output channel (0-15)
 * @param i2c_bus I2C bus number (e.g., 1 for /dev/i2c-1)
 * @param i2c_addr PCA9685 I2C address (typically 0x40)
 * @param pwm_freq_hz PWM frequency in Hz (typically 50)
 * @param min_pulse_us Minimum pulse width in microseconds (typically 1000)
 * @param max_pulse_us Maximum pulse width in microseconds (typically 2000)
 * @return Pointer to Servo object, or NULL on failure
 */
Servo* servo_create_pca9685(uint8_t channel, uint8_t i2c_bus, uint8_t i2c_addr,
                            double pwm_freq_hz,
                            uint16_t min_pulse_us, uint16_t max_pulse_us);

/**
 * Destroy a servo object and release resources
 * @param servo Pointer to servo object to destroy
 */
void servo_destroy(Servo *servo);

/**
 * Set servo angle (0-180 degrees)
 * @param servo Pointer to servo object
 * @param angle Angle in degrees (0-180)
 * @return 0 on success, -1 on failure
 */
int servo_set_angle(Servo *servo, double angle);

/**
 * Get current servo angle
 * @param servo Pointer to servo object
 * @return Current angle in degrees (0-180), -1 on failure
 */
double servo_get_angle(Servo *servo);

/**
 * Set servo pulse width directly (advanced)
 * @param servo Pointer to servo object
 * @param pulse_us Pulse width in microseconds
 * @return 0 on success, -1 on failure
 */
int servo_set_pulse(Servo *servo, uint16_t pulse_us);

/**
 * Get current servo pulse width
 * @param servo Pointer to servo object
 * @return Current pulse width in microseconds, -1 on failure
 */
int servo_get_pulse(Servo *servo);

/**
 * Stop servo (set pulse to 0)
 * @param servo Pointer to servo object
 * @return 0 on success, -1 on failure
 */
int servo_stop(Servo *servo);

/**
 * Enable servo output
 * @param servo Pointer to servo object
 * @return 0 on success, -1 on failure
 */
int servo_enable(Servo *servo);

/**
 * Disable servo output
 * @param servo Pointer to servo object
 * @return 0 on success, -1 on failure
 */
int servo_disable(Servo *servo);

#endif // SERVO_H
