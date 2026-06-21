#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Ticker.h>
#include <functional>

class Config;
class Relay;
class Meter;

// On-device web UI (SPEC.md "Web UI"). One server, two faces:
//   setup mode  -> captive-portal wizard (scan + provision)
//   online mode -> device UI tabs + REST API
// All assets are embedded in PROGMEM, so the whole UI ships in firmware.bin
// with no separate filesystem image to flash.
class WebUi {
public:
  using FloatProvider  = std::function<float()>;
  using BoolProvider   = std::function<bool()>;
  using StringProvider = std::function<String()>;
  using VoidCb         = std::function<void()>;

  void begin(bool setupMode, Config* cfg, Relay* relay, Meter* meter);

  void setEnergyProvider(FloatProvider f) { energy_ = f; }
  void setMqttStatus(BoolProvider f) { mqttUp_ = f; }
  void setMqttInfo(StringProvider f) { mqttInfo_ = f; }  // diagnostic string
  void setFaultProvider(BoolProvider f) { faultProvider_ = f; }
  void setScanProvider(StringProvider f) { scanProvider_ = f; }  // cached scan JSON
  void setScanTrigger(VoidCb f) { scanTrigger_ = f; }            // request a rescan
  void onApply(VoidCb cb) { onApply_ = cb; }  // re-apply cfg after a save
  void onIdentify(std::function<void(bool)> cb) { onIdentify_ = cb; }
  void setIdentifyState(BoolProvider f) { identifyState_ = f; }

private:
  void routesCommon();
  void routesSetup();
  void routesOnline();
  void handleJson(const char* path, std::function<void(JsonDocument&, AsyncWebServerRequest*)> fn);
  void handleOta();

  AsyncWebServer server_{80};
  bool begun_ = false;   // server_.begin() is one-shot; re-begin swaps routes only
  Config* cfg_ = nullptr;
  Relay* relay_ = nullptr;
  Meter* meter_ = nullptr;
  FloatProvider energy_;
  BoolProvider mqttUp_;
  BoolProvider faultProvider_;
  StringProvider mqttInfo_;
  StringProvider scanProvider_;
  VoidCb scanTrigger_;
  VoidCb onApply_;
  std::function<void(bool)> onIdentify_;
  BoolProvider identifyState_;
  Ticker restart_;
};
