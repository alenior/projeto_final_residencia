import 'package:cloud_firestore/cloud_firestore.dart';

/// Snapshot consolidado do estado atual do dispositivo.
///
/// Este modelo foi desenhado para consumir `devices/{deviceId}/status/current`,
/// que é atualizado pelo backend/Firebase Functions a partir dos eventos do
/// firmware. Campos ausentes são tratados de forma tolerante para manter o app
/// funcional enquanto os módulos reais do firmware ainda estão em implantação.
class DeviceStatus {
  final String deviceId;
  final String namespace;
  final String topic;
  final bool online;
  final DateTime? updatedAt;
  final DateTime? deviceTimestamp;
  final String? mac;
  final String? uid;
  final String? platform;
  final String? pythonVersion;
  final int? frequencyHz;
  final int? memFree;
  final int? memAlloc;
  final int? resetCause;
  final int? wakeReason;
  final Map<String, dynamic> raw;

  const DeviceStatus({
    required this.deviceId,
    required this.namespace,
    required this.topic,
    required this.online,
    required this.raw,
    this.updatedAt,
    this.deviceTimestamp,
    this.mac,
    this.uid,
    this.platform,
    this.pythonVersion,
    this.frequencyHz,
    this.memFree,
    this.memAlloc,
    this.resetCause,
    this.wakeReason,
  });

  factory DeviceStatus.empty(String deviceId) {
    return DeviceStatus(
      deviceId: deviceId,
      namespace: 'embarcatech2026',
      topic: '',
      online: false,
      raw: const {},
    );
  }

  factory DeviceStatus.fromFirestore(
    DocumentSnapshot<Map<String, dynamic>> snapshot,
  ) {
    final data = snapshot.data() ?? <String, dynamic>{};
    return DeviceStatus.fromMap(
      snapshot.reference.parent.parent?.id ?? snapshot.id,
      data,
    );
  }

  factory DeviceStatus.fromMap(String deviceId, Map<String, dynamic> data) {
    final device = _asMap(data['device']);
    final boot = _asMap(data['boot']);

    return DeviceStatus(
      deviceId: deviceId,
      namespace: _asString(data['namespace'], fallback: 'embarcatech2026'),
      topic: _asString(data['topic']),
      online: _asBool(data['online']),
      updatedAt: _asDateTime(data['updated_at']),
      deviceTimestamp: _asDateTime(data['timestamp']),
      mac: _firstString([data['mac'], device['mac'], boot['mac']]),
      uid: _firstString([data['uid'], device['uid'], boot['uid_hex']]),
      platform: _firstString([data['platform'], boot['platform']]),
      pythonVersion: _firstString([data['python'], boot['python']]),
      frequencyHz: _firstInt([data['freq_hz'], boot['freq_hz']]),
      memFree: _firstInt([data['mem_free'], boot['mem_free']]),
      memAlloc: _firstInt([data['mem_alloc'], boot['mem_alloc']]),
      resetCause: _firstInt([data['reset_cause'], boot['reset_cause']]),
      wakeReason: _firstInt([data['wake_reason'], boot['wake_reason']]),
      raw: Map<String, dynamic>.from(data),
    );
  }

  bool get hasBootInfo => mac != null || uid != null || platform != null;

  Duration? get age {
    if (updatedAt == null) return null;
    return DateTime.now().difference(updatedAt!);
  }

  bool get isStale {
    final currentAge = age;
    if (currentAge == null) return true;
    return currentAge > const Duration(minutes: 2);
  }

  Map<String, dynamic> toMap() => {
    'device_id': deviceId,
    'namespace': namespace,
    'topic': topic,
    'online': online,
    'updated_at': updatedAt,
    'timestamp': deviceTimestamp,
    'mac': mac,
    'uid': uid,
    'platform': platform,
    'python': pythonVersion,
    'freq_hz': frequencyHz,
    'mem_free': memFree,
    'mem_alloc': memAlloc,
    'reset_cause': resetCause,
    'wake_reason': wakeReason,
  };

  static Map<String, dynamic> _asMap(Object? value) {
    if (value is Map<String, dynamic>) return value;
    if (value is Map) return Map<String, dynamic>.from(value);
    return <String, dynamic>{};
  }

  static String _asString(Object? value, {String fallback = ''}) {
    if (value == null) return fallback;
    return value.toString();
  }

  static String? _firstString(List<Object?> values) {
    for (final value in values) {
      if (value != null && value.toString().trim().isNotEmpty) {
        return value.toString();
      }
    }
    return null;
  }

  static int? _firstInt(List<Object?> values) {
    for (final value in values) {
      if (value is int) return value;
      if (value is num) return value.toInt();
      if (value is String) return int.tryParse(value);
    }
    return null;
  }

  static bool _asBool(Object? value) {
    if (value is bool) return value;
    if (value is num) return value != 0;
    if (value is String) return value.toLowerCase() == 'true';
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
