# Configuração da câmera OV5640 no ESP32-S3

## Por que aparece `no module named 'camera'`?

A mensagem:

```txt
[CAMERA][WARN] Modulo 'camera' indisponivel neste firmware: no module named 'camera'
```

não é erro do Flutter, MQTT ou Firebase. Ela indica que o firmware MicroPython gravado no ESP32-S3 não possui um driver nativo de câmera exposto como módulo Python (`camera`). O arquivo `captura_de_imagem.py` já está integrado ao restante do projeto, mas ele precisa que o firmware do microcontrolador contenha o driver da câmera.

O driver usado pela maioria das soluções ESP32 é o `esp32-camera`, que suporta ESP32-S3 e sensores como OV2640, OV3660 e OV5640. Esse driver é nativo/C e exige PSRAM para resoluções relevantes, especialmente quando há Wi-Fi ativo.

## O que precisa ser feito

A rota recomendada agora é usar o firmware Arduino/C++ em `firmware_arduino/`, que já está preparado para `esp_camera.h`, MQTT, Firebase e Flutter.


### 1. Confirmar que a placa tem PSRAM

A OV5640 gera imagens grandes. Para ESP32-S3 com câmera, use uma placa com PSRAM habilitada, preferencialmente 8 MB ou mais.

No monitor serial/REPL, confirme as informações da sua placa e firmware. O projeto já imprime dados de boot em `main.py`; mantenha atenção à memória livre e ao modelo da placa.

### 2. Gravar um firmware com suporte a câmera

O firmware MicroPython oficial para ESP32-S3 normalmente não inclui o módulo `camera`. Portanto, uma destas abordagens é necessária:

1. **Usar um build MicroPython customizado com `esp32-camera` integrado** e módulo Python chamado `camera`.
2. **Usar um firmware específico do fabricante da placa**, quando houver, que já inclua suporte à câmera.
3. **Compilar seu próprio firmware MicroPython** integrando o driver `esp32-camera` e expondo o módulo `camera`.
4. **Alternativa futura:** migrar a parte de câmera para firmware ESP-IDF/Arduino ou CircuitPython. Isso exigiria adaptação de API e não é a opção assumida pelo código atual.

Após gravar o firmware correto, teste no Thonny/REPL:

```python
import camera
print(dir(camera))
```

Se esse comando falhar com `no module named 'camera'`, a captura ainda não poderá funcionar.

### 3. Configurar `secrets.py`

No ESP32, ajuste a cópia local de `secrets.py.example`:

```python
CAMERA_DRIVER_MODULE = "camera"
CAMERA_FRAME_SIZE = "SVGA"
CAMERA_JPEG_QUALITY = 12
CAMERA_LOCAL_DIR = "/imagens"
CAMERA_UPLOAD_URL = "https://us-central1-SEU_PROJETO.cloudfunctions.net/uploadCameraImage"
CAMERA_UPLOAD_TOKEN = "MESMO_TOKEN_CONFIGURADO_EM_functions/.env"
```

Ajuste também `CAMERA_PINS` conforme o pinout real da sua placa ESP32-S3 + OV5640. O pinout varia muito entre ESP32-S3-EYE, Freenove, Waveshare, XIAO Sense e placas customizadas.

### 4. Teste mínimo antes do app Flutter

Antes de testar pelo botão do app, valide no REPL:

```python
import captura_de_imagem
captura_de_imagem.verificar_suporte_camera()
captura_de_imagem.inicializar_camera_ov5640()
resultado = captura_de_imagem.capturar_e_salvar('/imagens', motivo='teste_repl')
print(resultado)
```

Resultado esperado quando o firmware tem driver e a câmera está conectada corretamente:

```txt
[CAMERA] OV5640 inicializada. frame_size=SVGA qualidade=12
[CAMERA] Imagem capturada: /imagens/ov5640_...
[CAMERA][UPLOAD] status=200 ok=True ...
```

Se aparecer `camera_indisponivel`, revise firmware, pinout, PSRAM e alimentação.


## Diagnóstico quando `esp_camera_fb_get` retorna nulo

Se o monitor serial mostrar `esp_camera_init` bem-sucedido, mas a captura falhar com `esp_camera_fb_get retornou nulo` ou `Falha ao capturar frame apos retentativas`, o problema normalmente está no lado físico/configuração da câmera, não no Flutter/Firebase. Verifique, nesta ordem:

1. Pinout real da placa/câmera: D0-D7, XCLK, PCLK, VSYNC, HREF, SIOD/SDA e SIOC/SCL devem corresponder exatamente ao módulo usado.
2. Alimentação: a OV5640 deve ter 3V3 estável, GND comum e fios curtos; instabilidade pode permitir `esp_camera_init`, mas impedir frame válido.
3. PSRAM habilitada na Arduino IDE e frame size inicial moderado (`FRAMESIZE_QVGA` ou `FRAMESIZE_VGA`).
4. Frequência XCLK: se persistir, teste `CAMERA_XCLK_FREQ_HZ 10000000` em `firmware_arduino/config.h`.
5. Modo de captura: use `#define CAMERA_GRAB_MODE CAMERA_GRAB_WHEN_EMPTY`; se ainda falhar, teste também `#define CAMERA_USE_PSRAM_FRAMEBUFFER false` com `FRAMESIZE_QVGA`.
6. Cabo/conector flat e orientação do módulo, especialmente em placas ESP32-S3 com câmera separada.

O firmware Arduino registra o PID do sensor, pinout configurado, parâmetros de câmera, tentativas de captura e reinicialização do driver para facilitar essa análise.


## Reinicialização após captura/upload

Se a captura mostra `Captura OK`, mas o ESP32-S3 reinicia durante ou logo após o upload HTTPS, mantenha `CAMERA_COPY_FRAME_BEFORE_UPLOAD true` e `CAMERA_DEINIT_BEFORE_UPLOAD true`. Com isso, o firmware copia o JPEG para um buffer próprio, libera o framebuffer da câmera e desinicializa o driver antes da conexão TLS/HTTP, reduzindo consumo e disputa de memória entre câmera, Wi-Fi e TLS.

O firmware também prioriza buffer interno para imagens pequenas com `CAMERA_UPLOAD_BUFFER_INTERNAL_MAX_BYTES`, aguarda `CAMERA_PRE_UPLOAD_SETTLE_MS` antes do HTTPS e `CAMERA_POST_UPLOAD_SETTLE_MS` depois do fechamento da conexão. Esses intervalos ajudam quando o reset ocorre por pico de consumo ou instabilidade entre câmera, PSRAM, Wi-Fi e TLS. O boot imprime checkpoints com `Serial.flush()` (`[BOOT] ...`) para mostrar exatamente em qual etapa a inicialização parou. Por padrão, `CAMERA_DIAGNOSTICS_USE_NVS` fica `false` para evitar acesso a `Preferences`/NVS logo no boot; se você precisar recuperar o último estágio persistido (`frame_copied`, `camera_deinit_before_upload`, `http_post_start`, `http_post_done`, `upload_success` etc.), altere temporariamente esse macro para `true` no `config.h`. Se ainda reiniciar, verifique alimentação: câmera + Wi-Fi ativo podem causar queda momentânea de tensão mesmo quando a captura isolada funciona.

## Boas práticas elétricas

- Não alimente câmera, relés, bomba, lâmpada ou ventoinha a partir do 3V3 do ESP32 se a corrente total exceder a capacidade da placa.
- A câmera OV5640 pode aquecer; teste inicialmente em resoluções moderadas.
- Use fios curtos para sinais paralelos da câmera (D0-D7, PCLK, VSYNC, HREF, XCLK), evitando ruído.
- Garanta GND comum entre ESP32 e módulos externos.
- Para atuadores indutivos, use driver adequado, diodo de flyback e fonte separada quando necessário.

## Como saber se o problema está resolvido

O problema do firmware estará resolvido quando:

1. `import camera` funcionar no Thonny.
2. `captura_de_imagem.inicializar_camera_ov5640()` imprimir inicialização bem-sucedida.
3. `captura_de_imagem.capturar_e_salvar(...)` salvar um arquivo `.jpg` localmente.
4. Com `CAMERA_UPLOAD_URL` e `CAMERA_UPLOAD_TOKEN` corretos, a imagem aparecer no Firebase Storage em `devices/<deviceId>/images/` e no app Flutter na tela da câmera.


## Upload HTTPS em chunks

Se o monitor serial mostrar `Reset reason=4(PANIC)` com último estágio `http_post_start` quando `CAMERA_DIAGNOSTICS_USE_NVS true` estiver ativo, mantenha `CAMERA_UPLOAD_USE_HTTPCLIENT false` no `config.h`. Esse modo envia o JPEG por `WiFiClientSecure` em chunks (`CAMERA_UPLOAD_CHUNK_BYTES`, padrão 1024 bytes), reduzindo pressão de heap durante o POST para a Cloud Function.


## Observação sobre build incremental da Arduino IDE

A implementação da câmera fica em `camera_runtime.cpp`; não há mais `camera_manager.cpp` no sketch, justamente para impedir que a IDE gere/linke `camera_manager.cpp.o` antigo. Se o linker ainda mencionar `camera_manager.cpp.o` com funções de clima, apague a pasta temporária do sketch em `C:\Users\<usuario>\AppData\Local\arduino\sketches\...` ou use uma compilação limpa, pois esse sintoma indica objeto incremental antigo da IDE.
