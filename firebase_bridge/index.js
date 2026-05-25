const admin = require('firebase-admin');
const mqtt = require('mqtt');

const mqttUrl = process.env.MQTT_URL || 'mqtt://broker.hivemq.com:1883';
const mqttUsername = process.env.MQTT_USERNAME || undefined;
const mqttPassword = process.env.MQTT_PASSWORD || undefined;
const topics = (process.env.MQTT_TOPICS || 'estufa/+/telemetria,estufa/+/alertas,estufa/+/status,estufa/+/teste')
  .split(',').map(t => t.trim()).filter(Boolean);
const prefix = process.env.FIRESTORE_COLLECTION_PREFIX || 'devices';

admin.initializeApp();
const db = admin.firestore();

function parseTopic(topic) {
  const p = topic.split('/');
  if (p.length < 3 || p[0] !== 'estufa') return null;
  return { deviceId: p[1], kind: p[2] };
}

async function writeByKind({ deviceId, kind, payload, topic }) {
  const now = admin.firestore.FieldValue.serverTimestamp();
  const base = db.collection(prefix).doc(deviceId);

  if (kind === 'status') {
    await base.collection('status').doc('current').set({ ...payload, topic, updated_at: now }, { merge: true });
    return;
  }

  const target = kind === 'telemetria' ? 'telemetry'
    : kind === 'alertas' ? 'alerts'
      : kind === 'teste' ? 'actions'
        : 'events';

  await base.collection(target).add({ ...payload, topic, received_at: now });
}

const client = mqtt.connect(mqttUrl, { username: mqttUsername, password: mqttPassword, clean: true });

client.on('connect', () => {
  console.log('[BRIDGE] MQTT connected:', mqttUrl);
  topics.forEach((t) => client.subscribe(t, (err) => {
    if (err) console.error('[BRIDGE] subscribe error', t, err.message);
    else console.log('[BRIDGE] subscribed', t);
  }));
});

client.on('message', async (topic, message) => {
  try {
    const parsedTopic = parseTopic(topic);
    if (!parsedTopic) return;

    const raw = message.toString();
    const payload = JSON.parse(raw);
    await writeByKind({ ...parsedTopic, payload, topic });
    console.log('[BRIDGE] persisted', topic);
  } catch (err) {
    console.error('[BRIDGE] message error', err.message);
  }
});

client.on('error', (err) => {
  console.error('[BRIDGE] MQTT error:', err.message);
});
