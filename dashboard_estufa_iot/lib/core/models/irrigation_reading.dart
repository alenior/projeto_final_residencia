import 'package:cloud_firestore/cloud_firestore.dart';

/// Leitura histórica do módulo Rega.
///
/// Os documentos são gravados em `devices/{deviceId}/irrigation/*` pela Function
/// `ingestIrrigationReading`, a partir dos dados enviados pelo ESP32-S3.
class IrrigationReading {
  final String id;
  final String deviceId;
  final String namespace;
  final DateTime? createdAt;
  final DateTime? timestampDevice;
  final int? soilRaw;
  final double? soilMoisturePercent;
  final bool lowSoilMoisture;
  final double? soilMinMoisturePercent;
  final int? soilDryRaw;
  final int? soilWetRaw;
  final int? readIntervalMs;
  final bool pumpOn;
  final String pumpReason;
  final bool pumpEvent;
  final bool autoPumpTriggered;
  final int? pumpTimeoutMs;
  final Map<String, dynamic> raw;

  const IrrigationReading({
    required this.id,
    required this.deviceId,
    required this.namespace,
    required this.lowSoilMoisture,
    required this.pumpOn,
    required this.pumpReason,
    required this.pumpEvent,
    required this.autoPumpTriggered,
    required this.raw,
    this.createdAt,
    this.timestampDevice,
    this.soilRaw,
    this.soilMoisturePercent,
    this.soilMinMoisturePercent,
    this.soilDryRaw,
    this.soilWetRaw,
    this.readIntervalMs,
    this.pumpTimeoutMs,
  });

  factory IrrigationReading.fromFirestore(
    QueryDocumentSnapshot<Map<String, dynamic>> snapshot,
  ) {
    final deviceDoc = snapshot.reference.parent.parent;
    return IrrigationReading.fromMap(
      id: snapshot.id,
      deviceId: deviceDoc?.id ?? '',
      data: snapshot.data(),
    );
  }

  factory IrrigationReading.fromMap({
    required String id,
    required String deviceId,
    required Map<String, dynamic> data,
  }) {
    return IrrigationReading(
      id: id,
      deviceId: deviceId,
      namespace: _asString(data['namespace'], fallback: 'embarcatech2026'),
      createdAt: _asDateTime(data['created_at']),
      timestampDevice: _asDateTime(data['timestamp_device']),
      soilRaw: _asInt(data['soil_raw']),
      soilMoisturePercent: _asDouble(data['soil_moisture_percent']),
      lowSoilMoisture: _asBool(data['low_soil_moisture']),
      soilMinMoisturePercent: _asDouble(data['soil_min_moisture_percent']),
      soilDryRaw: _asInt(data['soil_dry_raw']),
      soilWetRaw: _asInt(data['soil_wet_raw']),
      readIntervalMs: _asInt(data['read_interval_ms']),
      pumpOn: _asBool(data['pump_on']),
      pumpReason: _asString(data['pump_reason'], fallback: 'unknown'),
      pumpEvent: _asBool(data['pump_event']),
      autoPumpTriggered: _asBool(data['auto_pump_triggered']),
      pumpTimeoutMs: _asInt(data['pump_timeout_ms']),
      raw: Map<String, dynamic>.from(data),
    );
  }

  String get formattedSoilMoisture {
    final percent = soilMoisturePercent == null
        ? '--'
        : '${soilMoisturePercent!.toStringAsFixed(1)}%';
    final rawValue = soilRaw?.toString() ?? '--';
    return '$percent ($rawValue ADC)';
  }

  String get formattedThreshold => soilMinMoisturePercent == null
      ? '--%'
      : '${soilMinMoisturePercent!.toStringAsFixed(1)}%';

  String get formattedReadInterval {
    final value = readIntervalMs;
    if (value == null || value <= 0) return '-- s';
    final seconds = value / 1000.0;
    return '${seconds.toStringAsFixed(seconds >= 10 ? 0 : 1)} s';
  }

  String get formattedPumpTimeout {
    final value = pumpTimeoutMs;
    if (value == null || value <= 0) return '-- s';
    final seconds = value / 1000.0;
    return '${seconds.toStringAsFixed(seconds >= 10 ? 0 : 1)} s';
  }

  String get formattedCalibration {
    final dry = soilDryRaw?.toString() ?? '--';
    final wet = soilWetRaw?.toString() ?? '--';
    return 'seco=$dry / úmido=$wet';
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

  static double? _asDouble(Object? value) {
    if (value is double) return value;
    if (value is int) return value.toDouble();
    if (value is num) return value.toDouble();
    if (value is String) return double.tryParse(value.replaceAll(',', '.'));
    return null;
  }

  static DateTime? _asDateTime(Object? value) {
    if (value is Timestamp) return value.toDate();
    if (value is DateTime) return value;
    if (value is String) return DateTime.tryParse(value);
    return null;
  }
}
