#pragma once
#include "WiFi.h"
struct HttpSimulado { std::vector<std::string> posts; int codigo = 201; };
extern HttpSimulado httpSim;
class HTTPClient {
 public:
  bool begin(WiFiClient&, const String&) { return true; }
  void addHeader(const char*, const char*) {}
  int POST(const String& corpo) { httpSim.posts.push_back(corpo.s); return httpSim.codigo; }
  void end() {}
};
