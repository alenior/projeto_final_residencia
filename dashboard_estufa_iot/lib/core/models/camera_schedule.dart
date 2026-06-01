import 'package:cloud_firestore/cloud_firestore.dart';

/// Configuração da rotina automática da câmera OV5640.
///
/// O app persiste esta configuração em `devices/{deviceId}/settings/camera` e
/// também envia um comando `configurar_camera` para o ESP32 aplicar a agenda no
/// firmware em execução.
class CameraSchedule {
  final bool enabled;
  final int hour;
  final int minute;
  final int intervalHours;
  final DateTime? updatedAt;
  final String updatedBy;

  const CameraSchedule({
    this.enabled = true,
    this.hour = 12,
    this.minute = 0,
    this.intervalHours = 24,
    this.updatedAt,
    this.updatedBy = 'flutter_app',
  });

  factory CameraSchedule.fromMap(Map<String, dynamic>? data) {
    if (data == null) return const CameraSchedule();

    return CameraSchedule(
      enabled: data['enabled'] as bool? ?? data['habilitado'] as bool? ?? true,
      hour: _boundedInt(
        data['hour'] ?? data['hora'],
        fallback: 12,
        min: 0,
        max: 23,
      ),
      minute: _boundedInt(
        data['minute'] ?? data['minuto'],
        fallback: 0,
        min: 0,
        max: 59,
      ),
      intervalHours: _boundedInt(
        data['interval_hours'] ?? data['periodicidade_horas'],
        fallback: 24,
        min: 1,
        max: 168,
      ),
      updatedAt: _dateTimeFromFirestore(data['updated_at']),
      updatedBy: data['updated_by'] as String? ?? 'flutter_app',
    );
  }

  factory CameraSchedule.fromFirestore(
    DocumentSnapshot<Map<String, dynamic>> snapshot,
  ) {
    return CameraSchedule.fromMap(snapshot.data());
  }

  CameraSchedule copyWith({
    bool? enabled,
    int? hour,
    int? minute,
    int? intervalHours,
    DateTime? updatedAt,
    String? updatedBy,
  }) {
    return CameraSchedule(
      enabled: enabled ?? this.enabled,
      hour: hour ?? this.hour,
      minute: minute ?? this.minute,
      intervalHours: intervalHours ?? this.intervalHours,
      updatedAt: updatedAt ?? this.updatedAt,
      updatedBy: updatedBy ?? this.updatedBy,
    );
  }

  Map<String, dynamic> toMap({bool includeServerTimestamp = false}) => {
    'enabled': enabled,
    'habilitado': enabled,
    'hour': hour,
    'hora': hour,
    'minute': minute,
    'minuto': minute,
    'interval_hours': intervalHours,
    'periodicidade_horas': intervalHours,
    'updated_by': updatedBy,
    if (includeServerTimestamp) 'updated_at': FieldValue.serverTimestamp(),
  };

  Map<String, dynamic> toCommandPayload() => {
    'habilitado': enabled,
    'hora': hour,
    'minuto': minute,
    'periodicidade_horas': intervalHours,
  };

  String get formattedTime {
    final hh = hour.toString().padLeft(2, '0');
    final mm = minute.toString().padLeft(2, '0');
    return '$hh:$mm';
  }

  static int _boundedInt(
    dynamic value, {
    required int fallback,
    required int min,
    required int max,
  }) {
    final parsed = value is int ? value : int.tryParse(value?.toString() ?? '');
    if (parsed == null) return fallback;
    if (parsed < min) return min;
    if (parsed > max) return max;
    return parsed;
  }

  static DateTime? _dateTimeFromFirestore(dynamic value) {
    if (value == null) return null;
    if (value is Timestamp) return value.toDate();
    if (value is DateTime) return value;
    if (value is String) return DateTime.tryParse(value);
    return null;
  }
}
