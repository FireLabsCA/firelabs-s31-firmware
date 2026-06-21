#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include "Config.h"
#include "Led.h"
#include "Button.h"
#include "Relay.h"
#include "Meter.h"
#include "NetworkManager.h"
#include "MqttManager.h"
#include "WebUi.h"
#include "Log.h"

static Config cfg;
static Led led;
static Button button;
static Relay relay;
static Meter meter;
static NetworkManager net;
static MqttManager mqtt;
static WebUi web;

static const float    kNoLoadWatts = 0.1f;
static const uint32_t kNoLoadHold  = 12000;
static const uint32_t kPublishMs   = 8000;
static const uint32_t kDiagMs      = 60000;

static uint32_t noLoadSince = 0;
static bool     noLoadActive = false;
static uint32_t lastPublish = 0;
static uint32_t lastDiag = 0;
static double   energyTodayKwh = 0;  // TODO: reset at local midnight via NTP time

static bool initialRelayState() {
  switch (cfg.restoreMode) {
    case Config::RestoreMode::On:   return true;
    case Config::RestoreMode::Off:  return false;
    case Config::RestoreMode::Last: return cfg.relayLastState;
  }
  return false;
}

// Re-apply settings after a web save (brightness, calibration, broker).
static void applySettings() {
  led.setIndicatorPercent(cfg.indicatorPercent);
  meter.setCalibration(cfg.calVoltage, cfg.calCurrent, cfg.calPower);
  mqtt.begin(&cfg);
  mqtt.publishNoLoadEnabled(cfg.noLoadIndicator);
  mqtt.publishRestoreMode((uint8_t)cfg.restoreMode);
}

void setup() {
#ifdef FL_DIAG
  Serial.begin(115200);
  delay(80);
  Serial.println("\r\n");
  DBG("=== FireLabs S31 DIAG boot ===");
  DBG("reset: %s", ESP.getResetReason().c_str());
  DBG("heap=%u flash=%u", ESP.getFreeHeap(), ESP.getFlashChipRealSize());
#endif

  cfg.load();
  DBG("cfg loaded: wifi=%d name='%s'", cfg.hasWifi(), cfg.deviceName.c_str());

  relay.begin(initialRelayState());
  DBG("relay begin");
  relay.onChange([](bool on) {
    mqtt.publishRelay(on);
    if (cfg.restoreMode == Config::RestoreMode::Last) {
      cfg.relayLastState = on;
      cfg.save();
    }
  });

  led.begin();
  led.setIndicatorPercent(cfg.indicatorPercent);

  button.begin(&led);
  button.onTap([]() {
    if (led.identifying()) led.setIdentify(false);  // a press cancels identify
    else relay.toggle();
  });
  button.onFactoryReset([]() { cfg.clear(); ESP.restart(); });

#ifndef FL_DIAG
  meter.begin();   // claims UART0; skipped in DIAG so serial is the debug console
#endif
  meter.setCalibration(cfg.calVoltage, cfg.calCurrent, cfg.calPower);
  DBG("meter begin");

  mqtt.onRelayCommand([](bool on) { relay.set(on); });
  mqtt.onNoLoadCommand([](bool on) {
    cfg.noLoadIndicator = on; cfg.save();
    mqtt.publishNoLoadEnabled(on);
  });
  mqtt.onRestoreCommand([](uint8_t m) {
    cfg.restoreMode = (Config::RestoreMode)m; cfg.save();
    mqtt.publishRestoreMode(m);
  });
  mqtt.setMeterPresent([]() { return meter.present(); });
  mqtt.setRelayStateProvider([]() { return relay.isOn(); });

  web.setEnergyProvider([]() { return (float)energyTodayKwh; });
  web.setMqttStatus([]() { return mqtt.connected(); });
  web.setMqttInfo([]() { return String("r") + mqtt.lastReason() + " t" + mqtt.attempts(); });
  web.setFaultProvider([]() { return noLoadActive; });
  web.setScanProvider([]() { return net.scanJson(); });
  web.setScanTrigger([]() { net.startScan(); });
  web.onApply(applySettings);
  web.onIdentify([](bool on) { led.setIdentify(on); });
  web.setIdentifyState([]() { return led.identifying(); });

  net.onSetup([]() { DBG("web.begin (setup)"); web.begin(true, &cfg, &relay, &meter); DBG("web up (setup)"); });
  net.onOnline([]() {
    DBG("online: mdns + web + mqtt");
    MDNS.begin(cfg.hostname());
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("firelabs", "tcp", 80);  // for HA zeroconf discovery
    MDNS.addServiceTxt("firelabs", "tcp", "model", "S31");
    MDNS.addServiceTxt("firelabs", "tcp", "mac", WiFi.macAddress());
    web.begin(false, &cfg, &relay, &meter);
    mqtt.begin(&cfg);
  });
  DBG("net.begin ...");
  net.begin(&cfg);
  DBG("setup done, mode=%d heap=%u", (int)net.mode(), ESP.getFreeHeap());
}

static void updateLedStatus() {
  switch (net.mode()) {
    case NetworkManager::Mode::SetupAp:    led.setStatus(Led::Status::SetupAp); break;
    case NetworkManager::Mode::Connecting: led.setStatus(Led::Status::Connecting); break;
    case NetworkManager::Mode::Online:
      led.setStatus(noLoadActive && cfg.noLoadIndicator ? Led::Status::NoLoad : Led::Status::Off);
      break;
    default: led.setStatus(Led::Status::Off); break;
  }
}

static void updateNoLoad(uint32_t now) {
  if (!meter.present()) {            // S31 Lite: no metering, no fault
    if (noLoadActive) { noLoadActive = false; mqtt.publishFault(false); }
    noLoadSince = 0;
    return;
  }
  bool cond = relay.isOn() && meter.power() <= kNoLoadWatts;
  if (cond) {
    if (noLoadSince == 0) noLoadSince = now;
    bool active = (now - noLoadSince) >= kNoLoadHold;
    if (active != noLoadActive) { noLoadActive = active; mqtt.publishFault(active); }
  } else {
    noLoadSince = 0;
    if (noLoadActive) { noLoadActive = false; mqtt.publishFault(false); }
  }
}

void loop() {
  const uint32_t now = millis();

  net.loop();
  button.update(now);
#ifndef FL_DIAG
  meter.loop();
#endif
  mqtt.loop(now);

#ifdef FL_DIAG
  static uint32_t lastTick = 0;
  if (now - lastTick >= 2000) { lastTick = now; DBG("tick mode=%d heap=%u", (int)net.mode(), ESP.getFreeHeap()); }
#endif

  // Meter may come online after boot (mains applied later). Re-announce so HA
  // and the web UI gain the power entities.
  static bool meterWasPresent = false;
  if (meter.present() != meterWasPresent) {
    meterWasPresent = meter.present();
    mqtt.reannounce();
  }

  updateNoLoad(now);
  updateLedStatus();
  led.update(now);

  if (meter.present() && now - lastPublish >= kPublishMs) {
    lastPublish = now;
    float v, i, p;
    if (meter.sampleAverage(v, i, p)) {
      energyTodayKwh += (double)p * (kPublishMs / 3600000.0) / 1000.0;  // W*h -> kWh
      mqtt.publishPower(p, v, i, (float)energyTodayKwh);
    }
  }

  if (now - lastDiag >= kDiagMs) {
    lastDiag = now;
    if (net.isOnline()) mqtt.publishDiagnostics(WiFi.RSSI(), now / 1000);
  }

  MDNS.update();
}
