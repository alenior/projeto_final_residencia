import 'package:cloud_firestore/cloud_firestore.dart';

/// Leitura histórica de telemetria da Estufa IoT.
///
/// Consome documentos gravados em `devices/{deviceId}/telemetry/*` pela Cloud
/// Function `ingestMqttEvent`. O parser aceita tanto o formato atual do
/// firmware (`clima`, `solo`, `luminosidade`, `movimento`) quanto campos planos
/// usados em testes manuais (`temp_c`, `umidade_ar`, `umidade_solo`).
class TelemetryPoint {
  final String id;
  final String deviceId;
  final String namespace;
  final String topic;
  final DateTime? receivedAt;
  final DateTime? timestamp;
  final double? temperatureC;
  final double? airHumidity;
  final double? soilHumidity;
  final double? luminosity;
  final bool? motionDetected;
  final Map<String, dynamic> raw;

  const TelemetryPoint({
    required this.id,
    required this.deviceId,
    required this.namespace,
    required this.topic,
    required this.raw,
    this.receivedAt,
    this.timestamp,
    this.temperatureC,
    this.airHumidity,
    this.soilHumidity,
    this.luminosity,
    this.motionDetected,
  });

  factory TelemetryPoint.fromFirestore(
    QueryDocumentSnapshot<Map<String, dynamic>> snapshot,
  ) {
    final deviceDoc = snapshot.reference.parent.parent;
    return TelemetryPoint.fromMap(
      id: snapshot.id,
      deviceId: deviceDoc?.id ?? '',
      data: snapshot.data(),
    );
  }

  factory TelemetryPoint.fromMap({
    required String id,
    required String deviceId,
    required Map<String, dynamic> data,
  }) {
    final clima = _asMap(data['clima']);
    final solo = _asMap(data['solo']);
    final luminosidade = _asMap(data['luminosidade']);

    return TelemetryPoint(
      id: id,
      deviceId: deviceId,
      namespace: _asString(data['namespace'], fallback: 'embarcatech2026'),
      topic: _asString(data['topic']),
      receivedAt: _asDateTime(data['received_at']),
      timestamp: _asDateTime(data['timestamp']),
      temperatureC: _firstDouble([
        data['temp_c'],
        data['temperatura_c'],
        clima['temp_c'],
        clima['temperatura_c'],
        clima['temperatura'],
      ]),
      airHumidity: _firstDouble([
        data['umidade_ar'],
        data['humidity'],
        clima['umidade_ar'],
        clima['umidade'],
      ]),
      soilHumidity: _firstDouble([
        data['umidade_solo'],
        data['soil_humidity'],
        solo['umidade_solo'],
        solo['umidade'],
        solo['percentual'],
      ]),
      luminosity: _firstDouble([
        data['luminosidade'],
        data['lux'],
        luminosidade['lux'],
        luminosidade['valor'],
        luminosidade['percentual'],
      ]),
      motionDetected: _asNullableBool(data['movimento']),
      raw: Map<String, dynamic>.from(data),
    );
  }

  bool get hasClimateReading => temperatureC != null || airHumidity != null;
  bool get hasSoilReading => soilHumidity != null;
  bool get hasLuminosityReading => luminosity != null;

  Map<String, dynamic> toMap() => {
        'id': id,
        'device_id': deviceId,
        'namespace': namespace,
        'topic': topic,
        'received_at': receivedAt,
        'timestamp': timestamp,
        'temp_c': temperatureC,
        'umidade_ar': airHumidity,
        'umidade_solo': soilHumidity,
        'luminosidade': luminosity,
        'movimento': motionDetected,
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

  static double? _firstDouble(List<Object?> values) {
    for (final value in values) {
      if (value is num) return value.toDouble();
      if (value is String) return double.tryParse(value.replaceAll(',', '.'));
    }
    return null;
  }

  static bool? _asNullableBool(Object? value) {
    if (value == null) return null;
    if (value is bool) return value;
    if (value is num) return value != 0;
    if (value is String) return value.toLowerCase() == 'true';
    return null;
  }

  static DateTime? _asDateTime(Object? value) {
    if (value == null) return null;
    if (value is Timestamp) return value.toDate();
    if (value is DateTime) return value;
    if (value is String) return DateTime.tryParse(value);
    return null;
  }
}
