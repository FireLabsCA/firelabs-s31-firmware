#pragma once
#include <Arduino.h>
#include <DNSServer.h>
#include <functional>

class Config;

// Wifi lifecycle and captive-portal AP (SPEC.md "Provisioning flow").
//   no stored wifi          -> SetupAp (open AP + DNS captive portal)
//   stored wifi             -> Connecting, then Online
//   connect fails > 30s     -> fall back to SetupAp
//   online link drops       -> back to Connecting
class NetworkManager {
public:
  enum class Mode : uint8_t { Boot, SetupAp, Connecting, Online };
  using Callback = std::function<void()>;

  void begin(Config* cfg);
  void loop();

  Mode mode() const { return mode_; }
  bool isOnline() const { return mode_ == Mode::Online; }

  // Cached, non-blocking wifi scan for the setup wizard. A live scan from the
  // web handler would drop SoftAP clients and can crash the async stack, so we
  // scan async (at AP start, before a phone connects) and serve the cache.
  void startScan();
  String scanJson() const { return scanCache_.length() ? scanCache_ : String("[]"); }

  void onOnline(Callback cb) { onOnline_ = cb; }   // joined the network
  void onSetup(Callback cb)  { onSetup_ = cb; }    // entered AP setup mode

  static const uint32_t kConnectTimeout = 30000;

private:
  void startConnecting();
  void startAp();

  Config* cfg_ = nullptr;
  Mode mode_ = Mode::Boot;
  uint32_t connectStart_ = 0;
  DNSServer dns_;
  bool dnsActive_ = false;
  bool scanning_ = false;
  String scanCache_;
  Callback onOnline_;
  Callback onSetup_;
};
