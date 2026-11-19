
"""
Ponte MQTT -> API (ChuvaSegura)

O ESP32 (no Wokwi) publica cada leitura em um broker MQTT público. Esta ponte roda no SEU computador,
escuta o mesmo tópico e repassa a leitura para a API de monitoramento em C#:

    ESP32 (Wokwi) --MQTT--> broker público --MQTT--> ESTA PONTE --HTTP POST--> API C# /api/leituras

Só usa a biblioteca padrão do Python (não precisa instalar nada).

Uso:
    python ponte/ponte_mqtt_http.py --equipe troque-este-id
    ou
    python ponte/ponte_mqtt_http.py --equipe troque-este-id --api http://localhost:5080 --ponto 1

O --equipe precisa ser IGUAL ao ID_EQUIPE do sketch.ino.
"""
import argparse
import json
import socket
import struct
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime

# --------------------------------------------------------------------------------------
# Cliente MQTT 3.1.1 mínimo (somente assinar tópicos, QoS 0)
# --------------------------------------------------------------------------------------


def _codificar_tamanho(tamanho: int) -> bytes:
    saida = bytearray()
    while True:
        byte = tamanho % 128
        tamanho //= 128
        if tamanho > 0:
            byte |= 0x80
        saida.append(byte)
        if tamanho == 0:
            return bytes(saida)


def _texto(valor: str) -> bytes:
    bruto = valor.encode("utf-8")
    return struct.pack("!H", len(bruto)) + bruto


def _pacote(tipo_e_flags: int, corpo: bytes) -> bytes:
    return bytes([tipo_e_flags]) + _codificar_tamanho(len(corpo)) + corpo


def montar_connect(id_cliente: str, keepalive: int) -> bytes:
    corpo = _texto("MQTT") + bytes([4, 0x02]) + struct.pack("!H", keepalive) + _texto(id_cliente)
    return _pacote(0x10, corpo)


def montar_subscribe(id_pacote: int, topico: str) -> bytes:
    corpo = struct.pack("!H", id_pacote) + _texto(topico) + bytes([0])
    return _pacote(0x82, corpo)


PINGREQ = bytes([0xC0, 0x00])


def extrair_pacotes(buffer: bytearray):
    """Tira do buffer todos os pacotes MQTT completos. Devolve lista de (tipo_e_flags, corpo)."""
    pacotes = []
    while True:
        if len(buffer) < 2:
            break
        indice = 1
        tamanho = 0
        multiplicador = 1
        completo = False
        while indice < len(buffer) and indice <= 4:
            byte = buffer[indice]
            tamanho += (byte & 0x7F) * multiplicador
            multiplicador *= 128
            indice += 1
            if not (byte & 0x80):
                completo = True
                break
        if not completo or len(buffer) < indice + tamanho:
            break
        pacotes.append((buffer[0], bytes(buffer[indice : indice + tamanho])))
        del buffer[: indice + tamanho]
    return pacotes


def ler_publish(tipo_e_flags: int, corpo: bytes):
    """Interpreta um pacote PUBLISH. Devolve (topico, payload)."""
    qos = (tipo_e_flags >> 1) & 0x03
    (tamanho_topico,) = struct.unpack("!H", corpo[:2])
    topico = corpo[2 : 2 + tamanho_topico].decode("utf-8", errors="replace")
    posicao = 2 + tamanho_topico
    if qos > 0:
        posicao += 2  # identificador do pacote
    return topico, corpo[posicao:]


# --------------------------------------------------------------------------------------
# Repasse para a API
# --------------------------------------------------------------------------------------


def interpretar_leitura(payload: bytes, ponto_padrao: int):
    """Extrai (pontoId, distanciaCm) do JSON enviado pelo ESP32. Levanta ValueError se for inválido."""
    try:
        dados = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as erro:
        raise ValueError(f"JSON inválido: {erro}") from erro
    if not isinstance(dados, dict):
        raise ValueError("o JSON precisa ser um objeto")
    distancia = dados.get("distanciaCm")
    if isinstance(distancia, bool) or not isinstance(distancia, (int, float)):
        raise ValueError("faltou o campo numérico 'distanciaCm'")
    ponto = dados.get("pontoId", ponto_padrao)
    if isinstance(ponto, bool) or not isinstance(ponto, int):
        raise ValueError("'pontoId' precisa ser um número inteiro")
    return ponto, float(distancia)


def enviar_para_api(url_api: str, ponto_id: int, distancia_cm: float, timeout: float = 8.0):
    """POST /api/leituras. Devolve (status_http, texto_da_resposta)."""
    corpo = json.dumps({"pontoId": ponto_id, "distanciaCm": distancia_cm}).encode("utf-8")
    requisicao = urllib.request.Request(
        url_api.rstrip("/") + "/api/leituras",
        data=corpo,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(requisicao, timeout=timeout) as resposta:
            return resposta.status, resposta.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as erro:
        return erro.code, erro.read().decode("utf-8", errors="replace")


def registrar(mensagem: str):
    print(f"[{datetime.now():%H:%M:%S}] {mensagem}", flush=True)


def tratar_mensagem(topico: str, payload: bytes, args):
    texto = payload.decode("utf-8", errors="replace")
    registrar(f"recebido do ESP32: {texto}")
    try:
        ponto, distancia = interpretar_leitura(payload, args.ponto)
    except ValueError as erro:
        registrar(f"  ignorado ({erro})")
        return
    try:
        status, resposta = enviar_para_api(args.api, ponto, distancia)
    except (urllib.error.URLError, OSError) as erro:
        registrar(f"  API fora do ar? não consegui enviar: {erro}")
        return
    if status == 201:
        registrar(f"  API C# aceitou (201): ponto {ponto}, distância {distancia} cm")
    elif status == 422:
        registrar(f"  API rejeitou a leitura (422): {resposta[:120]}")
    else:
        registrar(f"  API respondeu {status}: {resposta[:120]}")


# --------------------------------------------------------------------------------------
# Laço principal
# --------------------------------------------------------------------------------------


def escutar(args):
    topico = f"chuvasegura/{args.equipe}/ponto{args.ponto}/leitura"
    registrar(f"conectando em {args.broker}:{args.porta} ...")
    with socket.create_connection((args.broker, args.porta), timeout=10) as sock:
        sock.settimeout(1.0)
        sock.sendall(montar_connect(f"ponte-chuvasegura-{int(time.time()) % 100000}", 60))
        buffer = bytearray()
        inscrito = False
        ultimo_ping = time.time()

        while True:
            try:
                dados = sock.recv(4096)
                if not dados:
                    raise ConnectionError("o broker fechou a conexão")
                buffer.extend(dados)
            except socket.timeout:
                pass

            for tipo_e_flags, corpo in extrair_pacotes(buffer):
                tipo = tipo_e_flags >> 4
                if tipo == 2:  # CONNACK
                    if len(corpo) < 2 or corpo[1] != 0:
                        raise ConnectionError(f"broker recusou a conexão (código {corpo[1:2]!r})")
                    sock.sendall(montar_subscribe(1, topico))
                elif tipo == 9:  # SUBACK
                    inscrito = True
                    registrar(f"escutando o tópico: {topico}")
                    registrar(f"repassando para: {args.api}/api/leituras   (Ctrl+C para parar)")
                elif tipo == 3:  # PUBLISH
                    t, payload = ler_publish(tipo_e_flags, corpo)
                    tratar_mensagem(t, payload, args)

            if inscrito and time.time() - ultimo_ping > 30:
                sock.sendall(PINGREQ)
                ultimo_ping = time.time()


def principal():
    parser = argparse.ArgumentParser(description="Ponte MQTT -> API de monitoramento (ChuvaSegura)")
    parser.add_argument("--equipe", required=True, help="mesmo ID_EQUIPE do sketch.ino")
    parser.add_argument("--api", default="http://localhost:5080", help="endereço da API C# (padrão: http://localhost:5080)")
    parser.add_argument("--ponto", type=int, default=1, help="id do ponto (padrão: 1)")
    parser.add_argument("--broker", default="broker.hivemq.com", help="broker MQTT (padrão: broker.hivemq.com)")
    parser.add_argument("--porta", type=int, default=1883, help="porta do broker (padrão: 1883)")
    args = parser.parse_args()

    espera = 2
    while True:
        try:
            escutar(args)
        except KeyboardInterrupt:
            registrar("encerrado.")
            return 0
        except (OSError, ConnectionError) as erro:
            registrar(f"conexão perdida ({erro}). Tentando de novo em {espera} s...")
            try:
                time.sleep(espera)
            except KeyboardInterrupt:
                return 0
            espera = min(espera * 2, 30)
        else:
            espera = 2


if __name__ == "__main__":
    sys.exit(principal())
