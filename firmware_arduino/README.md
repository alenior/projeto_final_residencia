# Firmware Arduino/C++ - Estufa IoT

Este diretório contém a migração do firmware para Arduino/C++ para viabilizar a câmera OV5640 com `esp32-camera`.

## Caminhos completos no repositório

- `/workspace/projeto_final_residencia/firmware_arduino/firmware_arduino.ino` — entrada principal, equivalente ao `main.py`.
- `/workspace/projeto_final_residencia/firmware_arduino/config.example.h` — exemplo de configuração, equivalente ao `secrets.py.example`.
- `/workspace/projeto_final_residencia/firmware_arduino/config.h` — configuração local real, não versionada.
- `/workspace/projeto_final_residencia/firmware_arduino/wifi_manager.*` — equivalente ao `wifi.py`.
- `/workspace/projeto_final_residencia/firmware_arduino/time_manager.*` — equivalente ao `sincronizar_horario.py`.
- `/workspace/projeto_final_residencia/firmware_arduino/mqtt_manager.*` — equivalente ao `envio_e_recebimento_nuvem.py`.
- `/workspace/projeto_final_residencia/firmware_arduino/camera_manager.h` + `/workspace/projeto_final_residencia/firmware_arduino/camera_runtime.cpp` — equivalente ao `captura_de_imagem.py`.
- `/workspace/projeto_final_residencia/firmware_arduino/climate_manager.*` — módulo clima com LDR, HDC1080, automação da lâmpada LED e envio ao Firebase.
- `/workspace/projeto_final_residencia/firmware_arduino/actuators.*` — GPIOs de bomba, lâmpada LED, PIR e leituras ADC.

## Dependências Arduino IDE

Instale pelo Library Manager:

- `PubSubClient`
- `ArduinoJson`

Se aparecer `fatal error: PubSubClient.h: No such file or directory`, instale a biblioteca `PubSubClient` pelo Library Manager da Arduino IDE. O firmware também pode compilar sem essa biblioteca quando `MQTT_USE_PUBSUBCLIENT` estiver `0` ou ausente no `config.h`, permitindo validar Wi-Fi/clima/câmera local; nesse modo MQTT, comandos do Flutter e publicações de telemetria ficam desabilitados. Para a integração completa do app, instale `PubSubClient` e defina `#define MQTT_USE_PUBSUBCLIENT 1` no `firmware_arduino/config.h`.

Também é necessário instalar o pacote de placas ESP32 da Espressif na Arduino IDE. A câmera usa `esp_camera.h`, fornecido pelo core ESP32 quando uma placa ESP32 com suporte a câmera é selecionada. Se a IDE compilar com aviso de `esp_camera.h` ausente, o firmware agora desabilita apenas o módulo de câmera para permitir testar Wi-Fi/MQTT/clima; para capturar OV5640, selecione um pacote/placa ESP32-S3 com `esp32-camera` disponível e PSRAM habilitada.

## Configuração inicial

1. Copie:

   ```txt
   firmware_arduino/config.example.h
   ```

   para:

   ```txt
   firmware_arduino/config.h
   ```

2. Ajuste Wi-Fi, MQTT, `CAMERA_UPLOAD_URL`, `CLIMATE_INGEST_URL` e `CAMERA_UPLOAD_TOKEN`. Para receber comandos do Flutter, instale `PubSubClient` e mantenha `MQTT_USE_PUBSUBCLIENT 1`.
3. Substitua todos os `CAMERA_PIN_*` pelo pinout real da sua placa ESP32-S3 + OV5640 e confirme o HDC1080 em SDA=GPIO14/SCL=GPIO21/endereço `0x40`.
4. Selecione na Arduino IDE uma placa ESP32-S3 com PSRAM habilitada.
5. Faça upload e acompanhe o Serial Monitor em `115200`.

## Integração mantida

O firmware Arduino mantém os mesmos tópicos e comandos usados pelo Flutter/Firebase:

- Comandos: `estufa/embarcatech2026/<deviceId>/comandos`
- Telemetria: `estufa/embarcatech2026/<deviceId>/telemetria`
- Status: `estufa/embarcatech2026/<deviceId>/status`
- Eventos de câmera: `estufa/embarcatech2026/<deviceId>/camera`

Comandos esperados:

```json
{"comando":"capturar","status":true}
{"comando":"configurar_camera","habilitado":true,"hora":12,"minuto":0,"periodicidade_horas":24}
{"comando":"irrigar","status":true}
{"comando":"iluminar","status":true}
{"comando":"aquecer","status":true}
{"comando":"ventilar","status":true}
```

## Módulo clima

O LDR é lido em `PIN_LDR_ADC` (GPIO1, ADC de 12 bits, 0-4095). O HDC1080 usa I2C0 com `HDC1080_SDA_PIN 14`, `HDC1080_SCL_PIN 21`, frequência de 100 kHz e endereço `0x40`. A lâmpada LED usa `PIN_RELE_LAMPADA 48` e a ventoinha usa `PIN_VENTOINHA 44`. O upload da câmera usa envio HTTPS em chunks por padrão (`CAMERA_UPLOAD_USE_HTTPCLIENT false`) para evitar PANIC durante `HTTPClient.POST`; se precisar voltar ao caminho antigo, defina `CAMERA_UPLOAD_USE_HTTPCLIENT true`. A cada `CLIMATE_INTERVAL_MS` o firmware envia uma leitura para `CLIMATE_INGEST_URL` e liga automaticamente a lâmpada se `ldr_raw <= LDR_DARK_THRESHOLD_RAW`. A ventoinha é verificada a cada `CLIMATE_FAN_CHECK_INTERVAL_MS`, liga quando a temperatura atinge `CLIMATE_FAN_TEMP_THRESHOLD_C` (35 °C por padrão) e desliga pelo timeout de segurança `CLIMATE_FAN_TIMEOUT_MS` (30 s por padrão). Os comandos MQTT/Flutter `iluminar`, `ventilar` e `configurar_clima` permitem acionar iluminação, acionar ventoinha e ajustar limiar/periodicidade da ventoinha.

## Botão local de teste da câmera

O firmware também pode usar um botão momentâneo em `PIN_BOTAO_CAMERA` para validar captura e upload sem depender do Flutter/MQTT. O padrão é GPIO45 com `INPUT_PULLUP`: ligue um lado do botão ao GPIO45 e o outro ao GND. Como GPIO45 é pino de strapping no ESP32-S3, não mantenha o botão pressionado durante boot/reset. Ao pressionar depois do boot, o Serial Monitor deve exibir `[BOTAO_CAMERA] Captura manual local solicitada.` e seguir com `[CAMERA] Captura OK` / `[CAMERA][UPLOAD] HTTP ...`.

## Observações importantes

- Os arquivos MicroPython permanecem no repositório como referência/protótipo, mas a câmera OV5640 deve ser testada por este firmware Arduino.
- O upload atual envia o JPEG como corpo binário (`image/jpeg`) para a Function `uploadCameraImage`, evitando o aumento de memória causado por base64. A Function também mantém compatibilidade com JSON/base64 para testes manuais.
- Se houver reset após `Captura OK`, mantenha `CAMERA_COPY_FRAME_BEFORE_UPLOAD` e `CAMERA_DEINIT_BEFORE_UPLOAD` ativos, reduza a resolução se necessário e observe no boot os campos `Reset reason=...` e `[CAMERA][LAST] stage=...`. Os parâmetros `CAMERA_UPLOAD_BUFFER_INTERNAL_MAX_BYTES`, `CAMERA_PRE_UPLOAD_SETTLE_MS` e `CAMERA_POST_UPLOAD_SETTLE_MS` controlam a cópia em RAM interna e as pausas antes/depois do HTTPS para reduzir instabilidade por pico de consumo/memória.
- Para produção, substitua `WiFiClientSecure::setInsecure()` por validação de certificado CA.
