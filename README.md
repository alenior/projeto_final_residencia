# Projeto Final de Residência - Estufa IoT

## Teste inicial MQTT (HiveMQ) com ESP32-S3 + MicroPython

### 1) Configuração do broker (HiveMQ)
1. Crie uma conta no HiveMQ Cloud.
2. Crie um cluster (plano gratuito serve para testes).
3. Em **Access Management > Credentials**, crie usuário/senha.
4. Em **Clients/WebSocket Client**, abra o cliente web para inspecionar tópicos.
5. Use o host e porta do cluster:
   - TLS: `8883`
   - Sem TLS (se habilitado): `1883`

### 2) Configuração do firmware
No arquivo `envio_e_recebimento_nuvem.py`, ajuste:
- `MQTT_BROKER`
- `MQTT_PORT`
- `MQTT_USER`
- `MQTT_PASSWORD`
- `DEVICE_ID`

No arquivo `wifi.py`, ajuste:
- `SSID`
- `PASSWORD`

### 3) Tópicos usados
- `estufa/<device_id>/telemetria`
- `estufa/<device_id>/alertas`
- `estufa/<device_id>/status`
- `estufa/<device_id>/comandos`
- `estufa/<device_id>/teste`

### 4) Fluxo de teste sugerido
1. Suba o firmware no Wokwi/ESP32-S3.
2. Verifique no serial:
   - conexão Wi-Fi
   - sincronização NTP
   - conexão MQTT e tópicos impressos
3. No cliente HiveMQ, assine `estufa/<device_id>/#`.
4. Verifique publicação automática em `.../teste` e `.../status` no boot.
5. Publique comando no tópico `.../comandos`.

Comandos de exemplo:
```json
{"comando":"irrigar","status":true}
```
```json
{"comando":"aquecer","status":false}
```
```json
{"comando":"capturar","status":true}
```

### 5) Observações
- O módulo MQTT usa `umqtt.simple`.
- Se o cluster exigir TLS, evoluir para configuração SSL no cliente MicroPython.
- Firebase e Flutter serão integrados em etapa posterior.


### 6) Erros comuns de conexão MQTT
- **Porta 8883 com `MQTT_SSL=False`**: no HiveMQ Cloud isso falha no handshake. Use `MQTT_SSL=True`.
- **Credenciais inválidas**: usuário/senha incorretos causam falha de autenticação.
- **Relógio não sincronizado**: em alguns cenários TLS pode falhar; garanta NTP antes do MQTT.
- **Broker/porta trocados**: confirme host do cluster e porta correta (8883 TLS).
- **Sem CA/certificados quando exigido**: dependendo do firmware MicroPython, pode ser necessário ajustar `ssl_params`.