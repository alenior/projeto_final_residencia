import 'package:cloud_firestore/cloud_firestore.dart';

import '../../core/models/command_request.dart';
import '../../core/models/irrigation_reading.dart';
import 'command_repository.dart';

class IrrigationRepository {
  final FirebaseFirestore _firestore;
  final CommandRepository _commandRepository;

  IrrigationRepository({
    FirebaseFirestore? firestore,
    required CommandRepository commandRepository,
  }) : _firestore = firestore ?? FirebaseFirestore.instance,
       _commandRepository = commandRepository;

  Stream<List<IrrigationReading>> watchHistory(
    String deviceId, {
    int limit = 60,
  }) {
    return _firestore
        .collection('devices/$deviceId/irrigation')
        .orderBy('created_at', descending: true)
        .limit(limit)
        .snapshots()
        .map(
          (snapshot) =>
              snapshot.docs.map(IrrigationReading.fromFirestore).toList(),
        );
  }

  Future<void> setPump(String deviceId, bool enabled) async {
    await _commandRepository.sendCommand(
      deviceId,
      CommandRequest(
        comando: 'irrigar',
        status: enabled,
        origem: 'flutter_irrigation_card',
        extraPayload: const {
          'reason': 'manual_flutter_irrigation',
          'pump_timeout_ms': 15000,
          'timeout_bomba_ms': 15000,
        },
      ),
    );
  }

  Future<void> configureIrrigation(
    String deviceId, {
    required double minMoisturePercent,
    required int readIntervalSeconds,
    int? dryRaw,
    int? wetRaw,
  }) async {
    final safeThreshold = minMoisturePercent.clamp(1.0, 95.0);
    final safeIntervalMs = readIntervalSeconds < 5
        ? 5000
        : readIntervalSeconds * 1000;
    final payload = <String, dynamic>{
      'reason': 'config_irrigation_flutter',
      'soil_min_moisture_percent': safeThreshold,
      'umidade_minima_solo_percent': safeThreshold,
      'soil_read_interval_ms': safeIntervalMs,
      'intervalo_leitura_solo_ms': safeIntervalMs,
      'pump_timeout_ms': 15000,
      'timeout_bomba_ms': 15000,
    };

    if (dryRaw != null) {
      final safeDry = dryRaw.clamp(0, 4095);
      payload['soil_dry_raw'] = safeDry;
      payload['solo_seco_raw'] = safeDry;
    }
    if (wetRaw != null) {
      final safeWet = wetRaw.clamp(0, 4095);
      payload['soil_wet_raw'] = safeWet;
      payload['solo_umido_raw'] = safeWet;
    }

    await _commandRepository.sendCommand(
      deviceId,
      CommandRequest(
        comando: 'configurar_rega',
        status: true,
        origem: 'flutter_irrigation_card',
        extraPayload: payload,
      ),
    );
  }
}
