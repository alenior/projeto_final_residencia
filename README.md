# Estufa IoT - Firmware ESP32 + Firebase + Flutter

Projeto de prototipação da Estufa IoT com firmware MicroPython no ESP32-S3, integração em nuvem via MQTT + Firebase e dashboard Flutter para monitoramento/controle.

## Componentes
- Firmware (`main.py`, `wifi.py`, `sincronizar_horario.py`, `envio_e_recebimento_nuvem.py`, `captura_de_imagem.py`)
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

## Deploy Cloud Functions
### Pré-requisitos
- Node.js 20+
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
   - definir `CAMERA_UPLOAD_TOKEN` com um valor forte e igual ao `secrets.py` do ESP32
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

## Segurança e boas práticas
- Não versionar segredos (`secrets.py`, `.env`, chaves de serviço, `firebase_options.dart`).
- Usar token de upload da câmera (`CAMERA_UPLOAD_TOKEN`) enquanto o protótipo não tiver autenticação mútua mais forte.
- Migrar para broker MQTT autenticado/TLS em produção.
- Alimentar a OV5640 e atuadores com fonte adequada; não alimentar relés, bomba, ventoinha ou iluminação diretamente pelo 3V3 do ESP32.
- Usar transistores/MOSFETs, diodos de flyback e fontes separadas/aterramento comum conforme a carga de cada atuador.