#ifndef SERVO_DRIVER_H
#define SERVO_DRIVER_H

#include "pca9685_driver.h"

#include <memory>

enum class SortDirection {
    Left,
    Right
};

struct ServoConfig {
    int i2c_bus = 4;
    int i2c_addr = 0x40;
    int pwm_freq_hz = 50;

    int base_channel = 0;
    int arm_channel = 1;

    int stop_us = 1630;
    int clockwise_us = 1550;
    int counterclockwise_us = 1700;

    int arm_20deg_time_ms = 333;
    int base_10deg_time_ms = 167;
    int base_70deg_time_ms = 1167;
    int hold_after_move_ms = 80;

    bool good_to_left = true;
};

class ServoDriver {
public:
    virtual ~ServoDriver() = default;

    virtual bool init() = 0;
    virtual bool sortChip(bool good) = 0;
    virtual bool stopAll() = 0;
};

class ContinuousServoDriver : public ServoDriver {
public:
    explicit ContinuousServoDriver(const ServoConfig& config);
    ~ContinuousServoDriver() override;

    bool init() override;
    bool sortChip(bool good) override;
    bool stopAll() override;

private:
    bool rotateTimed(int channel, bool clockwise, int duration_ms);
    bool armDown();
    bool armUp();
    bool push(SortDirection direction);

private:
    ServoConfig config_;
    Pca9685Driver pca_;
    bool initialized_ = false;
};

class PositionalServoDriver : public ServoDriver {
public:
    explicit PositionalServoDriver(const ServoConfig& config);

    bool init() override;
    bool sortChip(bool good) override;
    bool stopAll() override;

private:
    ServoConfig config_;
};

std::unique_ptr<ServoDriver> CreateContinuousServoDriver(const ServoConfig& config);

#endif // SERVO_DRIVER_H
