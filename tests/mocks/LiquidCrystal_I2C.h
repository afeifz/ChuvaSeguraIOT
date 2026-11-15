#pragma once
#include "Arduino.h"
struct LcdSimulado { std::string linha[2] = {"", ""}; int cursorLinha = 0; };
extern LcdSimulado lcdSim;
class LiquidCrystal_I2C {
 public:
  LiquidCrystal_I2C(uint8_t, uint8_t, uint8_t) {}
  void init() {}
  void backlight() {}
  void setCursor(int, int linha) { lcdSim.cursorLinha = linha; }
  void print(const char* t) { lcdSim.linha[lcdSim.cursorLinha] = t; }
};
