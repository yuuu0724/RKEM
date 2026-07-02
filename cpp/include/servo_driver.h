#ifndef SERVO_DRIVER_H
#define SERVO_DRIVER_H

#include "pca9685_driver.h"

#include <memory>
#include <mutex>

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

    int min_safe_us = 1000;
    int max_safe_us = 2000;
    int base_center_us = 1500;
    int base_left_25_us = 1600;
    int base_right_25_us = 1400;
    int base_left_max_us = 1733;
    int base_right_max_us = 1267;
    int lift_home_us = 1700;
    int lift_down_us = 1400;
    int positional_step_us = 10;
    int positional_step_delay_us = 11250;
    int positional_sweep_step_delay_us = 11250;
    int positional_action_delay_ms = 100;

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
    bool initLocked();
    bool validateConfigLocked() const;
    bool setPulseLocked(int channel, int pulse_us, const char* action);
    bool movePulseGraduallyLocked(int channel,
                                  int& current_pulse_us,
                                  int target_pulse_us,
                                  const char* action,
                                  int step_delay_us);
    bool homeLocked();
    void actionDelayLocked() const;

private:
    ServoConfig config_;
    Pca9685Driver pca_;
    bool initialized_ = false;
    bool positions_known_ = false;
    int base_current_us_ = 0;
    int lift_current_us_ = 0;
    std::mutex mutex_;
};

std::unique_ptr<ServoDriver> CreateContinuousServoDriver(const ServoConfig& config);
std::unique_ptr<ServoDriver> CreatePositionalServoDriver(const ServoConfig& config);

#endif // SERVO_DRIVER_H
