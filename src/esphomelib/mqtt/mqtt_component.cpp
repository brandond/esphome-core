//
// Created by Otto Winter on 25.11.17.
//

#include "esphomelib/mqtt/mqtt_component.h"

#include <algorithm>
#include <utility>

#include "esphomelib/mqtt/mqtt_client_component.h"
#include "esphomelib/log.h"
#include "esphomelib/helpers.h"
#include "esphomelib/application.h"

ESPHOMELIB_NAMESPACE_BEGIN

namespace mqtt {

static const char *TAG = "mqtt.component";

void MQTTComponent::set_retain(bool retain) {
  this->retain_ = retain;
}

std::string MQTTComponent::get_discovery_topic(const MQTTDiscoveryInfo &discovery_info) const {
  if (!this->is_discovery_enabled())
    return "";
  std::string sanitized_name = sanitize_string_whitelist(App.get_name(), HOSTNAME_CHARACTER_WHITELIST);
  return discovery_info.prefix + "/" + this->component_type() + "/" + sanitized_name + "/" +
      this->get_default_object_id() + "/config";
}

std::string MQTTComponent::get_default_topic_for(const std::string &suffix) const {
  return global_mqtt_client->get_topic_prefix() + "/" + this->component_type() + "/" + this->get_default_object_id()
      + "/" + suffix;
}

const std::string MQTTComponent::get_state_topic() const {
  return this->get_topic_for("state");
}

const std::string MQTTComponent::get_command_topic() const {
  return this->get_topic_for("command");
}

void MQTTComponent::send_message(const std::string &topic, const std::string &payload,
                                 const optional<uint8_t> &qos, const optional<bool> &retain) {
  bool actual_retain = retain.value_or(this->retain_);
  uint8_t actual_qos = qos.value_or(0);
  global_mqtt_client->publish(topic, payload, actual_qos, actual_retain);
}

void MQTTComponent::send_json_message(const std::string &topic, const json_build_t &f,
                                      const optional<uint8_t> &qos, const optional<bool> &retain) {
  bool actual_retain = retain.value_or(this->retain_);
  uint8_t actual_qos = qos.value_or(0);
  global_mqtt_client->publish_json(topic, f, actual_qos, actual_retain);
}

void MQTTComponent::send_discovery_() {
  const MQTTDiscoveryInfo &discovery_info = global_mqtt_client->get_discovery_info();

  ESP_LOGV(TAG, "'%s': Sending discovery...", this->friendly_name().c_str());

  this->send_json_message(this->get_discovery_topic(discovery_info), [&](JsonBuffer &buffer, JsonObject &root) {
    SendDiscoveryConfig config;
    config.state_topic = true;
    config.command_topic = true;
    config.platform = "mqtt";

    this->send_discovery(buffer, root, config);

    root["name"] = this->friendly_name();
    if (strcmp(config.platform, "mqtt") != 0)
      root["platform"] = config.platform;
    if (config.state_topic)
      root["state_topic"] = this->get_state_topic();
    if (config.command_topic)
      root["command_topic"] = this->get_command_topic();

    if (this->availability_ == nullptr) {
      root["availability_topic"] = global_mqtt_client->get_availability().topic;
      if (global_mqtt_client->get_availability().payload_available != "online")
        root["payload_available"] = global_mqtt_client->get_availability().payload_available;
      if (global_mqtt_client->get_availability().payload_not_available != "offline")
        root["payload_not_available"] = global_mqtt_client->get_availability().payload_not_available;
    } else if (!this->availability_->topic.empty()) {
      root["availability_topic"] = this->availability_->topic;
      if (this->availability_->payload_available != "online")
        root["payload_available"] = this->availability_->payload_available;
      if (this->availability_->payload_not_available != "offline")
        root["payload_not_available"] = this->availability_->payload_not_available;
    }
  }, 0, discovery_info.retain);
}

bool MQTTComponent::get_retain() const {
  return this->retain_;
}

bool MQTTComponent::is_discovery_enabled() const {
  return this->discovery_enabled_ && global_mqtt_client->is_discovery_enabled();
}

std::string MQTTComponent::get_default_object_id() const {
  return sanitize_string_whitelist(to_lowercase_underscore(this->friendly_name()), HOSTNAME_CHARACTER_WHITELIST);
}

void MQTTComponent::subscribe(const std::string &topic, mqtt_callback_t callback, uint8_t qos) {
  global_mqtt_client->subscribe(topic, std::move(callback), qos);
}

void MQTTComponent::subscribe_json(const std::string &topic, json_parse_t callback, uint8_t qos) {
  global_mqtt_client->subscribe_json(topic, std::move(callback), qos);
}

MQTTComponent::MQTTComponent() = default;

float MQTTComponent::get_setup_priority() const {
  return setup_priority::MQTT_COMPONENT;
}
void MQTTComponent::disable_discovery() {
  this->discovery_enabled_ = false;
}
void MQTTComponent::set_custom_state_topic(const std::string &custom_state_topic) {
  this->set_custom_topic("state", custom_state_topic);
}
void MQTTComponent::set_custom_command_topic(const std::string &custom_command_topic) {
  this->set_custom_topic("command", custom_command_topic);
}
void MQTTComponent::set_custom_topic(const std::string &key, const std::string &custom_topic) {
  this->custom_topics_[key] = custom_topic;
}
const std::string MQTTComponent::get_topic_for(const std::string &key) const {
  if (this->custom_topics_.find(key) != this->custom_topics_.end())
    return this->custom_topics_.find(key)->second;
  return this->get_default_topic_for(key);
}

void MQTTComponent::set_availability(std::string topic,
                                     std::string payload_available,
                                     std::string payload_not_available) {
  delete this->availability_;
  this->availability_ = new Availability();
  this->availability_->topic = std::move(topic);
  this->availability_->payload_available = std::move(payload_available);
  this->availability_->payload_not_available = std::move(payload_not_available);
}
void MQTTComponent::disable_availability() {
  this->set_availability("", "", "");
}
void MQTTComponent::setup_() {
  // Call component internal setup.
  this->setup_internal();

  this->setup();

  global_mqtt_client->add_on_connect_callback([this]() {
    this->next_send_discovery_ = true;
  });
}
void MQTTComponent::loop_() {
  this->loop_internal();

  this->loop();

  if (this->next_send_discovery_) {
    if (this->is_discovery_enabled())
      this->send_discovery_();

    this->next_send_discovery_ = false;
  }
}

} // namespace mqtt

ESPHOMELIB_NAMESPACE_END