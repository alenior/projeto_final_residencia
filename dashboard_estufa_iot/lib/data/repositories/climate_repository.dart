import 'package:cloud_firestore/cloud_firestore.dart';

import '../../core/models/climate_reading.dart';
import '../../core/models/command_request.dart';
import 'command_repository.dart';

class ClimateRepository {
  final FirebaseFirestore _firestore;
  final CommandRepository _commandRepository;

  ClimateRepository({
    FirebaseFirestore? firestore,
    required CommandRepository commandRepository,
  }) : _firestore = firestore ?? FirebaseFirestore.instance,
       _commandRepository = commandRepository;

  Stream<List<ClimateReading>> watchHistory(String deviceId, {int limit = 60}) {
    return _firestore
        .collection('devices/$deviceId/climate')
        .orderBy('created_at', descending: true)
        .limit(limit)
        .snapshots()
        .map(
          (snapshot) =>
              snapshot.docs.map(ClimateReading.fromFirestore).toList(),
        );
  }

  Future<void> setLighting(String deviceId, bool enabled) async {
    await _commandRepository.sendCommand(
      deviceId,
      CommandRequest(
        comando: 'iluminar',
        status: enabled,
        origem: 'flutter_climate_card',
        extraPayload: const {
          'reason': 'manual_flutter_climate',
          'manual_override_ms': 1800000,
        },
      ),
    );
  }

  Future<void> setFan(String deviceId, bool enabled) async {
    await _commandRepository.sendCommand(
      deviceId,
      CommandRequest(
        comando: 'ventilar',
        status: enabled,
        origem: 'flutter_climate_card',
        extraPayload: const {
          'reason': 'manual_flutter_climate_fan',
          'fan_timeout_ms': 30000,
        },
      ),
    );
  }

  Future<void> configureFanAutomation(
    String deviceId, {
    required double temperatureThresholdC,
    required int checkIntervalMinutes,
  }) async {
    final safeIntervalMinutes = checkIntervalMinutes < 1
        ? 1
        : checkIntervalMinutes;
    await _commandRepository.sendCommand(
      deviceId,
      CommandRequest(
        comando: 'configurar_clima',
        status: true,
        origem: 'flutter_climate_card',
        extraPayload: {
          'reason': 'config_fan_flutter',
          'fan_temp_threshold_c': temperatureThresholdC,
          'temperatura_limite_c': temperatureThresholdC,
          'fan_check_interval_ms': safeIntervalMinutes * 60 * 1000,
          'intervalo_verificacao_ms': safeIntervalMinutes * 60 * 1000,
          'fan_timeout_ms': 30000,
          'timeout_ventoinha_ms': 30000,
        },
      ),
    );
  }
}
