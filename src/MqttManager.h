#pragma once
#include <Arduino.h>
#include <AsyncMqttClient.h>
#include <Ticker.h>
#include <functional>

class Config;

// MQTT client + Home Assistant discovery (SPEC.md "MQTT and Home Assistant").
// One device, keyed by the MAC-based unique_id so a rename never orphans
// entities. Plaintext on the LAN.
class MqttManager {
public:
  using BoolCb = std::function<void(bool)>;
  using ByteCb = std::function<void(uint8_t)>;
  using Provider = std::function<bool()>;

  void begin(Config* cfg);
  void loop(uint32_t now);

  // S31 Lite has no CSE7766; gate the power entities on this.
  void setMeterPresent(Provider f) { meterPresent_ = f; }
  // Live relay state, so the first publish after (re)connect is the real state.
  void setRelayStateProvider(Provider f) { relayProvider_ = f; }

  bool connected() const { return client_.connected(); }
  int lastReason() const { return lastReason_; }      // last AsyncMqttClient disconnect reason
  uint32_t attempts() const { return attempts_; }     // connect() calls so far

  // Commands coming down from HA.
  void onRelayCommand(BoolCb cb)   { relayCmd_ = cb; }
  void onNoLoadCommand(BoolCb cb)  { noLoadCmd_ = cb; }
  void onRestoreCommand(ByteCb cb) { restoreCmd_ = cb; }

  // State going up to HA.
  void publishRelay(bool on);
  void publishPower(float watts, float volts, float amps, float kwhToday);
  void publishFault(bool noLoad);
  void publishNoLoadEnabled(bool on);
  void publishRestoreMode(uint8_t mode);
  void publishDiagnostics(int rssi, uint32_t uptimeSec);
  void publishAvailability(bool online);

  // Re-send discovery (e.g. the meter came online after boot, so power
  // entities should now appear in HA).
  void reannounce();

private:
  void connect();
  void onConnect(bool sessionPresent);
  void onDisconnect(AsyncMqttClientDisconnectReason reason);
  void onMessage(char* topic, char* payload, AsyncMqttClientMessageProperties props,
                 size_t len, size_t index, size_t total);

  void publishDiscovery();
  void pub(const String& topic, const String& payload, bool retain = true);

  Config* cfg_ = nullptr;
  AsyncMqttClient client_;
  Ticker reconnect_;
  BoolCb relayCmd_;
  BoolCb noLoadCmd_;
  ByteCb restoreCmd_;
  Provider meterPresent_;
  Provider relayProvider_;
  bool discoverySent_ = false;
  bool relayState_ = false;
  bool faultState_ = false;
  uint32_t lastTry_ = 0;
  int lastReason_ = -2;
  uint32_t attempts_ = 0;
  String clientId_;    // long-lived: AsyncMqttClient stores the pointer
  String willTopic_;   // long-lived: ditto
};
