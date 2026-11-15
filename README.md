# ChuvaSegura – Physical Computing (ESP32 no Wokwi)

## Ficha da CitizenTech

| Campo | Conteúdo |
|---|---|
| **Nome da CitizenTech** | ChuvaSegura |
| **Cidade e bairro** | São Paulo – Ipiranga (região do Rio Tamanduateí) |
| **Cidadão beneficiado** | Moradores e comerciantes do Ipiranga em ruas com histórico de alagamento, com atenção a idosos e pessoas com mobilidade reduzida |
| **Problema, em uma frase** | Moradores de áreas que alagam recebem o aviso de chuva forte tarde demais, ou de forma genérica demais, para proteger a família e seus bens. |
| **Como funciona** | Um sensor mede o nível da água num ponto crítico; um modelo de ML estima a chance de chuva forte; a API combina os dois e envia alertas ao app do morador, que também registra ocorrências. |
| **O que cada parte faz** | **IoT (ESP32/Wokwi): mede e sinaliza o nível** · API C#: recebe leituras e calcula risco · API Java: cadastro e alertas · ML: previsão de chuva forte com dados do INMET · App Expo: risco do bairro, alertas e ocorrências · Linux: hospeda e monitora o sistema · Cybersecurity: protege os dados dos moradores · QA: features e casos de teste |
| **Link do repositório** | https://github.com/afeifz/ChuvaSeguraIOT.git |
| **Indicador de impacto** | Minutos de antecedência entre o alerta e o nível crítico; % de chuvas fortes detectadas pelo modelo (recall) |
| **ODS conectados** | 11 (Cidades sustentáveis), 13 (Ação climática), 9 (Infraestrutura e inovação), 10 (Redução das desigualdades) |


**Equipe:** 

**Mohamed Afif RM 554445**

**Lucca Cardinale RM556668**

| Pasta / arquivo | O que é |
|---|---|
| `projeto-wokwi/` | Circuito e firmware (`sketch.ino`, `logica.h`, `diagram.json`, `libraries.txt`) e o passo a passo para criar o projeto no Wokwi |
| `ponte/` | Ponte MQTT → API C# (Python, sem dependências) |
| `tests/` | Testes do firmware e da ponte, que rodam no computador |
| `INTEGRANTES.txt` | Arquivo .txt com os nomes dos integrantes (item da entrega) |


1. **Link do projeto no Wokwi**: https://wokwi.com/projects/480429095495072769
2. **Link do vídeo**: https://youtu.be/VH9Y-xrN9D4
3. **Link da API C#**: https://github.com/afeifz/ChuvaSeguraC.git


