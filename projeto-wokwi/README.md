# ChuvaSegura – Circuito ESP32 no Wokwi

> **Global Solution FIAP 2026.2 · CitizenTech · Physical Computing: IoT & IoB**


## O que este circuito faz pelo morador do Ipiranga

O circuito é o **sensor de rua** do ChuvaSegura: fica preso acima de um ponto que alaga, mede a distância até a água e transforma isso em aviso. Ele faz duas coisas ao mesmo tempo:

| | O que faz | Por que importa |
|---|---|---|
| **1. Avisa no local** | LED verde/amarelo/vermelho, buzzer em ALERTA e display com o nível da água | Quem passa pela rua vê o risco **mesmo sem celular e sem internet** |
| **2. Avisa a nuvem** | Envia a leitura por Wi-Fi (MQTT → ponte → API C#) | A API guarda o histórico, o serviço Java avisa os moradores e o app mostra o risco |

Ele **não é um projeto paralelo**: a leitura que sai daqui entra exatamente no `POST /api/leituras` da API em C# (`pontoId` + `distanciaCm`), e as regras de risco são as mesmas da API (Normal < 30 cm, Atenção 30–59 cm, Alerta ≥ 60 cm).

```
 HC-SR04 ──► ESP32 ──► LEDs + buzzer + display      (aviso local, funciona offline)
              │
              └─ Wi-Fi ─► broker MQTT ─► ponte (seu PC) ─► API C# ─► API Java ─► App
```

## Peças

| Peça (Wokwi) | Função | Pino do ESP32 |
|---|---|---|
| ESP32 DevKit C v4 | Microcontrolador com Wi-Fi | — |
| `wokwi-hc-sr04` | Mede a distância até a água (2 a 400 cm) | TRIG = **26**, ECHO = **27**, VCC = 5V, GND |
| LED verde + resistor 220 Ω | Sem risco | **25** |
| LED amarelo + resistor 220 Ω | Atenção | **33** |
| LED vermelho + resistor 220 Ω | Alerta | **32** |
| `wokwi-buzzer` | Som intermitente em ALERTA | **23** |
| `wokwi-pushbutton` | Silencia o buzzer por 60 s | **19** (liga ao GND; pull-up interno) |
| `wokwi-lcd1602` (I2C, endereço 0x27) | Mostra nível e risco | SDA = **21**, SCL = **22**, VCC = 3V3, GND |

Fiz a escolha de pinos evitando os pinos "de boot" do ESP32 (0, 2, 5, 12 e 15).

## Como criar o projeto no Wokwi (5 minutos)

1. Acesse **https://wokwi.com** e entre com uma conta gratuita (necessária para salvar e compartilhar).
2. Clique em **New Project → ESP32 → Arduino** (o modelo "ESP32" simples).
3. Na aba **sketch.ino**, apague tudo e cole o conteúdo de `sketch.ino` deste pacote.
4. Crie o arquivo **logica.h** (botão com a seta ▾ ao lado das abas → *New file*) e cole o conteúdo de `logica.h`.
5. Abra a aba **libraries.txt** e deixe exatamente as duas linhas de `libraries.txt` deste pacote (`PubSubClient` e `LiquidCrystal I2C`).
6. Abra a aba **diagram.json** e cole o conteúdo de `diagram.json` (o circuito aparece montado).
7. No `sketch.ino`, troque **`ID_EQUIPE`** por um id só seu (ex.: `gs2026-rm12345-rm67890`). O broker é público e, se duas equipes usarem o mesmo id, as leituras se misturam.
8. Clique no botão verde **Start the simulation**.


## Como testar

No simulador, **clique no sensor HC-SR04** e mexa no controle **Distance** (altura de instalação = 100 cm, então nível = 100 − distância):

| Distance | Nível da água | O que deve acontecer |
|---|---|---|
| 90 cm | 10 cm | LED **verde**, display "SEM RISCO", sem som |
| 60 cm | 40 cm | LED **amarelo**, display "ATENCAO" |
| 35 cm | 65 cm | LED **vermelho**, **buzzer** intermitente, display "ALERTA" |
| 450 cm (ou sensor sem eco) | — | LED amarelo **piscando** e display "Sensor sem leitura"; nada é enviado |

- O risco só muda depois de **2 leituras seguidas** (~1 segundo), para o LED não ficar piscando por causa de ruído.
- **Botão azul:** silencia o buzzer por 60 s (aparece "MUDO" no display). Apertar de novo reativa.
- No **monitor serial** aparecem as leituras em JSON, como `{"pontoId":1,"distanciaCm":35.0,"nivelCm":65.0,"risco":"ALERTA",...}`, e as mensagens `[mqtt] publicado`.
- O display mostra **ONLINE** quando o MQTT está conectado e **OFFLINE** quando não está. O alarme local continua funcionando nos dois casos.

## Ligando o circuito à API (integração com o resto do ChuvaSegura)

O Wokwi não enxerga o `localhost` do seu computador, então usamos **MQTT** (padrão de IoT): o ESP32 publica em um broker público e uma **ponte** no seu computador repassa para a API.

1. Suba a API C# (`dotnet run --project src/ChuvaSegura.Monitoramento.Api`, porta 5080). Se quiser os alertas, deixe `AlertasService:Habilitado = true` e suba a API Java.
2. Em outro terminal, rode a ponte com o **mesmo `ID_EQUIPE`** do sketch:
   ```bash
   python ponte/ponte_mqtt_http.py --equipe seuidaqui
   ```
3. Mexa no sensor no Wokwi. A ponte mostra `API C# aceitou (201)`.
4. Confira na API (Swagger `http://localhost:5080/swagger`): `GET /api/pontos/1/risco` e `GET /api/bairros/Ipiranga/risco` já mostram o nível enviado pelo ESP32.
5. Se a API Java estiver no ar, `GET http://localhost:8080/api/alertas` mostra os alertas criados. O app (aba Risco e aba Alertas) também reflete.

**Plano B (sem internet ou sem broker):** troque `MODO_ENVIO` para `SO_SERIAL`. O circuito continua igual e o JSON aparece no monitor serial. Aí é só colar o JSON no Swagger (`POST /api/leituras`) para demonstrar a integração.

**Opcional (HTTP direto):** com a API em um endereço público (por exemplo, com um túnel do ngrok), use `MODO_ENVIO = VIA_HTTP` e coloque o endereço em `API_URL`.

## Como o firmware funciona (para explicar no vídeo)

- `logica.h`: as regras (nível, risco, mediana, confirmação, JSON, texto do display). Não depende do Arduino, por isso foi testado no computador.
- `sketch.ino`: liga tudo.
  - **Medir:** a cada 500 ms lê 3 vezes o HC-SR04 e usa a **mediana** (ignora leituras malucas isoladas). Distância = tempo do eco ÷ 58.
  - **Decidir:** nível = altura de instalação − distância; classifica em Normal/Atenção/Alerta; só troca o risco após 2 leituras seguidas.
  - **Atuar:** LEDs, buzzer (250 ms ligado a cada 600 ms) e display.
  - **Enviar:** a cada 5 s e **na hora** em que o risco muda; leituras inválidas nunca são enviadas.
  - **Rede sem travar:** Wi-Fi e MQTT reconectam sozinhos em segundo plano; o alarme local **nunca depende da internet**.

## Do Wokwi para o mundo real 

| Item | No Wokwi | Na placa de verdade |
|---|---|---|
| HC-SR04 | Ligado direto | O ECHO sai em **5 V** e o ESP32 aceita **3,3 V**: use divisor de tensão (1 kΩ + 2 kΩ). Na rua, prefira um sensor à prova d'água (ex.: JSN-SR04T) |
| LEDs | Resistor de 220 Ω | O mesmo (limita a corrente) |
| Buzzer | Passivo, tocado por `tone()` | Se for buzzer **ativo**, basta ligar/desligar o pino |
| Display LCD I2C | Alimentado em 3V3 | O módulo costuma precisar de 5 V |
| Alimentação | Automática | Fonte/bateria + painel solar, caixa protegida da chuva |

## Limitações 

- O **broker MQTT é público e sem senha**: qualquer pessoa poderia publicar no mesmo tópico. Por isso o `ID_EQUIPE` precisa ser único. Em produção: broker privado, **TLS** e autenticação (assunto de Cybersecurity).
- Os limites de risco estão repetidos no firmware e na API. Para mudar um, mude os dois.
- A simulação mede a distância que **você** escolhe no controle; no mundo real haveria ondas, espuma, chuva e lixo no caminho do sensor (por isso a mediana).

## Problemas comuns

| Sintoma | O que fazer |
|---|---|
| Erro de compilação sobre `PubSubClient` ou `LiquidCrystal_I2C` | Confira o `libraries.txt` (uma biblioteca por linha, nomes exatos) |
| Display em branco | Confira no `diagram.json` o atributo `"pins": "i2c"` do `lcd` e as ligações SDA = 21, SCL = 22 |
| `[mqtt] falhou, codigo -2` | O broker pode estar fora do ar ou bloqueado: tente `MQTT_SERVIDOR = "test.mosquitto.org"` ou use `SO_SERIAL` |
| A ponte não mostra nada | O `--equipe` da ponte precisa ser **idêntico** ao `ID_EQUIPE` do sketch, e a simulação precisa estar rodando |
| A ponte mostra 404 | O `pontoId` não existe na API: o ponto de exemplo é o **1** |
| A ponte mostra 409 | O ponto está **desativado** na API (`POST /api/pontos/1/ativar`) |
| Buzzer mudo | Aumente o `volume` do buzzer no diagrama ou confira o botão de silenciar (display mostra "MUDO") |
