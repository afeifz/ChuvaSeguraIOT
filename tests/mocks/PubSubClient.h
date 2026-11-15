#pragma once
#include "WiFi.h"
struct MqttSimulado {
  bool brokerOk = true;
  bool conectado = false;
  std::vector<std::pair<std::string, std::string>> publicados;
  int tentativas = 0;
};
extern MqttSimulado mqttSim;
class PubSubClient {
 public:
  PubSubClient(WiFiClient&) {}
  PubSubClient& setServer(const char*, uint16_t) { return *this; }
  bool setBufferSize(uint16_t) { return true; }
  bool connect(const char*) { mqttSim.tentativas++; mqttSim.conectado = mqttSim.brokerOk; return mqttSim.conectado; }
  bool connected() { return mqttSim.conectado && mqttSim.brokerOk; }
  bool publish(const char* topico, const char* payload) {
    if (!connected()) return false;
    mqttSim.publicados.push_back({topico, payload});
    return true;
  }
  bool loop() { return connected(); }
  int state() { return connected() ? 0 : -2; }
};
