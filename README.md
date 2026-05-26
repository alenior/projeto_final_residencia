# Estufa IoT - Firmware ESP32 + Firebase

Projeto de prototipação da Estufa IoT com firmware MicroPython no ESP32 e integração em nuvem via MQTT + Firebase.

## Componentes
- Firmware (`main.py`, `wifi.py`, `sincronizar_horario.py`, `envio_e_recebimento_nuvem.py`)
- Firebase Cloud Functions (`functions/`) para:
  - ingestão de eventos MQTT no Firestore
  - despacho de comandos Firestore -> MQTT

## Fluxos de comunicação
1. **Subida (telemetria/logs):** ESP32 -> MQTT -> Cloud Functions (HTTP ingest) -> Firestore
2. **Descida (comandos):** App Flutter escreve comando em Firestore -> Cloud Function publica MQTT -> ESP32 consome

## Tópicos MQTT
- Base firmware: `estufa/embarcatech2026/<deviceId>/...`
- Publicações:
  - `telemetria`
  - `alertas`
  - `status`
  - `teste`
- Comandos (consumo no firmware):
  - `estufa/embarcatech2026/<deviceId>/comandos`
  - `estufa/comandos` (compatibilidade legado)

## Deploy Cloud Functions
### Pré-requisitos
- Node.js 20+
- Firebase CLI (`npm i -g firebase-tools`)
- Projeto Firebase criado (Firestore habilitado)

### Passos
1. Login:
   - `firebase login`
2. Selecionar projeto:
   - `firebase use --add`
3. Entrar em `functions/` e instalar dependências:
   - `cd functions`
   - `npm install`
4. Configurar variáveis de ambiente locais (opcional para emulação):
   - copiar `functions/.env.example`
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

## Segurança
- Não versionar segredos (`secrets.py`, `.env`, chaves de serviço)
- Migrar para broker autenticado/TLS em produção