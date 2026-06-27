#ifndef PCA9685_DRIVER_H
#define PCA9685_DRIVER_H

#include <cstdint>
#include <string>

class Pca9685Driver {
public:
    Pca9685Driver() = default;
    ~Pca9685Driver();

    Pca9685Driver(const Pca9685Driver&) = delete;
    Pca9685Driver& operator=(const Pca9685Driver&) = delete;

    bool init(int bus, int address, int freq_hz);
    void close();
    bool isOpen() const { return fd_ >= 0; }

    bool setServoPulseUs(int channel, int pulse_us);
    bool stopServo(int channel, int stop_pulse_us);

private:
    bool writeReg(uint8_t reg, uint8_t value);
    bool readReg(uint8_t reg, uint8_t& value);
    int usToTicks(int pulse_us) const;

private:
    int fd_ = -1;
    int freq_hz_ = 50;
    std::string device_path_;
};

#endif // PCA9685_DRIVER_H
