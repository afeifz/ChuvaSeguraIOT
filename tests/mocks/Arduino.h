// Mock mínimo da API do Arduino/ESP32, SÓ para testar o sketch no computador.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 3
#endif

#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define LOW 0
#define HIGH 1

class String {
 public:
  String(const char* c) : s(c) {}
  std::string s;
};

// ----- "hardware" simulado, controlado pelos testes -----
struct HardwareSimulado {
  unsigned long agoraMs = 0;
  double distanciaCm = 90;
  bool semEco = false;
  bool botaoPressionado = false;
  bool pino[64] = {false};
  int tomAtual = 0;               // 0 = buzzer mudo
  std::vector<int> tons;          // histórico de tons ligados
  std::string saidaSerial;
};
extern HardwareSimulado hw;

inline void pinMode(int, int) {}
inline void digitalWrite(int pino, int valor) { hw.pino[pino] = (valor == HIGH); }
inline int digitalRead(int pino) { return pino == 19 ? (hw.botaoPressionado ? LOW : HIGH) : (hw.pino[pino] ? HIGH : LOW); }
inline unsigned long millis() { return hw.agoraMs; }
inline void delay(unsigned long ms) { hw.agoraMs += ms; }
inline void delayMicroseconds(unsigned int) {}
inline unsigned long pulseIn(int, int, unsigned long = 1000000UL) {
  if (hw.semEco) return 0;
  return (unsigned long)(hw.distanciaCm * 58.0);
}
inline void tone(int, unsigned int f) { hw.tomAtual = (int)f; hw.tons.push_back((int)f); }
inline void noTone(int) { hw.tomAtual = 0; }
inline double ledcWriteTone(int, double f) { hw.tomAtual = (int)f; return f; }
inline void ledcSetup(int, int, int) {}
inline void ledcAttachPin(int, int) {}

class SerialSimulado {
 public:
  void begin(long) {}
  void print(const char* t) { hw.saidaSerial += t; }
  void print(int v) { hw.saidaSerial += std::to_string(v); }
  void println(const char* t) { hw.saidaSerial += t; hw.saidaSerial += "\n"; }
  void println(int v) { hw.saidaSerial += std::to_string(v) + "\n"; }
  void println() { hw.saidaSerial += "\n"; }
};
extern SerialSimulado Serial;
