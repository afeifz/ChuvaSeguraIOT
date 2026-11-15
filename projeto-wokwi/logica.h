/*
 * logica.h - Regras do nó sensor ChuvaSegura (sem dependência do Arduino).
 *
 * Fica separado do sketch.ino para ser fácil de ler e de testar. As regras são as MESMAS da API em C#:
 *   nível da água = altura de instalação - distância medida
 *   Normal  : nível < 30 cm
 *   Atenção : 30 cm <= nível < 60 cm
 *   Alerta  : nível >= 60 cm
 */
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdio.h>

enum Risco { RISCO_NORMAL = 0, RISCO_ATENCAO = 1, RISCO_ALERTA = 2 };

// Faixa de medição do sensor ultrassônico HC-SR04 (cm).
const float DISTANCIA_MIN_CM = 2.0f;
const float DISTANCIA_MAX_CM = 400.0f;

inline bool distanciaValida(float distanciaCm) {
  return !isnan(distanciaCm) && distanciaCm >= DISTANCIA_MIN_CM && distanciaCm <= DISTANCIA_MAX_CM;
}

/** Altura da água. Se o sensor medir além da altura de instalação, não há água (nível 0). */
inline float nivelAguaCm(float distanciaCm, float alturaInstalacaoCm) {
  float nivel = alturaInstalacaoCm - distanciaCm;
  return nivel < 0.0f ? 0.0f : nivel;
}

inline Risco classificarRisco(float nivelCm, float limiteAtencaoCm, float limiteAlertaCm) {
  if (nivelCm >= limiteAlertaCm) return RISCO_ALERTA;
  if (nivelCm >= limiteAtencaoCm) return RISCO_ATENCAO;
  return RISCO_NORMAL;
}

/** Nome usado no JSON (igual ao que o serviço Java espera). */
inline const char* nomeRisco(Risco risco) {
  switch (risco) {
    case RISCO_ALERTA: return "ALERTA";
    case RISCO_ATENCAO: return "ATENCAO";
    default: return "NORMAL";
  }
}

/** Texto curto para o display LCD (no máximo 9 letras). */
inline const char* rotuloRisco(Risco risco) {
  switch (risco) {
    case RISCO_ALERTA: return "ALERTA";
    case RISCO_ATENCAO: return "ATENCAO";
    default: return "SEM RISCO";
  }
}

/** Mediana de até 16 amostras: ignora leituras "malucas" isoladas do sensor. */
inline float mediana(const float* valores, int quantidade) {
  float copia[16];
  if (quantidade <= 0) return NAN;
  if (quantidade > 16) quantidade = 16;
  for (int i = 0; i < quantidade; i++) copia[i] = valores[i];
  for (int i = 1; i < quantidade; i++) {  // ordenação por inserção
    float chave = copia[i];
    int j = i - 1;
    while (j >= 0 && copia[j] > chave) {
      copia[j + 1] = copia[j];
      j--;
    }
    copia[j + 1] = chave;
  }
  if (quantidade % 2 == 1) return copia[quantidade / 2];
  return (copia[quantidade / 2 - 1] + copia[quantidade / 2]) / 2.0f;
}

/**
 * Evita "pisca-pisca": o risco só muda depois de N leituras seguidas apontando para o mesmo novo nível.
 * atualizar() devolve true quando o risco confirmado mudou.
 */
struct ConfirmadorRisco {
  Risco atual = RISCO_NORMAL;
  Risco candidato = RISCO_NORMAL;
  int contagem = 0;

  bool atualizar(Risco lido, int confirmacoes) {
    if (lido == atual) {
      candidato = atual;
      contagem = 0;
      return false;
    }
    if (lido == candidato) {
      contagem++;
    } else {
      candidato = lido;
      contagem = 1;
    }
    if (contagem >= confirmacoes) {
      atual = lido;
      candidato = lido;
      contagem = 0;
      return true;
    }
    return false;
  }
};

/** Diferença de tempo segura mesmo quando millis() "dá a volta" (a cada ~49 dias). */
inline bool passouIntervalo(unsigned long agoraMs, unsigned long ultimoMs, unsigned long intervaloMs) {
  return (agoraMs - ultimoMs) >= intervaloMs;
}

/** Padrão do buzzer em ALERTA: 250 ms ligado a cada 600 ms. */
inline bool buzzerLigado(unsigned long agoraMs) {
  return (agoraMs % 600UL) < 250UL;
}

/**
 * Monta o JSON enviado à API. Os campos "pontoId" e "distanciaCm" são os que a API C# usa
 * (POST /api/leituras); os demais são informativos.
 * Devolve o tamanho escrito, ou -1 se não coube no buffer.
 */
inline int montarJson(char* destino, size_t tamanho, int pontoId, float distanciaCm, float nivelCm, Risco risco,
                      const char* dispositivo) {
  int escrito = snprintf(destino, tamanho,
                         "{\"pontoId\":%d,\"distanciaCm\":%.1f,\"nivelCm\":%.1f,\"risco\":\"%s\",\"dispositivo\":\"%s\"}",
                         pontoId, (double)distanciaCm, (double)nivelCm, nomeRisco(risco), dispositivo);
  if (escrito < 0 || (size_t)escrito >= tamanho) return -1;
  return escrito;
}

/** Linha 1 do LCD (16 colunas): "Agua: 65cm MUDO". */
inline void formatarLinhaNivel(char* destino, size_t tamanho, bool sensorOk, int nivelCm, bool mudo) {
  char texto[24];
  if (!sensorOk) {
    snprintf(texto, sizeof(texto), "Sensor sem leitura");
  } else {
    snprintf(texto, sizeof(texto), "Agua:%3dcm%s", nivelCm, mudo ? " MUDO" : "");
  }
  snprintf(destino, tamanho, "%-16.16s", texto);
}

/** Linha 2 do LCD (16 colunas): "ALERTA ONLINE". */
inline void formatarLinhaRisco(char* destino, size_t tamanho, bool sensorOk, Risco risco, const char* rede) {
  char texto[32];
  snprintf(texto, sizeof(texto), "%s %s", sensorOk ? rotuloRisco(risco) : "---", rede);
  snprintf(destino, tamanho, "%-16.16s", texto);
}
