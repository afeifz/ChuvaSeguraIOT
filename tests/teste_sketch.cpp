// Executa o sketch.ino REAL no computador com um "hardware" simulado e confere o comportamento:
//   g++ -std=c++17 -Imocks -I../projeto-wokwi -x c++ teste_sketch.cpp -o teste_sketch && ./teste_sketch
#include "Arduino.h"
HardwareSimulado hw;
SerialSimulado Serial;

#include "WiFi.h"
WifiSimulado wifiSim;
WiFiClass WiFi;
#include "HTTPClient.h"
HttpSimulado httpSim;
#include "PubSubClient.h"
MqttSimulado mqttSim;
#include "Wire.h"
TwoWire Wire;
#include "LiquidCrystal_I2C.h"
LcdSimulado lcdSim;

#include "../projeto-wokwi/sketch.ino"   // o firmware de verdade

static int falhas = 0;
#define VERIFICAR(cond, nome)                                   \
  do {                                                          \
    bool ok_ = (cond);                                          \
    std::printf("%s %s\n", ok_ ? "OK   " : "FALHA", nome);      \
    if (!ok_) { falhas++; }                                     \
  } while (0)

// avança o tempo simulado executando o loop() do firmware (a cada 10 ms)
static void rodar(unsigned long ms) {
  unsigned long fim = hw.agoraMs + ms;
  while (hw.agoraMs < fim) {
    loop();
    hw.agoraMs += 10;
  }
}

static bool contem(const std::string& texto, const char* trecho) { return texto.find(trecho) != std::string::npos; }
static std::string ultimoPublicado() { return mqttSim.publicados.empty() ? "" : mqttSim.publicados.back().second; }

int main() {
  hw.distanciaCm = 90;  // nível 10 cm -> NORMAL
  setup();
  rodar(4000);

  VERIFICAR(hw.pino[25] && !hw.pino[33] && !hw.pino[32], "NORMAL: so o LED verde acende");
  VERIFICAR(lcdSim.linha[1].rfind("SEM RISCO", 0) == 0, "NORMAL: display mostra SEM RISCO");
  VERIFICAR(lcdSim.linha[0].rfind("Agua: 10cm", 0) == 0, "NORMAL: display mostra o nivel 10 cm");
  VERIFICAR(!mqttSim.publicados.empty(), "NORMAL: publicou a leitura no MQTT");
  VERIFICAR(!mqttSim.publicados.empty() && mqttSim.publicados[0].first == "chuvasegura/gs2026-troque-este-id/ponto1/leitura", "topico MQTT no formato combinado com a ponte");
  VERIFICAR(contem(ultimoPublicado(), "\"pontoId\":1") && contem(ultimoPublicado(), "\"distanciaCm\":90.0"), "JSON tem pontoId e distanciaCm");
  VERIFICAR(hw.tomAtual == 0, "NORMAL: buzzer mudo");

  // ---- ATENÇÃO ----
  size_t antes = mqttSim.publicados.size();
  hw.distanciaCm = 60;  // nível 40
  rodar(1500);
  VERIFICAR(!hw.pino[25] && hw.pino[33] && !hw.pino[32], "ATENCAO: so o LED amarelo acende");
  VERIFICAR(mqttSim.publicados.size() > antes && contem(ultimoPublicado(), "\"risco\":\"ATENCAO\""), "ATENCAO: envio IMEDIATO ao mudar o risco");
  VERIFICAR(hw.tomAtual == 0, "ATENCAO: buzzer mudo");

  // ---- ALERTA ----
  hw.tons.clear();
  hw.distanciaCm = 35;  // nível 65
  rodar(1500);
  VERIFICAR(!hw.pino[25] && !hw.pino[33] && hw.pino[32], "ALERTA: so o LED vermelho acende");
  VERIFICAR(contem(ultimoPublicado(), "\"risco\":\"ALERTA\"") && contem(ultimoPublicado(), "\"distanciaCm\":35.0"), "ALERTA: JSON com risco ALERTA e distancia 35");
  VERIFICAR(!hw.tons.empty(), "ALERTA: o buzzer toca");
  rodar(2000);
  bool houveSilencioNoPadrao = false;
  for (int i = 0; i < 100; i++) { rodar(50); if (hw.tomAtual == 0) houveSilencioNoPadrao = true; }
  VERIFICAR(houveSilencioNoPadrao, "ALERTA: buzzer intermitente (nao fica ligado direto)");

  // ---- botão silencia ----
  hw.botaoPressionado = true;
  rodar(300);
  hw.botaoPressionado = false;
  hw.tons.clear();
  rodar(3000);
  VERIFICAR(hw.tons.empty() && hw.tomAtual == 0, "BOTAO: buzzer silenciado");
  VERIFICAR(lcdSim.linha[0].find("MUDO") != std::string::npos, "BOTAO: display mostra MUDO");
  VERIFICAR(hw.pino[32], "BOTAO: o LED vermelho continua aceso");
  rodar(62000);
  hw.tons.clear();
  rodar(2000);
  VERIFICAR(!hw.tons.empty(), "BOTAO: depois de 60 s o buzzer volta a tocar");

  // ---- resiliência: sem internet o alarme local continua ----
  size_t publicadosAntes = mqttSim.publicados.size();
  mqttSim.brokerOk = false;
  hw.botaoPressionado = true; rodar(300); hw.botaoPressionado = false;  // silencia para o teste ficar simples
  rodar(8000);
  VERIFICAR(hw.pino[32], "OFFLINE: o LED vermelho continua aceso sem internet");
  VERIFICAR(mqttSim.publicados.size() == publicadosAntes, "OFFLINE: nada e publicado (mas o firmware nao trava)");
  VERIFICAR(lcdSim.linha[1].find("OFFLINE") != std::string::npos, "OFFLINE: display mostra OFFLINE");
  mqttSim.brokerOk = true;
  rodar(12000);
  VERIFICAR(mqttSim.publicados.size() > publicadosAntes, "OFFLINE: ao voltar a internet, volta a publicar sozinho");
  VERIFICAR(lcdSim.linha[1].find("ONLINE") != std::string::npos, "ONLINE: display mostra ONLINE de novo");

  // ---- sensor sem eco ----
  hw.semEco = true;
  rodar(2000);
  VERIFICAR(lcdSim.linha[0].find("Sensor sem leitu") == 0, "SEM ECO: display avisa que o sensor esta sem leitura");
  VERIFICAR(hw.tomAtual == 0, "SEM ECO: buzzer nao toca com dado invalido");
  size_t n = mqttSim.publicados.size();
  rodar(7000);
  VERIFICAR(mqttSim.publicados.size() == n, "SEM ECO: nao envia leitura invalida para a API");
  bool amareloPiscou = false, amareloApagou = false;
  for (int i = 0; i < 120; i++) { rodar(10); (hw.pino[33] ? amareloPiscou : amareloApagou) = true; }
  VERIFICAR(amareloPiscou && amareloApagou, "SEM ECO: LED amarelo pisca");

  // ---- volta ao normal ----
  hw.semEco = false;
  hw.distanciaCm = 95;
  rodar(3000);
  VERIFICAR(hw.pino[25] && !hw.pino[32], "VOLTA AO NORMAL: LED verde de novo");
  VERIFICAR(contem(ultimoPublicado(), "\"risco\":\"NORMAL\""), "VOLTA AO NORMAL: publicou NORMAL");

  std::printf(falhas == 0 ? "\nTUDO OK\n" : "\n%d FALHA(S)\n", falhas);
  return falhas == 0 ? 0 : 1;
}
