//
// Created by Otto Winter on 26.11.17.
//
// Based on:
//   - https://github.com/NachtRaveVL/PCA9685-Arduino
//   - https://cdn-shop.adafruit.com/datasheets/PCA9685.pdf

#include "esphomelib/output/pca9685_output_component.h"

#include "esphomelib/log.h"

#ifdef USE_PCA9685_OUTPUT

ESPHOMELIB_NAMESPACE_BEGIN

namespace output {

static const char *TAG = "output.pca9685";

const uint8_t PCA9685_MODE_INVERTED = 0x10;
const uint8_t PCA9685_MODE_OUTPUT_ONACK = 0x08;
const uint8_t PCA9685_MODE_OUTPUT_TOTEM_POLE = 0x04;
const uint8_t PCA9685_MODE_OUTNE_HIGHZ = 0x02;
const uint8_t PCA9685_MODE_OUTNE_LOW = 0x01;

static const uint8_t PCA9685_REGISTER_SOFTWARE_RESET = 0x06;
static const uint8_t PCA9685_REGISTER_MODE1 = 0x00;
static const uint8_t PCA9685_REGISTER_MODE2 = 0x01;
static const uint8_t PCA9685_REGISTER_LED0 = 0x06;
static const uint8_t PCA9685_REGISTER_PRE_SCALE = 0xFE;

static const uint8_t PCA9685_MODE1_RESTART = 0b10000000;
static const uint8_t PCA9685_MODE1_AUTOINC = 0b00100000;
static const uint8_t PCA9685_MODE1_SLEEP = 0b00010000;

static const uint16_t PCA9685_PWM_FULL = 4096;

static const uint8_t PCA9685_ADDRESS = 0x40;

PCA9685OutputComponent::PCA9685OutputComponent(I2CComponent *parent, float frequency,
                                               uint8_t mode)
    : I2CDevice(parent, PCA9685_ADDRESS), frequency_(frequency),
      mode_(mode), min_channel_(0xFF), max_channel_(0x00), update_(true) {
  for (uint16_t &pwm_amount : this->pwm_amounts_)
    pwm_amount = 0;
}

void PCA9685OutputComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up PCA9685OutputComponent.");
  ESP_LOGCONFIG(TAG, "    Mode: 0x%02X", this->mode_);

  ESP_LOGV(TAG, "    Resetting devices...");
  this->write_bytes(PCA9685_REGISTER_SOFTWARE_RESET, nullptr, 0);

  this->write_byte(PCA9685_REGISTER_MODE1, PCA9685_MODE1_RESTART | PCA9685_MODE1_AUTOINC);
  this->write_byte(PCA9685_REGISTER_MODE2, this->mode_);

  ESP_LOGCONFIG(TAG, "    Frequency: %.0f", this->frequency_);

  int pre_scaler = (25000000 / (4096 * this->frequency_)) - 1;
  if (pre_scaler > 255) pre_scaler = 255;
  if (pre_scaler < 3) pre_scaler = 3;

  ESP_LOGV(TAG, "     -> Prescaler: %d", pre_scaler);

  uint8_t mode1;
  this->read_byte(PCA9685_REGISTER_MODE1, &mode1);
  mode1 = (mode1 & ~PCA9685_MODE1_RESTART) | PCA9685_MODE1_SLEEP;
  this->write_byte(PCA9685_REGISTER_MODE1, mode1);
  this->write_byte(PCA9685_REGISTER_PRE_SCALE, pre_scaler);

  mode1 = (mode1 & ~PCA9685_MODE1_SLEEP) | PCA9685_MODE1_RESTART;
  this->write_byte(PCA9685_REGISTER_MODE1, mode1);
  delayMicroseconds(500);

  this->loop();
}

void PCA9685OutputComponent::loop() {
  if (this->min_channel_ == 0xFF || !this->update_)
    return;

  uint8_t data[16*4];
  uint8_t len = 0;
  const uint16_t num_channels = this->max_channel_ - this->min_channel_ + 1;
  for (uint8_t channel = this->min_channel_; channel <= this->max_channel_; channel++) {
    uint16_t phase_begin = uint16_t(channel - this->min_channel_) / num_channels * 4096 ;
    uint16_t phase_end;
    uint16_t amount = this->pwm_amounts_[channel];
    if (amount == 0) {
      phase_end = 4096;
    } else if (amount >= 4096) {
      phase_begin = 4096;
      phase_end = 0;
    } else {
      phase_end = phase_begin + amount;
      if (phase_end >= 4096)
        phase_end -= 4096;
    }

    ESP_LOGVV(TAG, "Channel %02u: amount=%04u phase_begin=%04u phase_end=%04u", channel, amount, phase_begin, phase_end);

    data[len++] = phase_begin & 0xFF;
    data[len++] = (phase_begin >> 8) & 0xFF;
    data[len++] = phase_end & 0xFF;
    data[len++] = (phase_end >> 8) & 0xFF;
  }
  this->write_bytes(PCA9685_REGISTER_LED0 + 4 * this->min_channel_, data, len);

  this->update_ = false;
}

float PCA9685OutputComponent::get_setup_priority() const {
  return setup_priority::HARDWARE;
}

void PCA9685OutputComponent::set_channel_value(uint8_t channel, uint16_t value) {
  if (this->pwm_amounts_[channel] != value)
    this->update_ = true;
  this->pwm_amounts_[channel] = value;
}

PCA9685OutputComponent::Channel *PCA9685OutputComponent::create_channel(uint8_t channel,
                                                                        PowerSupplyComponent *power_supply,
                                                                        float max_power) {
  ESP_LOGV(TAG, "Getting channel %d...", channel);
  this->min_channel_ = std::min(this->min_channel_, channel);
  this->max_channel_ = std::max(this->max_channel_, channel);
  auto *c = new Channel(this, channel);
  c->set_power_supply(power_supply);
  c->set_max_power(max_power);
  return c;
}

float PCA9685OutputComponent::get_frequency() const {
  return this->frequency_;
}
void PCA9685OutputComponent::set_frequency(float frequency) {
  this->frequency_ = frequency;
}
uint8_t PCA9685OutputComponent::get_mode() const {
  return this->mode_;
}
void PCA9685OutputComponent::set_mode(uint8_t mode) {
  this->mode_ = mode;
}

PCA9685OutputComponent::Channel::Channel(PCA9685OutputComponent *parent, uint8_t channel)
    : FloatOutput(), parent_(parent), channel_(channel) {}

void PCA9685OutputComponent::Channel::write_state(float state) {
  const uint16_t max_duty = 4096;
  auto duty = uint16_t(state * max_duty);
  this->parent_->set_channel_value(this->channel_, duty);
}

} // namespace output

ESPHOMELIB_NAMESPACE_END

#endif //USE_PCA9685_OUTPUT
