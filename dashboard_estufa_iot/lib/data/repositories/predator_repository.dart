import 'package:cloud_firestore/cloud_firestore.dart';

import '../../core/models/command_request.dart';
import '../../core/models/predator_event.dart';
import 'command_repository.dart';

class PredatorRepository {
  final FirebaseFirestore _firestore;
  final CommandRepository _commandRepository;

  PredatorRepository({
    FirebaseFirestore? firestore,
    required CommandRepository commandRepository,
  }) : _firestore = firestore ?? FirebaseFirestore.instance,
       _commandRepository = commandRepository;

  Stream<List<PredatorEvent>> watchHistory(String deviceId, {int limit = 60}) {
    return _firestore
        .collection('devices/$deviceId/predators')
        .orderBy('created_at', descending: true)
        .limit(limit)
        .snapshots()
        .map(
          (snapshot) => snapshot.docs.map(PredatorEvent.fromFirestore).toList(),
        );
  }

  Future<void> configureMonitoring(
    String deviceId, {
    required bool monitoringEnabled,
    required bool buzzerEnabled,
    required int checkIntervalMs,
    required int cooldownMs,
    required int buzzerDurationMs,
    required int buzzerDuty,
  }) async {
    await _commandRepository.sendCommand(
      deviceId,
      CommandRequest(
        comando: 'configurar_predadores',
        status: true,
        origem: 'flutter_predators_card',
        extraPayload: {
          'reason': 'config_predators_flutter',
          'monitoring_enabled': monitoringEnabled,
          'monitoramento_habilitado': monitoringEnabled,
          'buzzer_enabled': buzzerEnabled,
          'buzzer_habilitado': buzzerEnabled,
          'predator_check_interval_ms': checkIntervalMs.clamp(100, 60000),
          'intervalo_verificacao_predadores_ms': checkIntervalMs.clamp(
            100,
            60000,
          ),
          'predator_alert_cooldown_ms': cooldownMs.clamp(5000, 3600000),
          'cooldown_alerta_predadores_ms': cooldownMs.clamp(5000, 3600000),
          'buzzer_duration_ms': buzzerDurationMs.clamp(500, 60000),
          'duracao_buzzer_ms': buzzerDurationMs.clamp(500, 60000),
          'buzzer_pwm_duty': buzzerDuty.clamp(0, 1023),
          'duty_buzzer': buzzerDuty.clamp(0, 1023),
        },
      ),
    );
  }

  Future<void> silenceAlarm(String deviceId) async {
    await _commandRepository.sendCommand(
      deviceId,
      const CommandRequest(
        comando: 'silenciar_predadores',
        status: true,
        origem: 'flutter_predators_card',
        extraPayload: {'reason': 'manual_silence_flutter'},
      ),
    );
  }

  Future<void> testBuzzer(String deviceId, bool enabled) async {
    await _commandRepository.sendCommand(
      deviceId,
      CommandRequest(
        comando: 'testar_buzzer',
        status: enabled,
        origem: 'flutter_predators_card',
        extraPayload: const {'reason': 'manual_buzzer_test_flutter'},
      ),
    );
  }
}
