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
const CAMERA_UPLOAD_TOKEN = process.env.CAMERA_UPLOAD_TOKEN || '';
const DEFAULT_NAMESPACE = 'embarcatech2026';

function mqttClient() {
  return mqtt.connect(MQTT_URL, {
    username: MQTT_USERNAME,
    password: MQTT_PASSWORD,
    clean: true,
    reconnectPeriod: 0,
  });
}

function validateUploadToken(req) {
  if (!CAMERA_UPLOAD_TOKEN) return true;
  const headerToken = req.get('x-camera-upload-token');
  const bodyToken = req.body?.uploadToken;
  return headerToken === CAMERA_UPLOAD_TOKEN || bodyToken === CAMERA_UPLOAD_TOKEN;
}

function safeStorageName(name) {
  return String(name || `ov5640_${Date.now()}.jpg`).replace(/[^a-zA-Z0-9_.-]/g, '_');
}

function copyDefinedFields(source, target, fieldNames) {
  for (const fieldName of fieldNames) {
    if (source[fieldName] !== undefined) {
      target[fieldName] = source[fieldName];
    }
  }
}

exports.health = onRequest(async (req, res) => {
  res.status(200).json({ ok: true, service: 'estufa-functions' });
});

// HTTP ingestion endpoint: can be called by app/backend/automation for historical writes.
exports.ingestMqttEvent = onRequest(async (req, res) => {
  try {
    const body = req.body || {};
    const namespace = body.namespace || DEFAULT_NAMESPACE;
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
      const target = kind === 'telemetria' ? 'telemetry' : kind === 'alertas' ? 'alerts' : kind === 'camera' ? 'images' : kind === 'teste' ? 'actions' : 'events';
      await base.collection(target).add({ ...payload, topic, namespace, received_at: now });
    }

    return res.status(200).json({ ok: true });
  } catch (err) {
    logger.error('ingestMqttEvent error', err);
    return res.status(500).json({ ok: false, error: err.message });
  }
});

// ESP32 -> HTTPS -> Firebase Storage + Firestore metadata.
// Espera JSON: { deviceId, namespace, filename, contentType, imageBase64, reason, capturedAt, metadata }
exports.uploadCameraImage = onRequest({ timeoutSeconds: 60, memory: '512Mi' }, async (req, res) => {
  try {
    if (req.method !== 'POST') {
      return res.status(405).json({ ok: false, error: 'Use POST' });
    }

    if (!validateUploadToken(req)) {
      return res.status(401).json({ ok: false, error: 'Token de upload invalido' });
    }

    const body = req.body || {};
    const deviceId = body.deviceId;
    const namespace = body.namespace || DEFAULT_NAMESPACE;
    const imageBase64 = body.imageBase64;
    const contentType = body.contentType || 'image/jpeg';
    const filename = safeStorageName(body.filename);
    const reason = body.reason || 'manual';
    const metadata = body.metadata || {};

    if (!deviceId || !imageBase64) {
      return res.status(400).json({ ok: false, error: 'deviceId e imageBase64 são obrigatórios' });
    }

    const imageBuffer = Buffer.from(imageBase64, 'base64');
    if (!imageBuffer.length) {
      return res.status(400).json({ ok: false, error: 'imageBase64 vazio ou invalido' });
    }

    const path = `devices/${deviceId}/images/${filename}`;
    const bucket = admin.storage().bucket();
    const file = bucket.file(path);

    await file.save(imageBuffer, {
      resumable: false,
      metadata: {
        contentType,
        metadata: {
          deviceId,
          namespace,
          reason,
          capturedAt: body.capturedAt || '',
        },
      },
    });

    const docRef = await db.collection('devices').doc(deviceId).collection('images').add({
      device_id: deviceId,
      filename,
      path,
      content_type: contentType,
      size_bytes: imageBuffer.length,
      namespace,
      reason,
      metadata,
      captured_at_device: body.capturedAt || null,
      created_at: admin.firestore.FieldValue.serverTimestamp(),
      updated_at: admin.firestore.FieldValue.serverTimestamp(),
      source: 'esp32_ov5640',
    });

    logger.info('Imagem da camera gravada', { deviceId, path, imageId: docRef.id, sizeBytes: imageBuffer.length });
    return res.status(200).json({ ok: true, path, imageId: docRef.id, sizeBytes: imageBuffer.length });
  } catch (err) {
    logger.error('uploadCameraImage error', err);
    return res.status(500).json({ ok: false, error: err.message });
  }
});

// Firestore -> MQTT command bridge
// App escreve comando em: devices/{deviceId}/commands/{commandId}
exports.dispatchCommandToMqtt = onDocumentCreated('devices/{deviceId}/commands/{commandId}', async (event) => {
  const deviceId = event.params.deviceId;
  const data = event.data?.data() || {};
  const namespace = data.namespace || DEFAULT_NAMESPACE;
  const topic = data.topic || `${TOPIC_BASE}/${deviceId}/comandos`;

  const payload = {
    comando: data.comando,
    status: data.status,
    origem: data.origem || 'firebase',
    ts_ms: Date.now(),
  };

  copyDefinedFields(data, payload, [
    'habilitado',
    'enabled',
    'hora',
    'hour',
    'minuto',
    'minute',
    'periodicidade_horas',
    'interval_hours',
    'capture_hour',
    'capture_minute',
    'capture_interval_hours',
    'config',
    'reason',
    'metadata',
  ]);

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
