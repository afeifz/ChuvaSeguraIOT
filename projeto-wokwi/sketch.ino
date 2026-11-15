/*
 * ChuvaSegura - Nó sensor de nível de água (ESP32 + HC-SR04)
 * Global Solution FIAP 2026.2 - Physical Computing: IoT & IoB
 *
 * O que este circuito faz, no problema do morador do Ipiranga:
 *   1. MEDE a distância até a água com o sensor ultrassônico HC-SR04 e calcula o nível da água.
 *   2. AVISA NA HORA, no próprio local, mesmo sem internet: LED verde/amarelo/vermelho,
 *      buzzer em ALERTA e display com o nível (quem passa na rua vê o risco).
 *   3. ENVIA a leitura para a API de monitoramento (C#) pela internet (MQTT -> ponte -> API,
 *      ou HTTP direto). A API é quem guarda o histórico, avisa o serviço de alertas e atualiza o app.
 *
 * Botão: silencia o buzzer por 60 s (ou reativa, se já estiver silenciado).
 *
 * Bibliotecas (arquivo libraries.txt): PubSubClient, LiquidCrystal I2C.
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#include "logica.h"

// =============================== CONFIGURAÇÃO DA EQUIPE ===============================
const int PONTO_ID = 1;                         // id do ponto cadastrado na API C# (ponto de exemplo = 1)
const char* DISPOSITIVO = "esp32-ipiranga-01";  // nome deste dispositivo
const float ALTURA_INSTALACAO_CM = 100.0f;      // altura do sensor em relação ao fundo (igual ao cadastro do ponto)
const float LIMITE_ATENCAO_CM = 30.0f;          // mesmos limites da API C#
const float LIMITE_ALERTA_CM = 60.0f;

// Como enviar a leitura:
//   SO_SERIAL : só mostra no monitor serial (plano B, não precisa de internet)
//   VIA_MQTT  : publica em um broker MQTT público; a "ponte" no seu computador repassa para a API (recomendado)
//   VIA_HTTP  : faz POST direto na API (a API precisa estar em um endereço público, ex.: ngrok)
enum ModoEnvio { SO_SERIAL, VIA_MQTT, VIA_HTTP };
const ModoEnvio MODO_ENVIO = VIA_MQTT;

const char* WIFI_SSID = "Wokwi-GUEST";  // rede Wi-Fi aberta do simulador Wokwi
const char* WIFI_SENHA = "";
const int WIFI_CANAL = 6;

const char* MQTT_SERVIDOR = "broker.hivemq.com";
const int MQTT_PORTA = 1883;
// ATENÇÃO: o broker é PÚBLICO. Troque por um id único da sua equipe (ex.: os RMs), senão você
// vai misturar suas leituras com as de outras equipes. A ponte usa o mesmo id.
const char* ID_EQUIPE = "gs2026-troque-este-id";

const char* API_URL = "https://SEU-ENDERECO-PUBLICO/api/leituras";  // só para o modo VIA_HTTP

// =============================== PINOS (ESP32) ===============================
const int PIN_TRIG = 26;          // HC-SR04 TRIG
const int PIN_ECHO = 27;          // HC-SR04 ECHO (na placa real, use divisor de tensão 5V -> 3,3V)
const int PIN_LED_VERDE = 25;     // sem risco
const int PIN_LED_AMARELO = 33;   // atenção
const int PIN_LED_VERMELHO = 32;  // alerta
const int PIN_BUZZER = 23;
const int PIN_BOTAO = 19;         // silenciar buzzer (liga ao GND; usa pull-up interno)
const int PIN_SDA = 21;           // display LCD I2C
const int PIN_SCL = 22;
const int CANAL_BUZZER = 0;       // usado só em versões antigas do núcleo ESP32 (2.x)

// =============================== TEMPOS ===============================
const unsigned long INTERVALO_MEDICAO_MS = 500;
const unsigned long INTERVALO_ENVIO_MS = 5000;   // envia a cada 5 s (e na hora, se o risco mudar)
const unsigned long INTERVALO_DISPLAY_MS = 300;
const unsigned long SILENCIO_MS = 60000;
const int AMOSTRAS_POR_MEDICAO = 3;
const int CONFIRMACOES_PARA_MUDAR_RISCO = 2;     // 2 leituras seguidas (~1 s) para mudar o risco

// =============================== ESTADO ===============================
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClient wifiCliente;
PubSubClient mqtt(wifiCliente);
ConfirmadorRisco confirmador;

char topicoMqtt[96];
bool sensorOk = false;
float distanciaCm = 0;
float nivelCm = 0;
bool riscoMudou = false;
bool enviouAlgumaVez = false;
bool ultimoEnvioOk = false;
unsigned long ultimaMedicao = 0;
unsigned long ultimoEnvio = 0;
unsigned long ultimoDisplay = 0;
unsigned long ultimaTentativaWifi = 0;
unsigned long ultimaTentativaMqtt = 0;
bool tentouMqtt = false;
unsigned long silenciadoAte = 0;
unsigned long ultimoBotao = 0;
bool botaoAnterior = false;
bool buzzerTocando = false;
char linha1Anterior[20] = "";
char linha2Anterior[20] = "";

// =============================== BUZZER ===============================
void buzzerTom(int frequencia) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  tone(PIN_BUZZER, frequencia);
#else
  ledcWriteTone(CANAL_BUZZER, frequencia);
#endif
}

void buzzerSilencio() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  noTone(PIN_BUZZER);
#else
  ledcWriteTone(CANAL_BUZZER, 0);
#endif
}

// =============================== SENSOR ===============================
float lerDistanciaUmaVez() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  unsigned long duracaoUs = pulseIn(PIN_ECHO, HIGH, 30000UL);  // espera no máximo 30 ms
  if (duracaoUs == 0) return NAN;                               // sem eco: sensor desconectado ou fora de alcance
  return duracaoUs / 58.0f;                                     // µs -> cm
}

/** Mediana de algumas leituras, descartando as inválidas. */
float lerDistanciaCm() {
  float amostras[AMOSTRAS_POR_MEDICAO];
  int validas = 0;
  for (int i = 0; i < AMOSTRAS_POR_MEDICAO; i++) {
    float d = lerDistanciaUmaVez();
    if (distanciaValida(d)) {
      amostras[validas++] = d;
    }
    delay(15);
  }
  if (validas == 0) return NAN;
  return mediana(amostras, validas);
}

void medir() {
  float d = lerDistanciaCm();
  sensorOk = distanciaValida(d);
  if (!sensorOk) {
    return;
  }

  distanciaCm = d;
  nivelCm = nivelAguaCm(d, ALTURA_INSTALACAO_CM);
  Risco lido = classificarRisco(nivelCm, LIMITE_ATENCAO_CM, LIMITE_ALERTA_CM);

  if (confirmador.atualizar(lido, CONFIRMACOES_PARA_MUDAR_RISCO)) {
    riscoMudou = true;
    silenciadoAte = 0;  // risco novo: o buzzer volta a tocar
    char mensagem[80];
    snprintf(mensagem, sizeof(mensagem), "[risco] mudou para %s (nivel %.1f cm)", nomeRisco(confirmador.atual),
             (double)nivelCm);
    Serial.println(mensagem);
  }
}

// =============================== ATUADORES ===============================
void atualizarAtuadores(unsigned long agora) {
  Risco risco = confirmador.atual;
  bool verde = false;
  bool amarelo = false;
  bool vermelho = false;

  if (!sensorOk) {
    amarelo = ((agora / 500UL) % 2UL) == 0UL;  // amarelo piscando: sensor sem leitura
  } else {
    verde = (risco == RISCO_NORMAL);
    amarelo = (risco == RISCO_ATENCAO);
    vermelho = (risco == RISCO_ALERTA);
  }
  digitalWrite(PIN_LED_VERDE, verde ? HIGH : LOW);
  digitalWrite(PIN_LED_AMARELO, amarelo ? HIGH : LOW);
  digitalWrite(PIN_LED_VERMELHO, vermelho ? HIGH : LOW);

  bool mudo = agora < silenciadoAte;
  bool deveTocar = sensorOk && risco == RISCO_ALERTA && !mudo && buzzerLigado(agora);
  if (deveTocar != buzzerTocando) {
    buzzerTocando = deveTocar;
    if (deveTocar) {
      buzzerTom(1800);
    } else {
      buzzerSilencio();
    }
  }
}

void tratarBotao(unsigned long agora) {
  bool pressionado = (digitalRead(PIN_BOTAO) == LOW);
  if (pressionado && !botaoAnterior && passouIntervalo(agora, ultimoBotao, 200UL)) {
    ultimoBotao = agora;
    if (agora < silenciadoAte) {
      silenciadoAte = 0;
      Serial.println("[botao] buzzer reativado");
    } else {
      silenciadoAte = agora + SILENCIO_MS;
      Serial.println("[botao] buzzer silenciado por 60 s");
    }
  }
  botaoAnterior = pressionado;
}

// =============================== REDE ===============================
const char* textoRede() {
  if (MODO_ENVIO == SO_SERIAL) return "LOCAL";
  if (MODO_ENVIO == VIA_MQTT) return mqtt.connected() ? "ONLINE" : "OFFLINE";
  return (WiFi.status() == WL_CONNECTED && ultimoEnvioOk) ? "ONLINE" : "OFFLINE";
}

void tentarMqtt() {
  char idCliente[64];
  snprintf(idCliente, sizeof(idCliente), "esp32-%s-%lu", ID_EQUIPE, (unsigned long)(millis() & 0xFFFFUL));
  Serial.print("[mqtt] conectando em ");
  Serial.println(MQTT_SERVIDOR);
  if (mqtt.connect(idCliente)) {
    Serial.println("[mqtt] conectado");
  } else {
    Serial.print("[mqtt] falhou, codigo ");
    Serial.println(mqtt.state());
  }
}

/** Mantém o Wi-Fi e o MQTT conectados SEM travar o resto (o alarme local nunca depende da internet). */
void gerenciarRede(unsigned long agora) {
  if (MODO_ENVIO == SO_SERIAL) return;

  if (WiFi.status() != WL_CONNECTED) {
    if (passouIntervalo(agora, ultimaTentativaWifi, 15000UL)) {
      ultimaTentativaWifi = agora;
      Serial.println("[wifi] conectando...");
      WiFi.begin(WIFI_SSID, WIFI_SENHA, WIFI_CANAL);
    }
    return;
  }

  if (MODO_ENVIO == VIA_MQTT) {
    if (mqtt.connected()) {
      mqtt.loop();
    } else if (!tentouMqtt || passouIntervalo(agora, ultimaTentativaMqtt, 5000UL)) {
      tentouMqtt = true;  // a primeira tentativa é imediata; depois, a cada 5 s
      ultimaTentativaMqtt = agora;
      tentarMqtt();
    }
  }
}

bool enviarMqtt(const char* json) {
  if (!mqtt.connected()) return false;
  return mqtt.publish(topicoMqtt, json);
}

bool enviarHttp(const char* json) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  WiFiClientSecure clienteSeguro;
  WiFiClient clienteSimples;
  bool https = (strncmp(API_URL, "https", 5) == 0);

  if (https) {
    clienteSeguro.setInsecure();  // simulação/demonstração: não valida o certificado
    http.begin(clienteSeguro, API_URL);
  } else {
    http.begin(clienteSimples, API_URL);
  }
  http.addHeader("Content-Type", "application/json");
  int codigo = http.POST(json);
  http.end();

  Serial.print("[http] resposta ");
  Serial.println(codigo);
  return codigo >= 200 && codigo < 300;
}

void enviarSeNecessario(unsigned long agora) {
  if (!sensorOk) return;

  bool porTempo = !enviouAlgumaVez || passouIntervalo(agora, ultimoEnvio, INTERVALO_ENVIO_MS);
  if (!porTempo && !riscoMudou) return;

  ultimoEnvio = agora;
  enviouAlgumaVez = true;
  riscoMudou = false;

  char json[160];
  if (montarJson(json, sizeof(json), PONTO_ID, distanciaCm, nivelCm, confirmador.atual, DISPOSITIVO) < 0) {
    Serial.println("[erro] JSON maior que o buffer");
    return;
  }

  // Sempre mostra no monitor serial (serve de plano B e de evidência no vídeo).
  Serial.print("[leitura] ");
  Serial.println(json);

  if (MODO_ENVIO == VIA_MQTT) {
    ultimoEnvioOk = enviarMqtt(json);
    Serial.println(ultimoEnvioOk ? "[mqtt] publicado" : "[mqtt] sem conexao: leitura so local");
  } else if (MODO_ENVIO == VIA_HTTP) {
    ultimoEnvioOk = enviarHttp(json);
  }
}

// =============================== DISPLAY ===============================
void atualizarDisplay(unsigned long agora) {
  if (!passouIntervalo(agora, ultimoDisplay, INTERVALO_DISPLAY_MS)) return;
  ultimoDisplay = agora;

  char linha1[20];
  char linha2[20];
  formatarLinhaNivel(linha1, sizeof(linha1), sensorOk, (int)(nivelCm + 0.5f), agora < silenciadoAte);
  formatarLinhaRisco(linha2, sizeof(linha2), sensorOk, confirmador.atual, textoRede());

  if (strcmp(linha1, linha1Anterior) != 0) {
    lcd.setCursor(0, 0);
    lcd.print(linha1);
    strcpy(linha1Anterior, linha1);
  }
  if (strcmp(linha2, linha2Anterior) != 0) {
    lcd.setCursor(0, 1);
    lcd.print(linha2);
    strcpy(linha2Anterior, linha2);
  }
}

// =============================== SETUP / LOOP ===============================
void testeInicial() {
  const int leds[3] = {PIN_LED_VERDE, PIN_LED_AMARELO, PIN_LED_VERMELHO};
  for (int i = 0; i < 3; i++) {
    digitalWrite(leds[i], HIGH);
    delay(200);
    digitalWrite(leds[i], LOW);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("ChuvaSegura - no sensor de nivel de agua");

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_AMARELO, OUTPUT);
  pinMode(PIN_LED_VERMELHO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BOTAO, INPUT_PULLUP);

#if !(defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcSetup(CANAL_BUZZER, 2000, 8);
  ledcAttachPin(PIN_BUZZER, CANAL_BUZZER);
#endif

  Wire.begin(PIN_SDA, PIN_SCL);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("ChuvaSegura     ");
  lcd.setCursor(0, 1);
  lcd.print("Iniciando...    ");

  testeInicial();

  snprintf(topicoMqtt, sizeof(topicoMqtt), "chuvasegura/%s/ponto%d/leitura", ID_EQUIPE, PONTO_ID);
  if (MODO_ENVIO != SO_SERIAL) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_SENHA, WIFI_CANAL);
    ultimaTentativaWifi = millis();
  }
  if (MODO_ENVIO == VIA_MQTT) {
    mqtt.setServer(MQTT_SERVIDOR, MQTT_PORTA);
    mqtt.setBufferSize(512);
    Serial.print("[mqtt] topico: ");
    Serial.println(topicoMqtt);
  }
}

void loop() {
  unsigned long agora = millis();

  if (passouIntervalo(agora, ultimaMedicao, INTERVALO_MEDICAO_MS)) {
    ultimaMedicao = agora;
    medir();
  }

  tratarBotao(agora);
  atualizarAtuadores(agora);
  gerenciarRede(agora);
  enviarSeNecessario(agora);
  atualizarDisplay(agora);
}
