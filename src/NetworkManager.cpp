#include "NetworkManager.h"
#include "Config.h"
#include "Log.h"
#include <ESP8266WiFi.h>

void NetworkManager::begin(Config* cfg) {
  cfg_ = cfg;
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.hostname(cfg_->hostname());
  if (cfg_->hasWifi()) startConnecting();
  else startAp();
}

void NetworkManager::startConnecting() {
  mode_ = Mode::Connecting;
  connectStart_ = millis();
  if (dnsActive_) { dns_.stop(); dnsActive_ = false; }  // tear down portal if retrying
  WiFi.softAPdisconnect(true);                          // drop the AP, station only
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg_->wifiSsid.c_str(), cfg_->wifiPass.c_str());
}

void NetworkManager::startAp() {
  mode_ = Mode::SetupAp;
  apStart_ = millis();

  // Scan FIRST, in plain station mode with no AP up. A scan makes the ESP8266
  // radio hop channels, which drops any SoftAP client, so we must never scan
  // once a phone is connected. Doing it here (boot context) is safe and the
  // blocking call is fine because loop() isn't running yet.
  startScan();

  // Now bring up the captive-portal AP with the network list already cached.
  DBG("ap: softAP '%s'", cfg_->apSsid().c_str());
  WiFi.mode(WIFI_AP);
  WiFi.softAP(cfg_->apSsid().c_str());
  dns_.setErrorReplyCode(DNSReplyCode::NoError);
  dns_.start(53, "*", WiFi.softAPIP());
  dnsActive_ = true;
  DBG("ap: up, ip=%s", WiFi.softAPIP().toString().c_str());
  if (onSetup_) onSetup_();
}

// Blocking scan. Call only from a non-async, non-AP-client context (boot).
void NetworkManager::startScan() {
  DBG("scan: sta mode");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false);
  DBG("scan: start");
  int n = WiFi.scanNetworks();
  DBG("scan: done n=%d", n);
  String s = "[";
  int count = 0;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    ssid.replace("\\", "\\\\");
    ssid.replace("\"", "\\\"");
    if (count++) s += ",";
    s += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    if (count >= 20) break;
  }
  s += "]";
  scanCache_ = s;
  WiFi.scanDelete();
}

void NetworkManager::loop() {
  switch (mode_) {
    case Mode::SetupAp:
      if (dnsActive_) dns_.processNextRequest();
      // Fallback recovery: a provisioned plug only lands here because wifi was
      // unreachable. Don't camp blind forever; retry the join once the window
      // passes (the router/outage may have come back). We re-enter Connecting
      // rather than reboot so the relay GPIO never drops and the load doesn't
      // flicker. A fresh, never-provisioned plug has no creds, so it stays put
      // for a human to set up.
      if (cfg_->hasWifi() && millis() - apStart_ >= kSetupFallbackTimeout) {
        DBG("ap: provisioned + stuck %lus, retrying wifi",
            (unsigned long)(kSetupFallbackTimeout / 1000));
        startConnecting();
      }
      break;

    case Mode::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        mode_ = Mode::Online;
        if (onOnline_) onOnline_();
      } else if (millis() - connectStart_ >= kConnectTimeout) {
        startAp();  // give up, reopen setup so the plug never goes dark
      }
      break;

    case Mode::Online:
      if (WiFi.status() != WL_CONNECTED) startConnecting();
      break;

    case Mode::Boot:
    default:
      break;
  }
}
