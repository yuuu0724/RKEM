#include "servo_driver.h"

#include <chrono>
#include <cstdio>
#include <thread>

ContinuousServoDriver::ContinuousServoDriver(const ServoConfig& config)
    : config_(config)
{
}

ContinuousServoDriver::~ContinuousServoDriver()
{
    stopAll();
}

bool ContinuousServoDriver::init()
{
    if (initialized_) {
        return true;
    }
    if (!pca_.init(config_.i2c_bus, config_.i2c_addr, config_.pwm_freq_hz)) {
        return false;
    }
    initialized_ = stopAll();
    return initialized_;
}

bool ContinuousServoDriver::sortChip(bool good)
{
    if (!initialized_ && !init()) {
        return false;
    }
    const SortDirection direction = (good == config_.good_to_left)
        ? SortDirection::Left
        : SortDirection::Right;

    if (!armDown()) {
        return false;
    }
    if (!push(direction)) {
        stopAll();
        return false;
    }
    if (!armUp()) {
        stopAll();
        return false;
    }
    return stopAll();
}

bool ContinuousServoDriver::stopAll()
{
    bool ok = true;
    ok = pca_.stopServo(config_.base_channel, config_.stop_us) && ok;
    ok = pca_.stopServo(config_.arm_channel, config_.stop_us) && ok;
    return ok;
}

bool ContinuousServoDriver::rotateTimed(int channel, bool clockwise, int duration_ms)
{
    if (duration_ms <= 0) {
        return true;
    }
    const int pulse = clockwise ? config_.clockwise_us : config_.counterclockwise_us;
    if (!pca_.setServoPulseUs(channel, pulse)) {
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
    if (!pca_.stopServo(channel, config_.stop_us)) {
        return false;
    }
    if (config_.hold_after_move_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.hold_after_move_ms));
    }
    return true;
}

bool ContinuousServoDriver::armDown()
{
    return rotateTimed(config_.arm_channel, true, config_.arm_20deg_time_ms);
}

bool ContinuousServoDriver::armUp()
{
    return rotateTimed(config_.arm_channel, false, config_.arm_20deg_time_ms);
}

bool ContinuousServoDriver::push(SortDirection direction)
{
    const bool prepare_clockwise = direction == SortDirection::Left;
    if (!rotateTimed(config_.base_channel, prepare_clockwise, config_.base_10deg_time_ms)) {
        return false;
    }
    return rotateTimed(config_.base_channel, !prepare_clockwise, config_.base_70deg_time_ms);
}

PositionalServoDriver::PositionalServoDriver(const ServoConfig& config)
    : config_(config)
{
}

bool PositionalServoDriver::init()
{
    std::fprintf(stderr, "[WARN] positional servo driver is reserved but not implemented yet\n");
    return false;
}

bool PositionalServoDriver::sortChip(bool)
{
    return false;
}

bool PositionalServoDriver::stopAll()
{
    return true;
}

std::unique_ptr<ServoDriver> CreateContinuousServoDriver(const ServoConfig& config)
{
    return std::unique_ptr<ServoDriver>(new ContinuousServoDriver(config));
}
