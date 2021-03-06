//
//  pcf8574_component.cpp
//  esphomelib
//
//  Created by Otto Winter on 05.05.18.
//  Copyright © 2018 Otto Winter. All rights reserved.
//
// Based on:
//   - https://github.com/skywodd/pcf8574_arduino_library/
//   - http://www.ti.com/lit/ds/symlink/pcf8574.pdf

#include "esphomelib/io/pcf8574_component.h"
#include "esphomelib/log.h"

#ifdef USE_PCF8574

ESPHOMELIB_NAMESPACE_BEGIN

namespace io {

static const char *TAG = "io.pcf8574";

PCF8574Component::PCF8574Component(I2CComponent *parent, uint8_t address, bool pcf8575)
    : Component(), I2CDevice(parent, address), pcf8575_(pcf8575) {}

void PCF8574Component::setup() {
  ESP_LOGCONFIG(TAG, "Setting up PCF8574...");
  ESP_LOGCONFIG(TAG, "    Address: 0x%02X", this->address_);
  ESP_LOGCONFIG(TAG, "    Is PCF8575: %s", this->pcf8575_ ? "YES" : "NO");
  if (!this->read_gpio_()) {
    ESP_LOGE(TAG, "PCF8574 not available under 0x%02X", this->address_);
    this->mark_failed();
    return;
  }

  this->write_gpio_();
  this->read_gpio_();
}
bool PCF8574Component::digital_read_(uint8_t pin) {
  this->read_gpio_();
  return this->input_mask_ & (1 << pin);
}
void PCF8574Component::digital_write_(uint8_t pin, bool value) {
  if (value) {
    this->port_mask_ |= (1 << pin);
  } else {
    this->port_mask_ &= ~(1 << pin);
  }

  this->write_gpio_();
}
void PCF8574Component::pin_mode_(uint8_t pin, uint8_t mode) {
  switch (mode) {
    case PCF8574_INPUT:
      this->ddr_mask_ &= ~(1 << pin);
      this->port_mask_ &= ~(1 << pin);
      break;
    case PCF8574_INPUT_PULLUP:
      this->ddr_mask_ &= ~(1 << pin);
      this->port_mask_ |= (1 << pin);
      break;
    case PCF8574_OUTPUT:
      this->ddr_mask_ |= (1 << pin);
      this->port_mask_ &= ~(1 << pin);
      break;
    default:
      assert(false);
  }

  this->write_gpio_();
}
bool PCF8574Component::read_gpio_() {
  if (this->is_failed())
    return false;

  if (this->pcf8575_) {
    if (!this->parent_->receive_16_(this->address_, &this->input_mask_, 1))
      return false;
  } else {
    uint8_t data;
    if (!this->parent_->receive_(this->address_, &data, 1))
      return false;
    this->input_mask_ = data;
  }

  return true;
}
bool PCF8574Component::write_gpio_() {
  if (this->is_failed())
    return false;

  uint16_t value = (this->input_mask_ & ~this->ddr_mask_) | this->port_mask_;

  this->parent_->begin_transmission_(this->address_);
  uint8_t data = value & 0xFF;
  this->parent_->write_(this->address_, &data, 1);

  if (this->pcf8575_) {
    data = (value >> 8) & 0xFF;
    this->parent_->write_(this->address_, &data, 1);
  }
  return this->parent_->end_transmission_(this->address_);
}
PCF8574GPIOInputPin PCF8574Component::make_input_pin(uint8_t pin, uint8_t mode, bool inverted) {
  assert(mode <= PCF8574_OUTPUT);
  if (this->pcf8575_) { assert(pin < 16); }
  else { assert(pin < 8); }
  return {this, pin, mode, inverted};
}
PCF8574GPIOOutputPin PCF8574Component::make_output_pin(uint8_t pin, bool inverted) {
  if (this->pcf8575_) { assert(pin < 16); }
  else { assert(pin < 8); }
  return {this, pin, PCF8574_OUTPUT, inverted};
}

void PCF8574GPIOInputPin::setup() {
  this->pin_mode(this->mode_);
}
bool PCF8574GPIOInputPin::digital_read() {
  return this->parent_->digital_read_(this->pin_) != this->inverted_;
}
void PCF8574GPIOInputPin::digital_write(bool value) {
  this->parent_->digital_write_(this->pin_, value != this->inverted_);
}
PCF8574GPIOInputPin::PCF8574GPIOInputPin(PCF8574Component *parent, uint8_t pin, uint8_t mode, bool inverted)
    : GPIOInputPin(pin, mode, inverted), parent_(parent) {}
GPIOPin *PCF8574GPIOInputPin::copy() const {
  return new PCF8574GPIOInputPin(*this);
}
void PCF8574GPIOInputPin::pin_mode(uint8_t mode) {
  this->parent_->pin_mode_(this->pin_, this->mode_);
}

void PCF8574GPIOOutputPin::setup() {
  this->pin_mode(this->mode_);
}
bool PCF8574GPIOOutputPin::digital_read() {
  return this->parent_->digital_read_(this->pin_) != this->inverted_;
}
void PCF8574GPIOOutputPin::digital_write(bool value) {
  this->parent_->digital_write_(this->pin_, value != this->inverted_);
}
PCF8574GPIOOutputPin::PCF8574GPIOOutputPin(PCF8574Component *parent, uint8_t pin, uint8_t mode, bool inverted)
    : GPIOOutputPin(pin, mode, inverted), parent_(parent) {}
GPIOPin *PCF8574GPIOOutputPin::copy() const {
  return new PCF8574GPIOOutputPin(*this);
}
void PCF8574GPIOOutputPin::pin_mode(uint8_t mode) {
  this->parent_->pin_mode_(this->pin_, this->mode_);
}

} // namespace io

ESPHOMELIB_NAMESPACE_END

#endif //USE_PCF8574
