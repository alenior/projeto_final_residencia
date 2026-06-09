# Estufa IoT - Firmware ESP32 + Firebase + Flutter

Projeto de prototipação da Estufa IoT com firmware MicroPython no ESP32-S3, integração em nuvem via MQTT + Firebase e dashboard Flutter para monitoramento/controle.

## Componentes
- Firmware MicroPython de referência (`main.py`, `wifi.py`, `sincronizar_horario.py`, `envio_e_recebimento_nuvem.py`, `captura_de_imagem.py`)
- Firmware Arduino/C++ para ESP32-S3 + OV5640 (`firmware_arduino/`)
- Firebase Cloud Functions (`functions/`) para:
  - ingestão de eventos MQTT no Firestore
  - despacho de comandos Firestore -> MQTT
  - upload HTTP de imagens da OV5640 para Firebase Storage + metadados no Firestore
- Dashboard Flutter (`dashboard_estufa_iot/`) com modelos e repositórios para telemetria, comandos e câmera.

## Fluxos de comunicação
1. **Subida (telemetria/logs):** ESP32 -> MQTT -> Cloud Functions (HTTP ingest/ponte futura) -> Firestore
2. **Descida (comandos):** App Flutter escreve comando em Firestore -> Cloud Function publica MQTT -> ESP32 consome
3. **Câmera:** ESP32 captura OV5640 -> Cloud Function `uploadCameraImage` -> Firebase Storage (`devices/<deviceId>/images/*`) + Firestore (`devices/<deviceId>/images/*`)

## Tópicos MQTT
- Base firmware: `estufa/embarcatech2026/<deviceId>/...`
- Publicações:
  - `telemetria`
  - `alertas`
  - `status`
  - `teste`
  - `camera`
- Comandos (consumo no firmware):
  - `estufa/embarcatech2026/<deviceId>/comandos`
  - `estufa/<deviceId>/comandos` (compatibilidade legado)
  - `estufa/comandos` (compatibilidade geral)

## Módulo da câmera OV5640

### Funcionalidades previstas nesta etapa
- Captura manual via comando do dashboard Flutter (`capturar`).
- Captura automática inicialmente configurada para 12h00, uma vez ao dia.
- Alteração de horário, periodicidade e habilitação via app (`configurar_camera`).
- Salvamento local no ESP32 em `/imagens` e tentativa de upload para Firebase via Function HTTP.
- Histórico pesquisável pelo app via Storage e metadados em Firestore.

### Configuração no ESP32
> Importante: se o serial mostrar `no module named 'camera'`, o firmware MicroPython gravado no ESP32-S3 não contém driver de câmera. Consulte `CAMERA_SETUP.md` antes de testar pelo app Flutter.

1. Copie `secrets.py.example` para `secrets.py` no dispositivo.
2. Ajuste Wi-Fi, `MQTT_DEVICE_ID`, `MQTT_NAMESPACE` e broker MQTT.
3. Configure `CAMERA_PINS` de acordo com a placa ESP32-S3 + OV5640 usada.
4. Após o deploy das Functions, preencha:
   - `CAMERA_UPLOAD_URL="https://us-central1-<PROJECT_ID>.cloudfunctions.net/uploadCameraImage"`
   - `CAMERA_UPLOAD_TOKEN="<mesmo token de functions/.env>"`
5. Mantenha resolução/qualidade moderadas (`SVGA`, `JPEG_QUALITY=12`) para reduzir uso de RAM durante base64/upload.

### Comandos de câmera esperados no MQTT
Captura imediata:
```json
{
  "comando": "capturar",
  "status": true,
  "origem": "flutter_camera_card"
}
```

Configuração de agenda:
```json
{
  "comando": "configurar_camera",
  "status": true,
  "habilitado": true,
  "hora": 12,
  "minuto": 0,
  "periodicidade_horas": 24,
  "origem": "flutter_camera_card"
}
```


## Firmware Arduino/C++ para câmera

A câmera OV5640 deve ser testada preferencialmente pelo firmware em `firmware_arduino/`, que usa o core ESP32 da Espressif e `esp_camera.h`. Os arquivos MicroPython permanecem como referência, mas o firmware MicroPython oficial geralmente não inclui o módulo `camera`.

Arquivos principais:
- `firmware_arduino/firmware_arduino.ino` — entrada principal.
- `firmware_arduino/config.example.h` — copie para `firmware_arduino/config.h` e preencha Wi-Fi, MQTT, Firebase e pinout da câmera.
- `firmware_arduino/wifi_manager.*` — Wi-Fi.
- `firmware_arduino/time_manager.*` — NTP/hora local.
- `firmware_arduino/mqtt_manager.*` — MQTT e comandos.
- `firmware_arduino/camera_manager.*` — OV5640, agenda e upload para Firebase.
- `firmware_arduino/climate_manager.*` — LDR em GPIO1, HDC1080 no I2C0 (SDA 14/SCL 21), automação da lâmpada LED em GPIO48 e ventoinha em GPIO44 e envio de histórico para Firestore.
- `firmware_arduino/actuators.*` — bomba, lâmpada LED, leituras básicas e compatibilidade com atuadores opcionais.

Consulte `firmware_arduino/README.md` antes do upload pela Arduino IDE. Se a compilação indicar `PubSubClient.h: No such file or directory`, instale `PubSubClient` pelo Library Manager e defina `MQTT_USE_PUBSUBCLIENT 1` no `config.h`; sem ela o firmware compila em modo degradado, mas não recebe comandos MQTT do Flutter.

## Deploy Cloud Functions
### Pré-requisitos
- Node.js 22+
- Firebase CLI (`npm i -g firebase-tools`)
- Projeto Firebase criado (Firestore e Storage habilitados)

### Passos
1. Login:
   - `firebase login`
2. Selecionar projeto:
   - `firebase use --add`
3. Entrar em `functions/` e instalar dependências:
   - `cd functions`
   - `npm install`
4. Configurar variáveis de ambiente locais (opcional para emulação):
   - copiar `functions/.env.example` para `functions/.env`
   - definir `CAMERA_UPLOAD_TOKEN` com um valor forte e igual ao `secrets.py`/`config.h` do ESP32; o módulo clima reaproveita esse token por padrão
5. Deploy:
   - `firebase deploy --only functions`

## Uso de comandos via Firestore
Grave um documento em:
`devices/<deviceId>/commands/<commandId>`

Exemplo de payload:
```json
{
  "comando": "irrigar",
  "status": true,
  "namespace": "embarcatech2026",
  "origem": "flutter_app"
}
```

A função `dispatchCommandToMqtt` publicará no tópico MQTT do dispositivo e marcará o documento com `dispatched=true`.

## Dashboard Flutter
Dependências usadas pelos repositórios/modelos do dashboard:
- `firebase_core`
- `firebase_auth`
- `cloud_firestore`
- `firebase_storage`
- `flutter_riverpod`
- `uuid`

Para gerar `firebase_options.dart`, entre em `dashboard_estufa_iot/` e execute `flutterfire configure` após instalar a Firebase CLI oficial e o FlutterFire CLI. O arquivo é específico do projeto Firebase/local e está no `.gitignore`.


### Visualização de imagens no Flutter

### Módulo Clima no Flutter

O módulo Clima lê `devices/{deviceId}/climate` para exibir histórico de temperatura, umidade, luminosidade, lâmpada LED e ventoinha. Os botões manuais gravam comandos `iluminar` e `ventilar` em `devices/{deviceId}/commands`, enquanto a configuração da ventoinha grava `configurar_clima`; a Function `dispatchCommandToMqtt` publica tudo no MQTT para o ESP32-S3. O firmware liga a lâmpada automaticamente quando `LDR_DARK_THRESHOLD_RAW` é atingido, aciona a ventoinha quando a temperatura supera o limiar configurado e registra os eventos via `ingestClimateReading`.

### Visualização de imagens no Flutter

A tela de câmera tenta carregar a prévia usando bytes obtidos pelo SDK do Firebase Storage e, se necessário, usa a Function pública `getCameraImage` como proxy de leitura. Isso evita falhas comuns no Flutter Web/Chrome em que uma URL HTTPS do Storage aparece no app como `statusCode: 0` por configuração de CORS/token. Após essa alteração, faça deploy também de `getCameraImage` com `firebase deploy --only functions`.

## Segurança e boas práticas
- Não versionar segredos (`secrets.py`, `.env`, chaves de serviço, `firebase_options.dart`).
- Usar token de upload da câmera (`CAMERA_UPLOAD_TOKEN`) enquanto o protótipo não tiver autenticação mútua mais forte.
- Migrar para broker MQTT autenticado/TLS em produção.
- Alimentar a OV5640 e atuadores com fonte adequada; não alimentar relés, bomba, ventoinha ou iluminação diretamente pelo 3V3 do ESP32.
- Usar transistores/MOSFETs, diodos de flyback e fontes separadas/aterramento comum conforme a carga de cada atuador.


### HTTP 403 no upload da câmera

Se o ESP32 mostrar `HTTP 403` com HTML `Forbidden` ao chamar `uploadCameraImage`, a requisição provavelmente foi bloqueada antes de entrar no código da Function por permissão de invocação do Cloud Run/Functions v2. As Functions HTTP deste projeto usam `invoker: 'public'`; após alterar esse ponto, faça novo deploy das Functions. O token `CAMERA_UPLOAD_TOKEN` continua sendo validado dentro da Function para rejeitar uploads não autorizados.

### Observação sobre deploy das Functions

O projeto versiona `firebase.json` com `source=functions`, `codebase=default` e runtime `nodejs22` para evitar que o Firebase CLI use configurações locais antigas ou inconsistentes. Também foi versionado `functions/functions.yaml`, gerado pelo binário `firebase-functions`, para evitar timeout na etapa de descoberta HTTP (`Cannot determine backend specification. Timeout after 10000`) em máquinas mais lentas. Sempre que adicionar/remover/renomear Functions, rode `cd functions && npm run manifest` antes do deploy.

Antes de reenviar as Functions, atualize as dependências no diretório `functions/`:

```bash
cd functions
npm install
cd ..
firebase deploy --only functions --debug
```

Se o deploy falhar apenas com mensagens genéricas como `Failed to update function`, execute com `--debug` e verifique no log detalhado se há erro de permissão, build ou runtime.