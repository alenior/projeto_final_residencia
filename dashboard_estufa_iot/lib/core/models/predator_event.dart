import 'package:cloud_firestore/cloud_firestore.dart';

/// Evento/histórico do módulo Predadores.
///
/// Os documentos são gravados em `devices/{deviceId}/predators/*` pela Function
/// `ingestPredatorAlert`, a partir dos eventos do PIR HC-SR501 e do buzzer.
class PredatorEvent {
  final String id;
  final String deviceId;
  final String namespace;
  final DateTime? createdAt;
  final DateTime? timestampDevice;
  final bool motionDetected;
  final bool monitoringEnabled;
  final bool buzzerEnabled;
  final bool alarmActive;
  final bool alertEvent;
  final String reason;
  final int? pirPin;
  final int? buzzerPin;
  final int? buzzerPwmFreqHz;
  final int? buzzerPwmResolutionBits;
  final int? buzzerPwmDuty;
  final int? checkIntervalMs;
  final int? alertCooldownMs;
  final int? buzzerDurationMs;
  final Map<String, dynamic> raw;

  const PredatorEvent({
    required this.id,
    required this.deviceId,
    required this.namespace,
    required this.motionDetected,
    required this.monitoringEnabled,
    required this.buzzerEnabled,
    required this.alarmActive,
    required this.alertEvent,
    required this.reason,
    required this.raw,
    this.createdAt,
    this.timestampDevice,
    this.pirPin,
    this.buzzerPin,
    this.buzzerPwmFreqHz,
    this.buzzerPwmResolutionBits,
    this.buzzerPwmDuty,
    this.checkIntervalMs,
    this.alertCooldownMs,
    this.buzzerDurationMs,
  });

  factory PredatorEvent.fromFirestore(
    QueryDocumentSnapshot<Map<String, dynamic>> snapshot,
  ) {
    final deviceDoc = snapshot.reference.parent.parent;
    return PredatorEvent.fromMap(
      id: snapshot.id,
      deviceId: deviceDoc?.id ?? '',
      data: snapshot.data(),
    );
  }

  factory PredatorEvent.fromMap({
    required String id,
    required String deviceId,
    required Map<String, dynamic> data,
  }) {
    return PredatorEvent(
      id: id,
      deviceId: deviceId,
      namespace: _asString(data['namespace'], fallback: 'embarcatech2026'),
      createdAt: _asDateTime(data['created_at']),
      timestampDevice: _asDateTime(data['timestamp_device']),
      motionDetected: _asBool(data['motion_detected']),
      monitoringEnabled: _asBool(data['monitoring_enabled']),
      buzzerEnabled: _asBool(data['buzzer_enabled']),
      alarmActive: _asBool(data['alarm_active']),
      alertEvent: _asBool(data['alert_event']),
      reason: _asString(data['reason'], fallback: 'unknown'),
      pirPin: _asInt(data['pir_pin']),
      buzzerPin: _asInt(data['buzzer_pin']),
      buzzerPwmFreqHz: _asInt(data['buzzer_pwm_freq_hz']),
      buzzerPwmResolutionBits: _asInt(data['buzzer_pwm_resolution_bits']),
      buzzerPwmDuty: _asInt(data['buzzer_pwm_duty']),
      checkIntervalMs: _asInt(data['check_interval_ms']),
      alertCooldownMs: _asInt(data['alert_cooldown_ms']),
      buzzerDurationMs: _asInt(data['buzzer_duration_ms']),
      raw: Map<String, dynamic>.from(data),
    );
  }

  String get formattedCheckInterval =>
      _formatMs(checkIntervalMs, fallback: '0,5 s');

  String get formattedCooldown => _formatMs(alertCooldownMs, fallback: '30 s');

  String get formattedBuzzerDuration =>
      _formatMs(buzzerDurationMs, fallback: '5 s');

  String get formattedPwm {
    final freq = buzzerPwmFreqHz?.toString() ?? '5000';
    final bits = buzzerPwmResolutionBits?.toString() ?? '10';
    final duty = buzzerPwmDuty?.toString() ?? '512';
    return '$freq Hz • $bits bits • duty $duty';
  }

  static String _formatMs(int? value, {required String fallback}) {
    if (value == null || value <= 0) return fallback;
    final seconds = value / 1000.0;
    return '${seconds.toStringAsFixed(seconds >= 10 ? 0 : 1)} s';
  }

  static String _asString(Object? value, {String fallback = ''}) {
    if (value == null) return fallback;
    return value.toString();
  }

  static bool _asBool(Object? value) {
    if (value is bool) return value;
    if (value is num) return value != 0;
    if (value is String) return value.toLowerCase() == 'true' || value == '1';
    return false;
  }

  static int? _asInt(Object? value) {
    if (value is int) return value;
    if (value is num) return value.toInt();
    if (value is String) return int.tryParse(value);
    return null;
  }

  static DateTime? _asDateTime(Object? value) {
    if (value is Timestamp) return value.toDate();
    if (value is DateTime) return value;
    if (value is String) return DateTime.tryParse(value);
    return null;
  }
}
