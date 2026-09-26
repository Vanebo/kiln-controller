#include "network.h"
#include "app_state.h"
#include "app_config.h"
#include <WiFi.h>
#include <ESPmDNS.h>

static void beginStationConnect() {
  if (wifiSsid.isEmpty()) return;
  if (setupApActive) WiFi.mode(WIFI_AP_STA);
  else WiFi.mode(WIFI_STA);
  WiFi.setHostname(settings.hostname.c_str());
  WiFi.setAutoReconnect(true);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  lastWifiAttemptMs = millis();
  Serial.println(String("Wi-Fi: trying ") + wifiSsid);
}

void initNetwork() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(settings.hostname.c_str());
  lastWifiAttemptMs = millis() - WIFI_RETRY_INTERVAL_MS;
  if (!wifiSsid.isEmpty()) beginStationConnect();
  else Serial.println("Wi-Fi not configured. Setup hotspot is OFF.");
}

void updateNetwork() {
  if (setupApActive && millis() - setupApStartedMs >= AP_TIMEOUT_MS) {
    stopSetupAP();
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!mdnsStarted) {
      if (MDNS.begin(settings.hostname.c_str())) {
        MDNS.addService("http", "tcp", 80);
        mdnsStarted = true;
        Serial.println(String("mDNS: http://") + settings.hostname + ".local");
      }
      Serial.println(String("Wi-Fi connected: ") + WiFi.localIP().toString());
    }
    return;
  }

  mdnsStarted = false;
  if (wifiSsid.isEmpty()) return;
  if (millis() - lastWifiAttemptMs >= WIFI_RETRY_INTERVAL_MS) {
    WiFi.disconnect(false, false);
    beginStationConnect();
  }
}

bool startSetupAP() {
  if (setupApActive) return true;
  WiFi.mode(WIFI_AP_STA);
  bool ok = WiFi.softAP("Kiln-Setup");
  setupApActive = ok;
  setupApStartedMs = millis();
  if (ok) Serial.println(String("Setup hotspot: http://") + WiFi.softAPIP().toString());
  else Serial.println("Failed to start setup hotspot");
  return ok;
}

void stopSetupAP() {
  if (!setupApActive) return;
  WiFi.softAPdisconnect(false);
  setupApActive = false;
  if (WiFi.status() == WL_CONNECTED || !wifiSsid.isEmpty()) WiFi.mode(WIFI_STA);
  Serial.println("Setup hotspot stopped");
}

String stationIp() {
  return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("--");
}

String apIp() {
  return setupApActive ? WiFi.softAPIP().toString() : String("--");
}

String wifiStatusText() {
  if (WiFi.status() == WL_CONNECTED) return "Connected";
  if (wifiSsid.isEmpty()) return "Not configured";
  return "Disconnected / retrying";
}

int wifiRssi() {
  return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
}

void requestWifiRetry() {
  if (wifiSsid.isEmpty()) return;
  lastWifiAttemptMs = millis() - WIFI_RETRY_INTERVAL_MS;
  WiFi.disconnect(false, false);
}
