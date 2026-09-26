#pragma once
#include <Arduino.h>

void initNetwork();
void updateNetwork();
bool startSetupAP();
void stopSetupAP();
String stationIp();
String apIp();
String wifiStatusText();
int wifiRssi();
void requestWifiRetry();
