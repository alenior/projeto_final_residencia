# Projeto Final de Residência - Estufa IoT

## Modo atual de prototipação MQTT (facilitado)
Para acelerar o desenvolvimento do firmware, o projeto está configurado para broker público sem TLS/autenticação:

- `MQTT_BROKER = "broker.hivemq.com"`
- `MQTT_PORT = 1883`
- `MQTT_USER = ""`
- `MQTT_PASSWORD = ""`

> Os refinamentos de segurança (TLS/ACL/credenciais privadas) serão retomados em etapa posterior.

## Tópicos do projeto
Publicação:
- `estufa/<device_id>/telemetria`
- `estufa/<device_id>/alertas`
- `estufa/<device_id>/status`
- `estufa/<device_id>/teste`

Subscrição:
- `estufa/<device_id>/comandos`
- `estufa/comandos` (comandos gerais)

## Fluxo de teste bidirecional (ESP32 ↔ broker)
1. Copie `secrets.py.example` para `secrets.py` e ajuste Wi-Fi/device_id.
2. Suba firmware no ESP32.
3. Abra um cliente MQTT (HiveMQ Web Client ou MQTT Explorer) e assine:
   - `estufa/<device_id>/#`
   - `estufa/comandos`
4. Verifique mensagens de boot/status/telemetria.
5. Publique comando de teste em `estufa/<device_id>/comandos` ou `estufa/comandos`:
```json
{"comando":"capturar","status":true}
```

## Próximo passo recomendado: Firebase
Sim — o próximo passo adequado é iniciar Firebase com foco em:
- **Firebase Storage** para imagens capturadas pela câmera.
- **Cloud Firestore** para logs e eventos da estufa.

Arquitetura inicial sugerida:
1. ESP32 publica telemetria/comandos no MQTT.
2. Serviço ponte consome MQTT e grava no Firestore.
3. Módulo de câmera envia imagens para Storage quando implantado.