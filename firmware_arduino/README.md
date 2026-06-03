# Firmware Arduino/C++ - Estufa IoT

Este diretório contém a migração do firmware para Arduino/C++ para viabilizar a câmera OV5640 com `esp32-camera`.

## Caminhos completos no repositório

- `/workspace/projeto_final_residencia/firmware_arduino/firmware_arduino.ino` — entrada principal, equivalente ao `main.py`.
- `/workspace/projeto_final_residencia/firmware_arduino/config.example.h` — exemplo de configuração, equivalente ao `secrets.py.example`.
- `/workspace/projeto_final_residencia/firmware_arduino/config.h` — configuração local real, não versionada.
- `/workspace/projeto_final_residencia/firmware_arduino/wifi_manager.*` — equivalente ao `wifi.py`.
- `/workspace/projeto_final_residencia/firmware_arduino/time_manager.*` — equivalente ao `sincronizar_horario.py`.
- `/workspace/projeto_final_residencia/firmware_arduino/mqtt_manager.*` — equivalente ao `envio_e_recebimento_nuvem.py`.
- `/workspace/projeto_final_residencia/firmware_arduino/camera_manager.*` — equivalente ao `captura_de_imagem.py`.
- `/workspace/projeto_final_residencia/firmware_arduino/actuators.*` — GPIOs de bomba, lâmpada, ventoinha, PIR e leituras ADC.

## Dependências Arduino IDE

Instale pelo Library Manager:

- `PubSubClient`
- `ArduinoJson`

Também é necessário instalar o pacote de placas ESP32 da Espressif na Arduino IDE. A câmera usa `esp_camera.h`, fornecido pelo core ESP32.

## Configuração inicial

1. Copie:

   ```txt
   firmware_arduino/config.example.h
   ```

   para:

   ```txt
   firmware_arduino/config.h
   ```

2. Ajuste Wi-Fi, MQTT, `CAMERA_UPLOAD_URL` e `CAMERA_UPLOAD_TOKEN`.
3. Substitua todos os `CAMERA_PIN_*` pelo pinout real da sua placa ESP32-S3 + OV5640.
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
{"comando":"aquecer","status":true}
{"comando":"ventilar","status":true}
```

## Observações importantes

- Os arquivos MicroPython permanecem no repositório como referência/protótipo, mas a câmera OV5640 deve ser testada por este firmware Arduino.
- O upload atual envia o JPEG como corpo binário (`image/jpeg`) para a Function `uploadCameraImage`, evitando o aumento de memória causado por base64. A Function também mantém compatibilidade com JSON/base64 para testes manuais.
- Para produção, substitua `WiFiClientSecure::setInsecure()` por validação de certificado CA.
