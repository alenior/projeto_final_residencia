const { onRequest } = require('firebase-functions/v2/https');
const { onDocumentCreated } = require('firebase-functions/v2/firestore');
const logger = require('firebase-functions/logger');
const admin = require('firebase-admin');
const mqtt = require('mqtt');

admin.initializeApp();
const db = admin.firestore();

const MQTT_URL = process.env.MQTT_URL || 'mqtt://broker.hivemq.com:1883';
const MQTT_USERNAME = process.env.MQTT_USERNAME || undefined;
const MQTT_PASSWORD = process.env.MQTT_PASSWORD || undefined;
const TOPIC_BASE = process.env.MQTT_TOPIC_BASE || 'estufa/embarcatech2026';

function mqttClient() {
  return mqtt.connect(MQTT_URL, {
    username: MQTT_USERNAME,
    password: MQTT_PASSWORD,
    clean: true,
    reconnectPeriod: 0,
  });
}

exports.health = onRequest(async (req, res) => {
  res.status(200).json({ ok: true, service: 'estufa-functions' });
});

// HTTP ingestion endpoint: can be called by app/backend/automation for historical writes.
exports.ingestMqttEvent = onRequest(async (req, res) => {
  try {
    const body = req.body || {};
    const namespace = body.namespace || 'embarcatech2026';
    const deviceId = body.deviceId;
    const kind = body.kind;
    const payload = body.payload || {};

    if (!deviceId || !kind) {
      return res.status(400).json({ ok: false, error: 'deviceId e kind são obrigatórios' });
    }

    const topic = `estufa/${namespace}/${deviceId}/${kind}`;
    const base = db.collection('devices').doc(deviceId);
    const now = admin.firestore.FieldValue.serverTimestamp();

    if (kind === 'status') {
      await base.collection('status').doc('current').set({ ...payload, topic, namespace, updated_at: now }, { merge: true });
    } else {
      const target = kind === 'telemetria' ? 'telemetry' : kind === 'alertas' ? 'alerts' : kind === 'teste' ? 'actions' : 'events';
      await base.collection(target).add({ ...payload, topic, namespace, received_at: now });
    }

    return res.status(200).json({ ok: true });
  } catch (err) {
    logger.error('ingestMqttEvent error', err);
    return res.status(500).json({ ok: false, error: err.message });
  }
});

// Firestore -> MQTT command bridge
// App escreve comando em: devices/{deviceId}/commands/{commandId}
exports.dispatchCommandToMqtt = onDocumentCreated('devices/{deviceId}/commands/{commandId}', async (event) => {
  const deviceId = event.params.deviceId;
  const data = event.data?.data() || {};
  const namespace = data.namespace || 'embarcatech2026';
  const topic = data.topic || `${TOPIC_BASE}/${deviceId}/comandos`;

  const payload = {
    comando: data.comando,
    status: data.status,
    origem: data.origem || 'firebase',
    ts_ms: Date.now(),
  };

  if (!payload.comando) {
    logger.warn('Comando ignorado: campo comando ausente', { deviceId, data });
    return;
  }

  const client = mqttClient();

  await new Promise((resolve, reject) => {
    client.on('connect', () => {
      client.publish(topic, JSON.stringify(payload), { qos: 0 }, (err) => {
        client.end(true);
        if (err) return reject(err);
        resolve();
      });
    });
    client.on('error', (err) => {
      client.end(true);
      reject(err);
    });
  });

  await event.data.ref.set({
    dispatched: true,
    dispatched_at: admin.firestore.FieldValue.serverTimestamp(),
    dispatched_topic: topic,
    namespace,
  }, { merge: true });

  logger.info('Comando publicado no MQTT', { deviceId, topic, payload });
});
