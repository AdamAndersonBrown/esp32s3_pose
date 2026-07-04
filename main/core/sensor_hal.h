#pragma once
#include <stdint.h>

// STAGE 1: HARDWARE ABSTRACTION LAYER (HAL)
// Goal: Convert raw silicon registers into a unified Body ENU Frame (East, North, Up)
// Accel ENU: +1g when axis points UP.
// Mag ENU: Positive when pointing NORTH or EAST. Negative when pointing UP (Earth field points down).

struct BodyVectors {
    float accel[3];
    float gyro[3];
    float mag[3];
};

static inline BodyVectors stage1_hal_transform(int16_t* sensors_data) {
    BodyVectors body;

    // Accelerometer 
    body.accel[0] = (float)sensors_data[4];
    body.accel[1] = (float)sensors_data[5];
    body.accel[2] = (float)sensors_data[6];

    // Gyroscope 
    body.gyro[0] = (float)sensors_data[7];
    body.gyro[1] = (float)sensors_data[8];
    body.gyro[2] = (float)sensors_data[9];

    // Magnetometer LSB Decoding
    float raw_mag_x = (float)(sensors_data[0]) / 8.0f;
    float raw_mag_y = (float)(sensors_data[1]) / 8.0f;
    float raw_mag_z = (float)(sensors_data[2]) / 2.0f;

    // Axis Alignment to ENU Body Frame (Derived from 6-Sided Box empirical data)
    body.mag[0] = raw_mag_x;   
    body.mag[1] = -raw_mag_y; 
    body.mag[2] = -raw_mag_z;  

    return body;
}
