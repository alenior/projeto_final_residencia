import 'package:cloud_firestore/cloud_firestore.dart';

/// Leitura histórica do módulo Clima.
///
/// Os documentos são gravados em `devices/{deviceId}/climate/*` pela Function
/// `ingestClimateReading`, a partir dos dados enviados pelo ESP32-S3.
class ClimateReading {
  final String id;
  final String deviceId;
  final String namespace;
  final DateTime? createdAt;
  final DateTime? timestampDevice;
  final int? ldrRaw;
  final double? ldrPercent;
  final bool lowLight;
  final bool autoLightTriggered;
  final int? lightThresholdRaw;
  final bool lampOn;
  final String lampReason;
  final bool manualOverrideActive;
  final bool hdc1080Available;
  final double? temperatureC;
  final double? humidityPercent;
  final Map<String, dynamic> raw;

  const ClimateReading({
    required this.id,
    required this.deviceId,
    required this.namespace,
    required this.lowLight,
    required this.autoLightTriggered,
    required this.lampOn,
    required this.lampReason,
    required this.manualOverrideActive,
    required this.hdc1080Available,
    required this.raw,
    this.createdAt,
    this.timestampDevice,
    this.ldrRaw,
    this.ldrPercent,
    this.lightThresholdRaw,
    this.temperatureC,
    this.humidityPercent,
  });

  factory ClimateReading.fromFirestore(
    QueryDocumentSnapshot<Map<String, dynamic>> snapshot,
  ) {
    final deviceDoc = snapshot.reference.parent.parent;
    return ClimateReading.fromMap(
      id: snapshot.id,
      deviceId: deviceDoc?.id ?? '',
      data: snapshot.data(),
    );
  }

  factory ClimateReading.fromMap({
    required String id,
    required String deviceId,
    required Map<String, dynamic> data,
  }) {
    return ClimateReading(
      id: id,
      deviceId: deviceId,
      namespace: _asString(data['namespace'], fallback: 'embarcatech2026'),
      createdAt: _asDateTime(data['created_at']),
      timestampDevice: _asDateTime(data['timestamp_device']),
      ldrRaw: _asInt(data['ldr_raw']),
      ldrPercent: _asDouble(data['ldr_percent']),
      lowLight: _asBool(data['low_light']),
      autoLightTriggered: _asBool(data['auto_light_triggered']),
      lightThresholdRaw: _asInt(data['light_threshold_raw']),
      lampOn: _asBool(data['lamp_on']),
      lampReason: _asString(data['lamp_reason'], fallback: 'unknown'),
      manualOverrideActive: _asBool(data['manual_override_active']),
      hdc1080Available: _asBool(data['hdc1080_available']),
      temperatureC: _asDouble(data['temp_c']),
      humidityPercent: _asDouble(data['humidity_percent']),
      raw: Map<String, dynamic>.from(data),
    );
  }

  String get formattedLuminosity {
    final raw = ldrRaw?.toString() ?? '--';
    final percent = ldrPercent == null
        ? '--'
        : '${ldrPercent!.toStringAsFixed(1)}%';
    return '$raw ADC ($percent)';
  }

  String get formattedTemperature =>
      temperatureC == null ? '-- °C' : '${temperatureC!.toStringAsFixed(1)} °C';

  String get formattedHumidity => humidityPercent == null
      ? '-- %'
      : '${humidityPercent!.toStringAsFixed(1)} %';

  static String _asString(Object? value, {String fallback = ''}) {
    if (value == null) return fallback;
    return value.toString();
  }

  static int? _asInt(Object? value) {
    if (value is int) return value;
    if (value is num) return value.toInt();
    if (value is String) return int.tryParse(value);
    return null;
  }

  static double? _asDouble(Object? value) {
    if (value is num) return value.toDouble();
    if (value is String) return double.tryParse(value.replaceAll(',', '.'));
    return null;
  }

  static bool _asBool(Object? value) {
    if (value is bool) return value;
    if (value is num) return value != 0;
    if (value is String) return value.toLowerCase() == 'true' || value == '1';
    return false;
  }

  static DateTime? _asDateTime(Object? value) {
    if (value == null) return null;
    if (value is Timestamp) return value.toDate();
    if (value is DateTime) return value;
    if (value is String) return DateTime.tryParse(value);
    return null;
  }
}
