#include "servo_driver.h"

#include <array>
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
    std::lock_guard<std::mutex> lock(mutex_);
    return initLocked();
}

bool PositionalServoDriver::sortChip(bool good)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ && !initLocked()) {
        return false;
    }

    const SortDirection direction = (good == config_.good_to_left)
        ? SortDirection::Left
        : SortDirection::Right;
    const bool first_left = direction == SortDirection::Left;
    const int first_pulse_us = first_left
        ? config_.base_left_25_us
        : config_.base_right_25_us;
    const int sweep_target_us = first_left
        ? config_.base_right_max_us
        : config_.base_left_max_us;

    std::fprintf(stdout, "[SERVO] start positional sort result=%s direction=%s\n",
                 good ? "good" : "bad", first_left ? "left" : "right");
    std::fflush(stdout);

    if (!homeLocked()) {
        return false;
    }
    actionDelayLocked();

    if (!movePulseGraduallyLocked(config_.base_channel,
                                  base_current_us_,
                                  first_pulse_us,
                                  first_left ? "base left 25deg" : "base right 25deg",
                                  config_.positional_step_delay_us)) {
        homeLocked();
        return false;
    }
    actionDelayLocked();

    if (!movePulseGraduallyLocked(config_.arm_channel,
                                  lift_current_us_,
                                  config_.lift_down_us,
                                  "lift down",
                                  config_.positional_step_delay_us)) {
        homeLocked();
        return false;
    }
    actionDelayLocked();

    if (!movePulseGraduallyLocked(config_.base_channel,
                                  base_current_us_,
                                  sweep_target_us,
                                  first_left ? "base sweep right" : "base sweep left",
                                  config_.positional_sweep_step_delay_us)) {
        homeLocked();
        return false;
    }
    actionDelayLocked();

    if (!movePulseGraduallyLocked(config_.arm_channel,
                                  lift_current_us_,
                                  config_.lift_home_us,
                                  "lift home",
                                  config_.positional_step_delay_us)) {
        homeLocked();
        return false;
    }
    actionDelayLocked();

    if (!movePulseGraduallyLocked(config_.base_channel,
                                  base_current_us_,
                                  config_.base_center_us,
                                  "base center",
                                  config_.positional_step_delay_us)) {
        homeLocked();
        return false;
    }

    std::fprintf(stdout, "[SERVO] positional sort complete; servos remain at standby\n");
    std::fflush(stdout);
    return true;
}

bool PositionalServoDriver::stopAll()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pca_.isOpen()) {
        return true;
    }
    return homeLocked();
}

bool PositionalServoDriver::initLocked()
{
    if (initialized_) {
        return true;
    }
    if (!validateConfigLocked()) {
        return false;
    }
    if (!pca_.init(config_.i2c_bus, config_.i2c_addr, config_.pwm_freq_hz)) {
        return false;
    }
    if (!homeLocked()) {
        pca_.close();
        return false;
    }
    initialized_ = true;
    std::fprintf(stdout, "[SERVO] positional servos initialized at standby\n");
    std::fflush(stdout);
    return true;
}

bool PositionalServoDriver::validateConfigLocked() const
{
    if (config_.i2c_bus < 0 || config_.i2c_addr <= 0 || config_.pwm_freq_hz <= 0) {
        std::fprintf(stderr, "[ERROR] invalid positional servo bus/address/frequency\n");
        return false;
    }
    if (config_.base_channel < 0 || config_.base_channel > 15 ||
        config_.arm_channel < 0 || config_.arm_channel > 15 ||
        config_.base_channel == config_.arm_channel) {
        std::fprintf(stderr,
                     "[ERROR] invalid positional servo channels base=%d lift=%d\n",
                     config_.base_channel, config_.arm_channel);
        return false;
    }
    if (config_.min_safe_us <= 0 || config_.min_safe_us >= config_.max_safe_us ||
        config_.positional_step_us <= 0 || config_.positional_step_delay_us <= 0 ||
        config_.positional_sweep_step_delay_us <= 0 ||
        config_.positional_action_delay_ms < 0) {
        std::fprintf(stderr,
                     "[ERROR] invalid positional servo config range=%d-%dus step=%dus "
                     "step_delay=%dus sweep_step_delay=%dus action_delay=%dms\n",
                     config_.min_safe_us, config_.max_safe_us,
                     config_.positional_step_us, config_.positional_step_delay_us,
                     config_.positional_sweep_step_delay_us,
                     config_.positional_action_delay_ms);
        return false;
    }

    const std::array<int, 7> pulses = {
        config_.base_center_us,
        config_.base_left_25_us,
        config_.base_right_25_us,
        config_.base_left_max_us,
        config_.base_right_max_us,
        config_.lift_home_us,
        config_.lift_down_us
    };
    for (int pulse_us : pulses) {
        if (pulse_us < config_.min_safe_us || pulse_us > config_.max_safe_us) {
            std::fprintf(stderr,
                         "[ERROR] positional servo pulse %dus outside safe range %d-%dus\n",
                         pulse_us, config_.min_safe_us, config_.max_safe_us);
            return false;
        }
    }
    return true;
}

bool PositionalServoDriver::setPulseLocked(int channel, int pulse_us, const char* action)
{
    if (pulse_us < config_.min_safe_us || pulse_us > config_.max_safe_us) {
        std::fprintf(stderr, "[ERROR] reject unsafe servo action=%s pulse=%dus\n",
                     action, pulse_us);
        return false;
    }
    std::fprintf(stdout, "[SERVO] %s: channel=%d pulse=%dus\n",
                 action, channel, pulse_us);
    std::fflush(stdout);
    if (!pca_.setServoPulseUs(channel, pulse_us)) {
        std::fprintf(stderr, "[ERROR] positional servo action failed: %s\n", action);
        return false;
    }
    return true;
}

bool PositionalServoDriver::movePulseGraduallyLocked(int channel,
                                                     int& current_pulse_us,
                                                     int target_pulse_us,
                                                     const char* action,
                                                     int step_delay_us)
{
    if (current_pulse_us < config_.min_safe_us || current_pulse_us > config_.max_safe_us ||
        target_pulse_us < config_.min_safe_us || target_pulse_us > config_.max_safe_us) {
        std::fprintf(stderr,
                     "[ERROR] reject unsafe gradual servo action=%s current=%dus target=%dus\n",
                     action, current_pulse_us, target_pulse_us);
        return false;
    }
    if (current_pulse_us == target_pulse_us) {
        return true;
    }

    std::fprintf(stdout,
                 "[SERVO] %s gradual: channel=%d from=%dus to=%dus step=%dus delay=%dus\n",
                 action, channel, current_pulse_us, target_pulse_us,
                 config_.positional_step_us, step_delay_us);
    std::fflush(stdout);

    while (current_pulse_us != target_pulse_us) {
        const bool increasing = target_pulse_us > current_pulse_us;
        const int remaining_us = increasing
            ? target_pulse_us - current_pulse_us
            : current_pulse_us - target_pulse_us;
        const int step_us = remaining_us < config_.positional_step_us
            ? remaining_us
            : config_.positional_step_us;
        const int next_pulse_us = current_pulse_us + (increasing ? step_us : -step_us);

        if (!pca_.setServoPulseUs(channel, next_pulse_us)) {
            std::fprintf(stderr,
                         "[ERROR] gradual servo action failed: %s pulse=%dus\n",
                         action, next_pulse_us);
            return false;
        }
        current_pulse_us = next_pulse_us;
        std::this_thread::sleep_for(
            std::chrono::microseconds(step_delay_us));
    }
    return true;
}

bool PositionalServoDriver::homeLocked()
{
    if (!positions_known_) {
        const bool base_ok = setPulseLocked(config_.base_channel,
                                            config_.base_center_us,
                                            "base center");
        const bool lift_ok = setPulseLocked(config_.arm_channel,
                                            config_.lift_home_us,
                                            "lift home");
        if (base_ok && lift_ok) {
            base_current_us_ = config_.base_center_us;
            lift_current_us_ = config_.lift_home_us;
            positions_known_ = true;
        }
        return base_ok && lift_ok;
    }

    const bool base_ok = movePulseGraduallyLocked(config_.base_channel,
                                                   base_current_us_,
                                                   config_.base_center_us,
                                                   "base center",
                                                   config_.positional_step_delay_us);
    const bool lift_ok = movePulseGraduallyLocked(config_.arm_channel,
                                                   lift_current_us_,
                                                   config_.lift_home_us,
                                                   "lift home",
                                                   config_.positional_step_delay_us);
    return base_ok && lift_ok;
}

void PositionalServoDriver::actionDelayLocked() const
{
    if (config_.positional_action_delay_ms > 0) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(config_.positional_action_delay_ms));
    }
}

std::unique_ptr<ServoDriver> CreateContinuousServoDriver(const ServoConfig& config)
{
    return std::unique_ptr<ServoDriver>(new ContinuousServoDriver(config));
}

std::unique_ptr<ServoDriver> CreatePositionalServoDriver(const ServoConfig& config)
{
    return std::unique_ptr<ServoDriver>(new PositionalServoDriver(config));
}
