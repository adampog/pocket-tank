/* imu_mpu6050.h - an MPU-6050 standing in for the QMI8658 (this fork: the 2.8" CYD has no IMU of its own; a
 * GY-521-style breakout on its I2C socket, docs/CYD.md). imu_port_qmi8658.c holds the orientation and handling logic
 * and asks this chip for its samples when no QMI8658 answers. Accel in counts at +-2 g, 16384 a g, as the QMI8658. */
#ifndef IMU_MPU6050_H
#define IMU_MPU6050_H
#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
bool imu_mpu6050_probe(i2c_master_bus_handle_t bus);   /* 0x68 then 0x69, WHO_AM_I, configure; false = none */
bool imu_mpu6050_reset_config(void);
bool imu_mpu6050_read(int16_t a[3]);                   /* the chip's own X Y Z */
void imu_mpu6050_sleep(void);
/* how the breakout is mounted (Kconfig, bench facts): the axis along the screen's up and its sign held right side
 * up; the axis out of the glass and its sign lying flat, screen up */
void imu_mpu6050_axes(int *up_axis, int *up_sign, int *out_axis, int *out_up_sign);
#endif
