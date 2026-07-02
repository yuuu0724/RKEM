#include "pca9685_driver.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {
constexpr uint8_t kMode1 = 0x00;
constexpr uint8_t kMode2 = 0x01;
constexpr uint8_t kPrescale = 0xFE;
constexpr uint8_t kLed0OnL = 0x06;
}

Pca9685Driver::~Pca9685Driver()
{
    close();
}

bool Pca9685Driver::init(int bus, int address, int freq_hz)
{
    close();
    if (bus < 0 || address <= 0 || freq_hz <= 0) {
        std::fprintf(stderr, "[ERROR] invalid PCA9685 config bus=%d addr=0x%x freq=%d\n",
                     bus, address, freq_hz);
        return false;
    }

    device_path_ = "/dev/i2c-" + std::to_string(bus);
    fd_ = ::open(device_path_.c_str(), O_RDWR);
    if (fd_ < 0) {
        std::fprintf(stderr, "[ERROR] open %s failed: %s\n",
                     device_path_.c_str(), std::strerror(errno));
        return false;
    }
    if (ioctl(fd_, I2C_SLAVE, address) < 0) {
        std::fprintf(stderr, "[ERROR] ioctl I2C_SLAVE 0x%x failed: %s\n",
                     address, std::strerror(errno));
        close();
        return false;
    }

    freq_hz_ = freq_hz;
    const int prescale = std::max(3, std::min(255, static_cast<int>(std::round(25000000.0 / (4096.0 * freq_hz_) - 1.0))));

    uint8_t old_mode = 0;
    if (!readReg(kMode1, old_mode)) {
        close();
        return false;
    }
    const uint8_t sleep_mode = static_cast<uint8_t>((old_mode & 0x7f) | 0x10);
    if (!writeReg(kMode1, sleep_mode) ||
        !writeReg(kPrescale, static_cast<uint8_t>(prescale)) ||
        !writeReg(kMode2, 0x04) ||
        !writeReg(kMode1, old_mode)) {
        close();
        return false;
    }
    usleep(5000);
    if (!writeReg(kMode1, 0x80 | 0x20 | 0x01)) {
        close();
        return false;
    }
    std::fprintf(stdout, "[INFO] PCA9685 initialized on %s addr=0x%x freq=%dHz\n",
                 device_path_.c_str(), address, freq_hz_);
    std::fflush(stdout);
    return true;
}

void Pca9685Driver::close()
{
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool Pca9685Driver::setServoPulseUs(int channel, int pulse_us)
{
    if (fd_ < 0 || channel < 0 || channel > 15 || pulse_us <= 0) {
        return false;
    }
    const int ticks = usToTicks(pulse_us);
    if (ticks < 0 || ticks > 4095) {
        std::fprintf(stderr, "[ERROR] servo pulse out of range channel=%d pulse=%dus ticks=%d\n",
                     channel, pulse_us, ticks);
        return false;
    }

    const uint8_t base = static_cast<uint8_t>(kLed0OnL + 4 * channel);
    return writeReg(base, 0) &&
           writeReg(static_cast<uint8_t>(base + 1), 0) &&
           writeReg(static_cast<uint8_t>(base + 2), static_cast<uint8_t>(ticks & 0xff)) &&
           writeReg(static_cast<uint8_t>(base + 3), static_cast<uint8_t>((ticks >> 8) & 0x0f));
}

bool Pca9685Driver::stopServo(int channel, int stop_pulse_us)
{
    return setServoPulseUs(channel, stop_pulse_us);
}

bool Pca9685Driver::writeReg(uint8_t reg, uint8_t value)
{
    if (fd_ < 0) {
        return false;
    }
    const uint8_t data[2] = {reg, value};
    if (::write(fd_, data, sizeof(data)) != static_cast<ssize_t>(sizeof(data))) {
        std::fprintf(stderr, "[ERROR] PCA9685 write reg=0x%02x failed: %s\n",
                     reg, std::strerror(errno));
        return false;
    }
    return true;
}

bool Pca9685Driver::readReg(uint8_t reg, uint8_t& value)
{
    if (fd_ < 0) {
        return false;
    }
    if (::write(fd_, &reg, 1) != 1) {
        std::fprintf(stderr, "[ERROR] PCA9685 select reg=0x%02x failed: %s\n",
                     reg, std::strerror(errno));
        return false;
    }
    if (::read(fd_, &value, 1) != 1) {
        std::fprintf(stderr, "[ERROR] PCA9685 read reg=0x%02x failed: %s\n",
                     reg, std::strerror(errno));
        return false;
    }
    return true;
}

int Pca9685Driver::usToTicks(int pulse_us) const
{
    return (pulse_us * 4096 * freq_hz_ + 500000) / 1000000;
}
