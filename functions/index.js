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
    clientId: `firebase-functions-${Date.now()}-${Math.random().toString(16).slice(2)}`,
    protocolVersion: 4,
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

function setCorsHeaders(res, contentType = null) {
  res.set('Access-Control-Allow-Origin', '*');
  res.set('Access-Control-Allow-Methods', 'GET,POST,OPTIONS');
  res.set('Access-Control-Allow-Headers', 'Content-Type,x-camera-upload-token,x-device-id,x-namespace,x-filename,x-reason,x-captured-at');
  if (contentType) res.set('Content-Type', contentType);
}

function functionBaseUrl(functionName) {
  const projectId = process.env.GCLOUD_PROJECT || process.env.GCP_PROJECT || '';
  const region = process.env.FUNCTION_REGION || 'us-central1';
  if (!projectId) return null;
  return `https://${region}-${projectId}.cloudfunctions.net/${functionName}`;
}

function cameraProxyUrl(path) {
  const base = functionBaseUrl('getCameraImage');
  if (!base) return null;
  return `${base}?path=${encodeURIComponent(path)}`;
}


function numberOrNull(value) {
  if (value === undefined || value === null || value === '') return null;
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : null;
}

function boolOrFalse(value) {
  if (value === true) return true;
  if (value === false || value === undefined || value === null) return false;
  if (typeof value === 'number') return value !== 0;
  if (typeof value === 'string') return value.toLowerCase() === 'true' || value === '1';
  return false;
}

function copyDefinedFields(source, target, fieldNames) {
  for (const fieldName of fieldNames) {
    if (source[fieldName] !== undefined) {
      target[fieldName] = source[fieldName];
    }
  }
}

exports.health = onRequest({ invoker: 'public' }, async (req, res) => {
  res.status(200).json({ ok: true, service: 'estufa-functions' });
});

// HTTP ingestion endpoint: can be called by app/backend/automation for historical writes.
exports.ingestMqttEvent = onRequest({ invoker: 'public' }, async (req, res) => {
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


// ESP32 -> HTTPS -> Firestore climate history.
// Grava leituras do LDR/HDC1080 em devices/{deviceId}/climate e registra evento quando a iluminacao e acionada por baixa luminosidade.
exports.ingestClimateReading = onRequest({ invoker: 'public', timeoutSeconds: 30, memory: '256Mi' }, async (req, res) => {
  try {
    setCorsHeaders(res);
    if (req.method === 'OPTIONS') {
      return res.status(204).send('');
    }
    if (req.method !== 'POST') {
      return res.status(405).json({ ok: false, error: 'Use POST' });
    }

    if (!validateUploadToken(req)) {
      return res.status(401).json({ ok: false, error: 'Token de upload invalido' });
    }

    const body = req.body || {};
    const deviceId = body.deviceId || req.get('x-device-id');
    const namespace = body.namespace || req.get('x-namespace') || DEFAULT_NAMESPACE;
    if (!deviceId) {
      return res.status(400).json({ ok: false, error: 'deviceId é obrigatório' });
    }

    const now = admin.firestore.FieldValue.serverTimestamp();
    const reading = {
      device_id: deviceId,
      namespace,
      topic: `estufa/${namespace}/${deviceId}/clima`,
      timestamp_device: body.timestamp || null,
      uptime_ms: numberOrNull(body.uptime_ms),
      ldr_raw: numberOrNull(body.ldr_raw),
      ldr_percent: numberOrNull(body.ldr_percent),
      low_light: boolOrFalse(body.low_light),
      auto_light_triggered: boolOrFalse(body.auto_light_triggered),
      light_threshold_raw: numberOrNull(body.light_threshold_raw),
      light_hysteresis_raw: numberOrNull(body.light_hysteresis_raw),
      lamp_on: boolOrFalse(body.lamp_on),
      lamp_reason: body.lamp_reason || 'unknown',
      manual_override_active: boolOrFalse(body.manual_override_active),
      fan_on: boolOrFalse(body.fan_on),
      fan_reason: body.fan_reason || 'unknown',
      auto_fan_triggered: boolOrFalse(body.auto_fan_triggered),
      fan_event: boolOrFalse(body.fan_event),
      fan_temp_threshold_c: numberOrNull(body.fan_temp_threshold_c),
      fan_check_interval_ms: numberOrNull(body.fan_check_interval_ms),
      fan_timeout_ms: numberOrNull(body.fan_timeout_ms),
      hdc1080_available: boolOrFalse(body.hdc1080_available),
      temp_c: numberOrNull(body.temp_c),
      humidity_percent: numberOrNull(body.humidity_percent),
      source: 'esp32_s3_climate',
      created_at: now,
      updated_at: now,
    };

    const base = db.collection('devices').doc(deviceId);
    const docRef = await base.collection('climate').add(reading);

    if (reading.auto_light_triggered && reading.low_light && reading.lamp_on) {
      await base.collection('events').add({
        kind: 'low_light_lamp_on',
        namespace,
        ldr_raw: reading.ldr_raw,
        ldr_percent: reading.ldr_percent,
        light_threshold_raw: reading.light_threshold_raw,
        lamp_on: reading.lamp_on,
        message: 'Luminosidade abaixo do limite; lampada LED acionada automaticamente.',
        created_at: now,
        climate_reading_id: docRef.id,
      });
    }

    if (reading.fan_event || reading.auto_fan_triggered) {
      await base.collection('events').add({
        kind: reading.auto_fan_triggered ? 'fan_on_high_temperature' : `fan_${reading.fan_reason}`,
        namespace,
        fan_on: reading.fan_on,
        fan_reason: reading.fan_reason,
        temp_c: reading.temp_c,
        fan_temp_threshold_c: reading.fan_temp_threshold_c,
        fan_check_interval_ms: reading.fan_check_interval_ms,
        fan_timeout_ms: reading.fan_timeout_ms,
        message: reading.auto_fan_triggered
          ? 'Temperatura acima do limite; ventoinha acionada automaticamente.'
          : `Evento da ventoinha registrado: ${reading.fan_reason}.`,
        created_at: now,
        climate_reading_id: docRef.id,
      });
    }

    logger.info('Leitura de clima gravada', {
      deviceId,
      readingId: docRef.id,
      ldrRaw: reading.ldr_raw,
      lampOn: reading.lamp_on,
      fanOn: reading.fan_on,
      fanReason: reading.fan_reason,
    });
    return res.status(200).json({ ok: true, readingId: docRef.id });
  } catch (err) {
    logger.error('ingestClimateReading error', err);
    return res.status(500).json({ ok: false, error: err.message });
  }
});

// ESP32 -> HTTPS -> Firebase Storage + Firestore metadata.
// Aceita JSON base64 ou corpo binario image/jpeg. No modo binario, envie headers x-device-id, x-namespace, x-filename, x-reason e x-captured-at.
exports.uploadCameraImage = onRequest({ invoker: 'public', timeoutSeconds: 60, memory: '512Mi' }, async (req, res) => {
  try {
    setCorsHeaders(res);
    if (req.method === 'OPTIONS') {
      return res.status(204).send('');
    }
    if (req.method !== 'POST') {
      return res.status(405).json({ ok: false, error: 'Use POST' });
    }

    if (!validateUploadToken(req)) {
      return res.status(401).json({ ok: false, error: 'Token de upload invalido' });
    }

    const body = req.body || {};
    const rawBody = req.rawBody;
    const isRawImage = rawBody && rawBody.length && !body.imageBase64;
    const deviceId = body.deviceId || req.get('x-device-id');
    const namespace = body.namespace || req.get('x-namespace') || DEFAULT_NAMESPACE;
    const imageBase64 = body.imageBase64;
    const contentType = body.contentType || req.get('content-type') || 'image/jpeg';
    const filename = safeStorageName(body.filename || req.get('x-filename'));
    const reason = body.reason || req.get('x-reason') || 'manual';
    const capturedAt = body.capturedAt || req.get('x-captured-at') || null;
    const metadata = body.metadata || {};

    if (!deviceId) {
      return res.status(400).json({ ok: false, error: 'deviceId é obrigatório' });
    }

    let imageBuffer;
    if (isRawImage) {
      imageBuffer = Buffer.from(rawBody);
    } else if (imageBase64) {
      imageBuffer = Buffer.from(imageBase64, 'base64');
    } else {
      return res.status(400).json({ ok: false, error: 'imageBase64 ou corpo binário image/jpeg é obrigatório' });
    }

    if (!imageBuffer.length) {
      return res.status(400).json({ ok: false, error: 'imagem vazia ou invalida' });
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
          capturedAt: capturedAt || '',
        },
      },
    });

    const proxyUrl = cameraProxyUrl(path);

    const docRef = await db.collection('devices').doc(deviceId).collection('images').add({
      device_id: deviceId,
      filename,
      path,
      content_type: contentType,
      size_bytes: imageBuffer.length,
      namespace,
      reason,
      metadata,
      upload_mode: isRawImage ? 'raw_image' : 'base64_json',
      proxy_url: proxyUrl,
      captured_at_device: capturedAt,
      created_at: admin.firestore.FieldValue.serverTimestamp(),
      updated_at: admin.firestore.FieldValue.serverTimestamp(),
      source: 'esp32_ov5640',
    });

    logger.info('Imagem da camera gravada', { deviceId, path, imageId: docRef.id, sizeBytes: imageBuffer.length });
    return res.status(200).json({ ok: true, path, imageId: docRef.id, sizeBytes: imageBuffer.length, proxyUrl });
  } catch (err) {
    logger.error('uploadCameraImage error', err);
    return res.status(500).json({ ok: false, error: err.message });
  }
});


exports.getCameraImage = onRequest({ invoker: 'public', timeoutSeconds: 30, memory: '256Mi' }, async (req, res) => {
  try {
    setCorsHeaders(res);
    if (req.method === 'OPTIONS') {
      return res.status(204).send('');
    }
    if (req.method !== 'GET') {
      return res.status(405).json({ ok: false, error: 'Use GET' });
    }

    const path = String(req.query.path || '');
    if (!path.startsWith('devices/') || !path.includes('/images/')) {
      return res.status(400).json({ ok: false, error: 'path invalido' });
    }

    const file = admin.storage().bucket().file(path);
    const [exists] = await file.exists();
    if (!exists) {
      return res.status(404).json({ ok: false, error: 'imagem nao encontrada' });
    }

    const [metadata] = await file.getMetadata();
    const [buffer] = await file.download();
    res.set('Cache-Control', 'public, max-age=300');
    res.set('Content-Type', metadata.contentType || 'image/jpeg');
    return res.status(200).send(buffer);
  } catch (err) {
    logger.error('getCameraImage error', err);
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
    'duration_ms',
    'manual_override_ms',
    'lamp_reason',
    'light_threshold_raw',
    'limite_luz_raw',
    'light_hysteresis_raw',
    'histerese_luz_raw',
    'fan_temp_threshold_c',
    'temperatura_limite_c',
    'fan_check_interval_ms',
    'intervalo_verificacao_ms',
    'fan_timeout_ms',
    'timeout_ventoinha_ms',
  ]);

  if (!payload.comando) {
    logger.warn('Comando ignorado: campo comando ausente', { deviceId, data });
    return;
  }

  const client = mqttClient();

  await new Promise((resolve, reject) => {
    client.on('connect', () => {
      client.publish(topic, JSON.stringify(payload), { qos: 1, retain: false }, (err) => {
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
