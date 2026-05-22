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


### 7) Secrets e segurança de credenciais
1. Copie `secrets.py.example` para `secrets.py`.
2. Preencha Wi-Fi e MQTT no `secrets.py`.
3. O arquivo `secrets.py` está no `.gitignore` e não será versionado.
4. Se você já expôs senha em commit/log/chat, rotacione imediatamente no HiveMQ Cloud.

Exemplo de erro `Falha ao inicializar cliente: 5`:
- Geralmente indica problema de autenticação/ACL no broker (usuário/senha inválidos ou sem permissão no tópico).
- Confirme usuário/senha em **Access Management** e teste no cliente web do HiveMQ com os mesmos dados.


### 8) HiveMQ Cloud não conecta com username/password
Se o cliente web do HiveMQ não conecta com `esp32s3-estufa-001` / `Naodigo2026`, normalmente o problema é de credencial/ACL:

1. Em **Access Management > Credentials**, confirme se existe exatamente esse usuário.
2. Se não existir, crie um novo usuário (ex.: `estufa-app`) e senha forte, depois atualize `secrets.py`.
3. Em **Access Management > Permissions/ACL**, permita tópicos `estufa/#` para esse usuário.
4. No cliente web, use:
   - Host: `<seu-cluster>.s1.eu.hivemq.cloud`
   - Port: `8883`
   - TLS/SSL: habilitado
   - Username/Password: o usuário de **Credentials** (não o `device_id`).
5. Se falhar, rotacione senha e teste novamente.

> Dica: `device_id` identifica o microcontrolador nos tópicos; não é obrigatoriamente o mesmo valor do usuário MQTT.