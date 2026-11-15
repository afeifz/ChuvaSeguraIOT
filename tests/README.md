# Testes (rodam no computador, sem Wokwi)

Estes testes verificam o **código** do firmware e da ponte. Eles **não** substituem testar o circuito no Wokwi.

| Teste | O que verifica | Como rodar |
|---|---|---|
| `test_logica.cpp` | Regras de `logica.h`: faixa do sensor, nível, risco, mediana, confirmação, JSON, display | `g++ -std=c++17 -I../projeto-wokwi test_logica.cpp -o test_logica && ./test_logica` |
| `teste_sketch.cpp` | O `sketch.ino` **de verdade**, com um "hardware" simulado (`mocks/`): LEDs, buzzer, botão, display, MQTT, offline, sensor sem eco | `g++ -std=c++17 -Imocks -I../projeto-wokwi -x c++ teste_sketch.cpp -o teste_sketch && ./teste_sketch` |
| `test_ponte.py` | A ponte, com um broker MQTT mínimo e uma API falsa | `python test_ponte.py` |

Precisa de `g++` (compilador C++) para os dois primeiros e de Python 3 para o último.
