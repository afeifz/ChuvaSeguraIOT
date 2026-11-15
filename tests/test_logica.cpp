// Testes das regras do firmware no computador (sem Arduino):
//   g++ -std=c++17 -I../projeto-wokwi test_logica.cpp -o test_logica && ./test_logica
#include <cassert>
#include <cstdio>
#include <cstring>

#include "logica.h"

static int falhas = 0;
#define VERIFICAR(cond, nome)                      \
  do {                                             \
    bool ok_ = (cond);                             \
    std::printf("%s %s\n", ok_ ? "OK   " : "FALHA", nome); \
    if (!ok_) falhas++;                            \
  } while (0)

int main() {
  // ---- faixa do sensor ----
  VERIFICAR(distanciaValida(2.0f) && distanciaValida(400.0f), "distancia nos limites da faixa e valida");
  VERIFICAR(!distanciaValida(1.99f) && !distanciaValida(400.1f) && !distanciaValida(0.0f), "distancia fora da faixa e invalida");
  VERIFICAR(!distanciaValida(NAN), "NaN (sem eco) e invalido");

  // ---- nivel e risco (mesmos numeros dos Test Cases / API C#) ----
  VERIFICAR(nivelAguaCm(35.0f, 100.0f) == 65.0f, "distancia 35 cm -> nivel 65 cm");
  VERIFICAR(nivelAguaCm(150.0f, 100.0f) == 0.0f, "distancia maior que a altura -> nivel 0");
  VERIFICAR(classificarRisco(10, 30, 60) == RISCO_NORMAL, "nivel 10 -> NORMAL");
  VERIFICAR(classificarRisco(29.9f, 30, 60) == RISCO_NORMAL, "nivel 29,9 -> NORMAL");
  VERIFICAR(classificarRisco(30, 30, 60) == RISCO_ATENCAO, "nivel 30 -> ATENCAO");
  VERIFICAR(classificarRisco(59.9f, 30, 60) == RISCO_ATENCAO, "nivel 59,9 -> ATENCAO");
  VERIFICAR(classificarRisco(60, 30, 60) == RISCO_ALERTA, "nivel 60 -> ALERTA");
  VERIFICAR(classificarRisco(65, 30, 60) == RISCO_ALERTA, "nivel 65 -> ALERTA");
  VERIFICAR(std::strcmp(nomeRisco(RISCO_ATENCAO), "ATENCAO") == 0, "nome do risco sem acento (igual ao Java)");

  // ---- mediana ----
  float impar[5] = {90, 91, 5, 90, 89};       // 5 cm e uma leitura "maluca"
  VERIFICAR(mediana(impar, 5) == 90.0f, "mediana ignora leitura maluca isolada");
  float par[4] = {10, 20, 30, 40};
  VERIFICAR(mediana(par, 4) == 25.0f, "mediana de quantidade par");
  VERIFICAR(std::isnan(mediana(par, 0)), "mediana sem amostras e NaN");

  // ---- confirmacao do risco (anti pisca-pisca) ----
  ConfirmadorRisco c;
  VERIFICAR(!c.atualizar(RISCO_ALERTA, 2) && c.atual == RISCO_NORMAL, "1a leitura de alerta ainda nao muda");
  VERIFICAR(c.atualizar(RISCO_ALERTA, 2) && c.atual == RISCO_ALERTA, "2a leitura seguida confirma ALERTA");
  VERIFICAR(!c.atualizar(RISCO_ALERTA, 2), "mesmo risco nao gera nova mudanca");
  c.atualizar(RISCO_NORMAL, 2);
  c.atualizar(RISCO_ALERTA, 2);  // volta a alerta: zera a contagem
  VERIFICAR(c.atual == RISCO_ALERTA, "leitura isolada diferente nao derruba o alerta");
  VERIFICAR(!c.atualizar(RISCO_ATENCAO, 2) && !c.atualizar(RISCO_NORMAL, 2) && c.atual == RISCO_ALERTA,
            "leituras alternadas nao confirmam nada");

  // ---- tempo ----
  VERIFICAR(passouIntervalo(5000, 0, 5000), "passou exatamente o intervalo");
  VERIFICAR(!passouIntervalo(4999, 0, 5000), "ainda nao passou");
  VERIFICAR(passouIntervalo(100UL, 0xFFFFFF00UL, 200UL), "funciona quando o millis() da a volta");
  VERIFICAR(buzzerLigado(0) && buzzerLigado(249) && !buzzerLigado(250) && !buzzerLigado(599) && buzzerLigado(600),
            "padrao do buzzer: 250 ms ligado a cada 600 ms");

  // ---- JSON enviado a API ----
  char json[160];
  int n = montarJson(json, sizeof(json), 1, 35.0f, 65.0f, RISCO_ALERTA, "esp32-ipiranga-01");
  VERIFICAR(n > 0, "JSON cabe no buffer");
  VERIFICAR(std::strcmp(json,
                        "{\"pontoId\":1,\"distanciaCm\":35.0,\"nivelCm\":65.0,\"risco\":\"ALERTA\",\"dispositivo\":\"esp32-ipiranga-01\"}") == 0,
            "JSON no formato esperado pela API C# (pontoId e distanciaCm)");
  char pequeno[10];
  VERIFICAR(montarJson(pequeno, sizeof(pequeno), 1, 35.0f, 65.0f, RISCO_ALERTA, "x") == -1, "buffer pequeno e detectado");

  // ---- display 16x2 ----
  char l1[20], l2[20];
  formatarLinhaNivel(l1, sizeof(l1), true, 65, false);
  formatarLinhaRisco(l2, sizeof(l2), true, RISCO_ALERTA, "ONLINE");
  VERIFICAR(std::strlen(l1) == 16 && std::strlen(l2) == 16, "linhas do LCD tem exatamente 16 colunas");
  VERIFICAR(std::strncmp(l1, "Agua: 65cm", 10) == 0, "linha 1: Agua: 65cm");
  VERIFICAR(std::strncmp(l2, "ALERTA ONLINE", 13) == 0, "linha 2: ALERTA ONLINE");
  formatarLinhaNivel(l1, sizeof(l1), true, 65, true);
  VERIFICAR(std::strstr(l1, "MUDO") != nullptr && std::strlen(l1) == 16, "linha 1 mostra MUDO");
  formatarLinhaNivel(l1, sizeof(l1), false, 0, false);
  VERIFICAR(std::strncmp(l1, "Sensor sem leitu", 16) == 0, "linha 1 sem sensor (cortada em 16)");
  formatarLinhaRisco(l2, sizeof(l2), true, RISCO_NORMAL, "OFFLINE");
  VERIFICAR(std::strlen(l2) == 16 && std::strncmp(l2, "SEM RISCO OFFLIN", 16) == 0, "pior caso cabe em 16 colunas");

  std::printf(falhas == 0 ? "\nTUDO OK\n" : "\n%d FALHA(S)\n", falhas);
  return falhas == 0 ? 0 : 1;
}
