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
5. Para melhorar a nitidez, use resolução/qualidade mais altas quando houver PSRAM (`XGA`, `JPEG_QUALITY=8`, lembrando que no `esp32-camera` valores menores de qualidade significam menos compressão). Se houver instabilidade, reduza temporariamente para `SVGA`/`JPEG_QUALITY=10-12`.

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
- `firmware_arduino/camera_manager.h` + `firmware_arduino/camera_runtime.cpp` — OV5640, agenda e upload para Firebase.
- `firmware_arduino/climate_manager.*` — LDR em GPIO1, HDC1080 no I2C0 (SDA 14/SCL 21), automação da lâmpada LED em GPIO48 e ventoinha em GPIO44 e envio de histórico para Firestore.
- `firmware_arduino/irrigation_manager.*` — sensor de umidade do solo em GPIO41, bomba em GPIO47, leitura inicial a cada 15 s, automação por limiar de umidade e timeout de segurança de 15 s.
- `firmware_arduino/predator_manager.*` — PIR HC-SR501 em GPIO42, buzzer PWM em GPIO3 a 5 kHz/10 bits, alerta local e histórico em Firestore.
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



> Se o ESP32-S3 reiniciar com `IllegalInstruction` ao entrar em `[BOOT] Inicializando MQTT...`, mantenha `MQTT_BOOT_SAFE_MODE 1` e `MQTT_CONNECT_ON_BOOT 0` no `firmware_arduino/config.h`. Nessa condição o firmware não chama `PubSubClient.connect()` no boot, permitindo validar sensores/atuadores locais antes de reativar a comunicação MQTT. Para retomar os comandos remotos, teste depois `MQTT_BOOT_SAFE_MODE 0`, confirme broker/porta/credenciais e mantenha `MQTT_USE_PUBSUBCLIENT 1`.

### Módulos Clima, Rega e Predadores no Flutter

O módulo Clima lê `devices/{deviceId}/climate` para exibir histórico de temperatura, umidade, luminosidade, lâmpada LED e ventoinha. O módulo Rega lê `devices/{deviceId}/irrigation` para exibir as últimas leituras do solo e eventos da bomba. O módulo Predadores lê `devices/{deviceId}/predators` para exibir histórico de presença, alarme e buzzer. Os botões manuais gravam comandos `iluminar`, `ventilar` e `irrigar` em `devices/{deviceId}/commands`, enquanto as configurações da ventoinha/lâmpada gravam `configurar_clima` e a configuração da rega grava `configurar_rega`; a Function `dispatchCommandToMqtt` publica tudo no MQTT para o ESP32-S3. O firmware liga a lâmpada automaticamente quando o limiar de LDR é atingido (`LDR_DARK_THRESHOLD_RAW`, agora mais sensível por padrão em 1800 raw), permite ajustar limiar/histerese pelo Flutter, aciona a ventoinha quando a temperatura supera o limiar configurado e registra os eventos via `ingestClimateReading`. Na Rega, o ESP32 aciona a bomba quando a umidade do solo fica abaixo de `SOIL_MIN_MOISTURE_PERCENT`, sempre com timeout `IRRIGATION_PUMP_TIMEOUT_MS`, e grava o histórico via `ingestIrrigationReading`. Em Predadores, o PIR dispara alerta informativo com buzzer PWM e grava registros via `ingestPredatorAlert`; o Flutter envia `configurar_predadores`, `silenciar_predadores` e `testar_buzzer`.

A AppBar do dashboard consome `devices/{deviceId}/status/current` para mostrar um indicador Online/Offline e abrir a tela `/device`, onde são exibidos ID, namespace, firmware, MAC, IP, SSID, RSSI, uptime, heap e PSRAM livres. O firmware Arduino publica essas informações no tópico MQTT `estufa/{namespace}/{deviceId}/status`; mantenha o bridge/Function `ingestMqttEvent` recebendo eventos `kind=status` para espelhar esse payload no Firestore.

> Se o ESP32-S3 reiniciar com `assert failed: spinlock_release` ao entrar em `[BOOT] Inicializando clima...`, mantenha `CLIMATE_HDC1080_ENABLED 0` no `firmware_arduino/config.h` para desabilitar temporariamente o barramento I2C do HDC1080. Assim LDR, lâmpada e demais módulos continuam operando; depois valide SDA=GPIO14, SCL=GPIO21, alimentação e pull-ups antes de reativar o HDC1080.

### Armazenamento local em SD Card

O firmware Arduino inicializa o leitor microSD em SDMMC 1-bit com `PIN_SD_CLK` GPIO39, `PIN_SD_CMD` GPIO38, `PIN_SD_D0` GPIO40 e `SDMMC_FREQUENCY_KHZ` 25000 quando `SD_CARD_ENABLED` está ativo. As leituras principais são registradas em NDJSON dentro de `/logs`, e cada captura da câmera é salva em `/imagens` antes da tentativa de upload. Quando não houver internet ou o upload falhar, imagens entram em `/fila/imagens_pendentes.ndjson` e leituras/eventos JSON entram em `/fila/registros_pendentes.ndjson`; o loop chama `processPendingCameraUploads()` e `processPendingSdJsonUploads()` periodicamente e remove da fila somente itens enviados com sucesso, evitando reenvio duplicado de imagens pelo mesmo nome de arquivo.

> Se o ESP32-S3 reiniciar com `Interrupt wdt timeout` ou `PANIC` ao entrar em `[BOOT] Inicializando SD Card...`, mantenha `SD_BOOT_SAFE_MODE 1` e `SD_CARD_ENABLED 0` no `firmware_arduino/config.h`. Nessa condição o sketch nem chama `setupSdManager()`, isolando totalmente o SD durante os testes dos demais módulos. Depois valide alimentação 3V3, GND comum, cartão formatado em FAT32 e sinais CLK=GPIO39, CMD=GPIO38, D0=GPIO40 antes de reativar; para bring-up, teste `SD_BOOT_SAFE_MODE 0`, `SD_CARD_ENABLED 1`, `SD_MOUNT_ON_BOOT 1` e `SDMMC_FREQUENCY_KHZ 400`, retornando a 25000 somente após a montagem ficar estável.


> Se o ESP32-S3 reiniciar com `assert failed: spinlock_release` ao entrar em `[BOOT] Inicializando camera...`, mantenha `CAMERA_BOOT_SAFE_MODE 1` e `CAMERA_INIT_ON_BOOT 0` no `firmware_arduino/config.h`. Nessa condição o sketch não chama `esp_camera_init()` no boot nem dispara capturas automáticas, permitindo validar Wi-Fi, MQTT, clima, rega e predadores. Para reativar a câmera, teste primeiro com `CAMERA_BOOT_SAFE_MODE 0` e `CAMERA_INIT_ON_BOOT 1`; se o erro voltar, valide o pinout OV5640, alimentação, PSRAM e possíveis conflitos de LEDC/PWM com o buzzer.

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