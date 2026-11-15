"""
Teste da ponte MQTT -> API sem internet: sobe um broker MQTT mínimo, uma API falsa e um "ESP32" que publica.
    python test_ponte.py
"""
import http.server
import importlib.util
import json
import pathlib
import socket
import struct
import threading
import time
from types import SimpleNamespace

caminho = pathlib.Path(__file__).resolve().parent.parent / "ponte" / "ponte_mqtt_http.py"
spec = importlib.util.spec_from_file_location("ponte", caminho)
ponte = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ponte)

falhas = 0


def verificar(condicao, nome):
    global falhas
    print(("OK   " if condicao else "FALHA"), nome)
    if not condicao:
        falhas += 1


# ---------------------------------------------------------------- broker MQTT mínimo (independente da ponte)
assinantes = []  # (socket, topico)


def ler_exato(sock, n):
    dados = b""
    while len(dados) < n:
        parte = sock.recv(n - len(dados))
        if not parte:
            raise ConnectionError
        dados += parte
    return dados


def ler_pacote(sock):
    cabecalho = ler_exato(sock, 1)[0]
    tamanho, mult = 0, 1
    while True:
        b = ler_exato(sock, 1)[0]
        tamanho += (b & 0x7F) * mult
        mult *= 128
        if not b & 0x80:
            break
    return cabecalho, ler_exato(sock, tamanho)


def atender(sock):
    try:
        while True:
            cab, corpo = ler_pacote(sock)
            tipo = cab >> 4
            if tipo == 1:  # CONNECT
                assert corpo[2:6] == b"MQTT" and corpo[6] == 4, "protocolo MQTT 3.1.1"
                sock.sendall(bytes([0x20, 0x02, 0x00, 0x00]))
            elif tipo == 8:  # SUBSCRIBE
                id_pacote = corpo[:2]
                tam = struct.unpack("!H", corpo[2:4])[0]
                topico = corpo[4 : 4 + tam].decode()
                assinantes.append((sock, topico))
                sock.sendall(bytes([0x90, 0x03]) + id_pacote + bytes([0x00]))
            elif tipo == 12:  # PINGREQ
                sock.sendall(bytes([0xD0, 0x00]))
            elif tipo == 3:  # PUBLISH vindo do "ESP32"
                tam = struct.unpack("!H", corpo[:2])[0]
                topico = corpo[2 : 2 + tam].decode()
                for destino, filtro in list(assinantes):
                    if filtro == topico:
                        destino.sendall(bytes([cab]) + ponte._codificar_tamanho(len(corpo)) + corpo)
    except (ConnectionError, OSError):
        pass


broker = socket.socket()
broker.bind(("127.0.0.1", 0))
broker.listen()
PORTA_BROKER = broker.getsockname()[1]


def aceitar():
    while True:
        try:
            cliente, _ = broker.accept()
        except OSError:
            return
        threading.Thread(target=atender, args=(cliente,), daemon=True).start()


threading.Thread(target=aceitar, daemon=True).start()

# ---------------------------------------------------------------- API falsa
recebidos = []


class ApiFalsa(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        corpo = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        recebidos.append((self.path, corpo, self.headers.get("Content-Type")))
        if corpo["distanciaCm"] > 400:
            self.send_response(422)
            self.send_header("Content-Type", "application/problem+json")
            self.end_headers()
            self.wfile.write(json.dumps({"title": "Leitura inválida"}).encode())
        else:
            self.send_response(201)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b"{}")

    def log_message(self, *a):
        pass


api = http.server.HTTPServer(("127.0.0.1", 0), ApiFalsa)
threading.Thread(target=api.serve_forever, daemon=True).start()
URL_API = f"http://127.0.0.1:{api.server_address[1]}"

# ---------------------------------------------------------------- funções puras
verificar(ponte._codificar_tamanho(0) == b"\x00", "tamanho 0 codificado")
verificar(ponte._codificar_tamanho(127) == b"\x7f", "tamanho 127 em 1 byte")
verificar(ponte._codificar_tamanho(128) == b"\x80\x01", "tamanho 128 em 2 bytes")
verificar(ponte._codificar_tamanho(321) == b"\xc1\x02", "tamanho 321 em 2 bytes")
buf = bytearray(b"\x30\x03abc\x30\x05ab")  # um pacote completo e um incompleto
pacotes = ponte.extrair_pacotes(buf)
verificar(len(pacotes) == 1 and pacotes[0][1] == b"abc" and bytes(buf) == b"\x30\x05ab", "extrai pacote completo e guarda o incompleto")
verificar(ponte.interpretar_leitura(b'{"pontoId":2,"distanciaCm":35.0}', 1) == (2, 35.0), "interpreta JSON do ESP32")
verificar(ponte.interpretar_leitura(b'{"distanciaCm":35}', 7) == (7, 35.0), "usa o ponto padrao quando faltar pontoId")
for ruim in (b"nao e json", b"[1,2]", b'{"pontoId":1}', b'{"distanciaCm":"abc"}', b'{"pontoId":"x","distanciaCm":1}'):
    try:
        ponte.interpretar_leitura(ruim, 1)
        verificar(False, f"rejeita {ruim!r}")
    except ValueError:
        verificar(True, f"rejeita {ruim!r}")

# ---------------------------------------------------------------- ponte de verdade
args = SimpleNamespace(equipe="equipe-teste", api=URL_API, ponto=1, broker="127.0.0.1", porta=PORTA_BROKER)
TOPICO = "chuvasegura/equipe-teste/ponto1/leitura"
threading.Thread(target=lambda: ponte.escutar(args), daemon=True).start()

for _ in range(50):
    if assinantes:
        break
    time.sleep(0.1)
verificar(len(assinantes) == 1 and assinantes[0][1] == TOPICO, "ponte conectou e assinou o topico certo")

# "ESP32" publicando
esp = socket.create_connection(("127.0.0.1", PORTA_BROKER))
esp.sendall(ponte.montar_connect("esp32-teste", 60))
ler_pacote(esp)  # CONNACK


def publicar(payload: bytes, topico=TOPICO):
    corpo = struct.pack("!H", len(topico)) + topico.encode() + payload
    esp.sendall(bytes([0x30]) + ponte._codificar_tamanho(len(corpo)) + corpo)


def esperar(n, limite=5.0):
    fim = time.time() + limite
    while time.time() < fim and len(recebidos) < n:
        time.sleep(0.05)


json_esp32 = b'{"pontoId":1,"distanciaCm":35.0,"nivelCm":65.0,"risco":"ALERTA","dispositivo":"esp32-ipiranga-01"}'
publicar(json_esp32)
esperar(1)
verificar(len(recebidos) == 1, "leitura do ESP32 chegou na API")
verificar(recebidos and recebidos[0][0] == "/api/leituras", "enviou para POST /api/leituras")
verificar(recebidos and recebidos[0][1] == {"pontoId": 1, "distanciaCm": 35.0}, "corpo contem so pontoId e distanciaCm (o que a API C# usa)")
verificar(recebidos and recebidos[0][2] == "application/json", "Content-Type application/json")

publicar(b"isto nao e json")
publicar(b'{"pontoId":1,"distanciaCm":450.0}')  # a API responde 422
esperar(2)
time.sleep(0.5)
verificar(len(recebidos) == 2 and recebidos[1][1]["distanciaCm"] == 450.0, "JSON invalido e ignorado; leitura fora da faixa chega (e a API responde 422)")

publicar(b'{"pontoId":1,"distanciaCm":60.0}')
esperar(3)
verificar(len(recebidos) == 3 and recebidos[2][1]["distanciaCm"] == 60.0, "a ponte continua funcionando depois de erro 422")

publicar(b'{"pontoId":1,"distanciaCm":12.5,"dispositivo":"' + b"x" * 200 + b'"}')  # pacote com tamanho > 127 (2 bytes de tamanho)
esperar(4)
verificar(len(recebidos) == 4 and recebidos[3][1]["distanciaCm"] == 12.5, "pacote grande (tamanho em 2 bytes) e tratado")

publicar(b'{"pontoId":1,"distanciaCm":99.0}', topico="chuvasegura/outra-equipe/ponto1/leitura")
time.sleep(0.5)
verificar(len(recebidos) == 4, "mensagens de outra equipe (outro topico) nao chegam")

print("\nTUDO OK" if falhas == 0 else f"\n{falhas} FALHA(S)")
raise SystemExit(1 if falhas else 0)
