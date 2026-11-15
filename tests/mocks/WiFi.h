#pragma once
#include "Arduino.h"
#define WL_CONNECTED 3
#define WL_DISCONNECTED 6
#define WIFI_STA 1

struct WifiSimulado { bool disponivel = true; bool conectado = false; int tentativas = 0; };
extern WifiSimulado wifiSim;

class WiFiClient {};
class WiFiClass {
 public:
  void mode(int) {}
  int begin(const char*, const char*, int = 0) { wifiSim.tentativas++; wifiSim.conectado = wifiSim.disponivel; return 0; }
  int status() { return (wifiSim.disponivel && wifiSim.conectado) ? WL_CONNECTED : WL_DISCONNECTED; }
};
extern WiFiClass WiFi;
