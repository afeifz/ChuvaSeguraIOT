# Ponte MQTT → API (ChuvaSegura)

Programa de **uma só arquivo** (`ponte_mqtt_http.py`) que roda no seu computador e liga o ESP32 do Wokwi à API em C#.

```
ESP32 (Wokwi) ──MQTT──► broker público ──MQTT──► ponte ──HTTP POST──► API C# (/api/leituras)
```

## Como usar

Precisa só do **Python 3** (não instala nada):

```bash
python ponte_mqtt_http.py --equipe SEU-ID-EQUIPE
```

| Opção | Padrão | Para que serve |
|---|---|---|
| `--equipe` | (obrigatória) | O mesmo `ID_EQUIPE` do `sketch.ino` |
| `--api` | `http://localhost:5080` | Endereço da API C# |
| `--ponto` | `1` | Id do ponto de monitoramento |
| `--broker` | `broker.hivemq.com` | Broker MQTT (o mesmo do sketch) |
| `--porta` | `1883` | Porta do broker |

O terminal mostra cada leitura recebida e a resposta da API (`201` aceita, `422` fora da faixa do sensor). Se a internet cair, a ponte reconecta sozinha. `Ctrl+C` encerra.

## Como funciona

- Implementa um cliente **MQTT 3.1.1** mínimo (conectar, assinar um tópico e receber mensagens), por isso não precisa de bibliotecas.
- Tópico assinado: `chuvasegura/<equipe>/ponto<id>/leitura`.
- Valida o JSON recebido e envia à API apenas `{ "pontoId": ..., "distanciaCm": ... }`.
