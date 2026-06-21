#include "MqttManager.h"
#include "Config.h"
#include "Branding.h"
#include <ArduinoJson.h>

void MqttManager::begin(Config* cfg) {
  cfg_ = cfg;
  if (cfg_->mqttHost.length() == 0) return;  // nothing configured yet

  client_.setServer(cfg_->mqttHost.c_str(), cfg_->mqttPort);
  if (cfg_->mqttUser.length())
    client_.setCredentials(cfg_->mqttUser.c_str(), cfg_->mqttPass.c_str());
  // AsyncMqttClient keeps POINTERS to these and builds the CONNECT packet async
  // (after begin() returns), so the backing strings must be long-lived members,
  // not locals/temporaries. This was the availability bug.
  clientId_ = cfg_->hostname();
  client_.setClientId(clientId_.c_str());
  client_.setKeepAlive(15);  // broker marks the plug dead ~22s after a real loss

  willTopic_ = cfg_->topicBase() + "/availability";
  client_.setWill(willTopic_.c_str(), 1, true, "offline");

  client_.onConnect([this](bool s) { onConnect(s); });
  client_.onDisconnect([this](AsyncMqttClientDisconnectReason r) { onDisconnect(r); });
  client_.onMessage([this](char* t, char* p, AsyncMqttClientMessageProperties pr,
                           size_t l, size_t i, size_t tot) { onMessage(t, p, pr, l, i, tot); });
  connect();
}

void MqttManager::connect() {
  if (cfg_ && cfg_->mqttHost.length()) { attempts_++; client_.connect(); }
}

void MqttManager::pub(const String& topic, const String& payload, bool retain) {
  if (!connected()) return;
  client_.publish(topic.c_str(), 1, retain, payload.c_str(), payload.length());
}

void MqttManager::onConnect(bool) {
  String base = cfg_->topicBase();
  client_.subscribe((base + "/relay/set").c_str(), 1);
  client_.subscribe((base + "/noload/set").c_str(), 1);
  client_.subscribe((base + "/restore/set").c_str(), 1);

  publishAvailability(true);
  if (!discoverySent_) { publishDiscovery(); discoverySent_ = true; }
  publishRelay(relayProvider_ ? relayProvider_() : relayState_);  // real state, not stale cache
  publishFault(faultState_);  // seed a value so HA isn't "Unknown"
  publishNoLoadEnabled(cfg_->noLoadIndicator);
  publishRestoreMode((uint8_t)cfg_->restoreMode);
}

void MqttManager::onDisconnect(AsyncMqttClientDisconnectReason reason) {
  discoverySent_ = false;  // loop() drives reconnection
  lastReason_ = (int)reason;
}

void MqttManager::onMessage(char* topic, char* payload, AsyncMqttClientMessageProperties,
                            size_t len, size_t, size_t) {
  String t(topic);
  String body;
  body.reserve(len);
  for (size_t i = 0; i < len; i++) body += payload[i];
  bool on = body.equalsIgnoreCase("ON") || body == "1" || body.equalsIgnoreCase("true");

  if (t.endsWith("/relay/set") && relayCmd_) relayCmd_(on);
  else if (t.endsWith("/noload/set") && noLoadCmd_) noLoadCmd_(on);
  else if (t.endsWith("/restore/set") && restoreCmd_) {
    uint8_t m = 2;  // Last
    if (body.equalsIgnoreCase("Off")) m = 0;
    else if (body.equalsIgnoreCase("On")) m = 1;
    restoreCmd_(m);
  }
}

void MqttManager::loop(uint32_t now) {
  if (!cfg_ || cfg_->mqttHost.length() == 0) return;
  if (client_.connected()) return;
  // Full re-init on retry (same path as a UI "Save"), not a bare reconnect. A
  // bare connect() doesn't recover from the failed first attempt at boot.
  if (now - lastTry_ >= 5000) { lastTry_ = now; begin(cfg_); }
}

// ---- state ----

void MqttManager::publishRelay(bool on) {
  relayState_ = on;          // remembered even before MQTT exists
  if (!cfg_) return;         // not begun yet (setup/AP mode): nothing to publish
  pub(cfg_->topicBase() + "/relay/state", on ? "ON" : "OFF");
}

void MqttManager::publishPower(float watts, float volts, float amps, float kwhToday) {
  if (!cfg_) return;
  String base = cfg_->topicBase();
  char v[16];
  snprintf(v, sizeof(v), "%.1f", watts);    pub(base + "/power", v);
  snprintf(v, sizeof(v), "%.1f", volts);    pub(base + "/voltage", v);
  snprintf(v, sizeof(v), "%.2f", amps);     pub(base + "/current", v);
  snprintf(v, sizeof(v), "%.3f", kwhToday); pub(base + "/energy_today", v);
}

void MqttManager::publishFault(bool noLoad) {
  faultState_ = noLoad;
  if (!cfg_) return;
  pub(cfg_->topicBase() + "/fault", noLoad ? "ON" : "OFF");
}

void MqttManager::publishNoLoadEnabled(bool on) {
  if (!cfg_) return;
  pub(cfg_->topicBase() + "/noload/state", on ? "ON" : "OFF");
}

void MqttManager::publishRestoreMode(uint8_t mode) {
  if (!cfg_) return;
  const char* s = mode == 0 ? "Off" : mode == 1 ? "On" : "Last";
  pub(cfg_->topicBase() + "/restore/state", s);
}

void MqttManager::publishDiagnostics(int rssi, uint32_t uptimeSec) {
  if (!cfg_) return;
  String base = cfg_->topicBase();
  pub(base + "/rssi", String(rssi));
  pub(base + "/uptime", String(uptimeSec));
}

void MqttManager::publishAvailability(bool online) {
  if (!cfg_) return;
  pub(cfg_->topicBase() + "/availability", online ? "online" : "offline");
}

void MqttManager::reannounce() {
  if (cfg_ && client_.connected()) publishDiscovery();
}

// ---- discovery ----

// Shared device block so every entity groups under one HA device.
static void addDevice(JsonObject root, Config* cfg) {
  JsonObject dev = root["device"].to<JsonObject>();
  dev["identifiers"][0] = cfg->uniqueId();
  dev["name"] = cfg->deviceName.length() ? cfg->deviceName : cfg->hostname();
  dev["manufacturer"] = "FireLabs";
  dev["model"] = "S31";
  dev["sw_version"] = FL_FW_VERSION;
  dev["configuration_url"] = String("http://") + cfg->hostname() + ".local";
}

void MqttManager::publishDiscovery() {
  const String base = cfg_->topicBase();
  const String uid = cfg_->uniqueId();
  const String prefix = cfg_->discoveryPrefix;
  const String avail = base + "/availability";
  const bool meter = meterPresent_ ? meterPresent_() : true;  // false on S31 Lite

  auto emit = [&](const char* component, const char* object, JsonDocument& doc) {
    doc["availability_topic"] = avail;
    doc["unique_id"] = uid + "_" + object;
    addDevice(doc.as<JsonObject>(), cfg_);
    String out;
    serializeJson(doc, out);
    pub(prefix + "/" + component + "/" + uid + "/" + object + "/config", out);
  };

  {
    JsonDocument d;
    d["name"] = "Relay";
    d["state_topic"] = base + "/relay/state";
    d["command_topic"] = base + "/relay/set";
    d["icon"] = "mdi:power-socket-us";
    emit("switch", "relay", d);
  }
  auto sensor = [&](const char* obj, const char* name, const char* unit,
                    const char* devClass, const char* stateClass) {
    JsonDocument d;
    d["name"] = name;
    d["state_topic"] = base + "/" + obj;
    if (unit) d["unit_of_measurement"] = unit;
    if (devClass) d["device_class"] = devClass;
    if (stateClass) d["state_class"] = stateClass;
    emit("sensor", obj, d);
  };
  if (meter) {  // power features only exist on the full S31
    sensor("power", "Power", "W", "power", "measurement");
    sensor("voltage", "Voltage", "V", "voltage", "measurement");
    sensor("current", "Current", "A", "current", "measurement");
    sensor("energy_today", "Energy Today", "kWh", "energy", "total_increasing");
  }
  sensor("rssi", "WiFi Signal", "dBm", "signal_strength", "measurement");
  sensor("uptime", "Uptime", "s", "duration", "total_increasing");

  if (meter) {
    {
      JsonDocument d;
      d["name"] = "Fault";
      d["state_topic"] = base + "/fault";
      d["payload_on"] = "ON";
      d["payload_off"] = "OFF";
      d["device_class"] = "problem";
      emit("binary_sensor", "fault", d);
    }
    {
      JsonDocument d;
      d["name"] = "No-load Indicator";
      d["state_topic"] = base + "/noload/state";
      d["command_topic"] = base + "/noload/set";
      d["entity_category"] = "config";
      d["icon"] = "mdi:led-on";
      emit("switch", "noload", d);
    }
  }
  {
    JsonDocument d;
    d["name"] = "Restore Mode";
    d["state_topic"] = base + "/restore/state";
    d["command_topic"] = base + "/restore/set";
    d["entity_category"] = "config";
    JsonArray opts = d["options"].to<JsonArray>();
    opts.add("Off"); opts.add("On"); opts.add("Last");
    emit("select", "restore", d);
  }

  // Push current state so HA isn't blank until the next change.
  publishRelay(false);
}
