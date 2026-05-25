# Firebase Bridge (MQTT -> Firestore)

Este serviço recebe mensagens MQTT publicadas pelo firmware e grava no Firestore.

## Setup
1. `cd firebase_bridge`
2. `npm install`
3. Copie `.env.example` para `.env` e ajuste valores.
4. Baixe a chave de serviço do Firebase (Admin SDK) e salve como `serviceAccountKey.json`.
5. Exporte variáveis (`source .env`) e execute `npm start`.

## Mapeamento de coleções
- `estufa/<deviceId>/status` -> `devices/{deviceId}/status/current`
- `estufa/<deviceId>/telemetria` -> `devices/{deviceId}/telemetry/*`
- `estufa/<deviceId>/alertas` -> `devices/{deviceId}/alerts/*`
- `estufa/<deviceId>/teste` -> `devices/{deviceId}/actions/*`

## Storage (imagens)
Quando o módulo de câmera estiver pronto, recomenda-se enviar metadados da imagem por MQTT para uma coleção `images` e o upload binário via endpoint HTTP autenticado (Cloud Run/Function) para o Firebase Storage.
