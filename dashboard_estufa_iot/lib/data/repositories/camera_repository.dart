import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:firebase_storage/firebase_storage.dart';

import '../../core/models/camera_item.dart';
import '../../core/models/camera_schedule.dart';
import '../../core/models/command_request.dart';
import 'command_repository.dart';

/// Repositório do módulo de câmera.
///
/// - Histórico: lê imagens no Firebase Storage e metadados em Firestore.
/// - Captura manual: envia comando `capturar` para o ESP32 via Firestore->MQTT.
/// - Agenda automática: persiste `settings/camera` e envia comando
///   `configurar_camera` para o firmware aplicar a nova rotina.
class CameraRepository {
  final FirebaseStorage _storage;
  final FirebaseFirestore _firestore;
  final CommandRepository _commandRepository;

  CameraRepository({
    FirebaseStorage? storage,
    FirebaseFirestore? firestore,
    required CommandRepository commandRepository,
  }) : _storage = storage ?? FirebaseStorage.instance,
       _firestore = firestore ?? FirebaseFirestore.instance,
       _commandRepository = commandRepository;

  /// Prefixo padrão recomendado para imagens no Storage.
  ///
  /// Exemplo: `devices/esp32s3-estufa-001/images/2026-05-28_120000.jpg`.
  String storagePrefix(String deviceId) => 'devices/$deviceId/images';

  DocumentReference<Map<String, dynamic>> _scheduleRef(String deviceId) {
    return _firestore.doc('devices/$deviceId/settings/camera');
  }

  /// Lista as imagens mais recentes no Firebase Storage.
  ///
  /// O Firebase Storage não ordena por data no `list`, por isso ordenamos pelo
  /// metadata `updated` após carregar os metadados de cada arquivo.
  Future<List<CameraItem>> listStorageImages(
    String deviceId, {
    int maxResults = 30,
  }) async {
    final result = await _storage
        .ref(storagePrefix(deviceId))
        .list(ListOptions(maxResults: maxResults));

    final items = await Future.wait(
      result.items.map(
        (ref) => CameraItem.fromStorageReference(ref: ref, deviceId: deviceId),
      ),
    );

    items.sort((a, b) {
      final aDate =
          a.updatedAt ?? a.createdAt ?? DateTime.fromMillisecondsSinceEpoch(0);
      final bDate =
          b.updatedAt ?? b.createdAt ?? DateTime.fromMillisecondsSinceEpoch(0);
      return bDate.compareTo(aDate);
    });

    return items;
  }

  /// Observa metadados de imagens gravados pela Cloud Function `uploadCameraImage`.
  Stream<List<CameraItem>> watchImageMetadata(
    String deviceId, {
    int limit = 50,
  }) {
    return _firestore
        .collection('devices/$deviceId/images')
        .orderBy('created_at', descending: true)
        .limit(limit)
        .snapshots()
        .map(
          (snapshot) => snapshot.docs.map(CameraItem.fromFirestore).toList(),
        );
  }

  /// Observa a agenda atual salva em Firestore.
  Stream<CameraSchedule> watchSchedule(String deviceId) {
    return _scheduleRef(deviceId).snapshots().map(CameraSchedule.fromFirestore);
  }

  /// Solicita captura imediata ao ESP32.
  Future<void> requestCapture(
    String deviceId, {
    String reason = 'manual_flutter',
  }) {
    return _commandRepository.sendCommand(
      deviceId,
      CommandRequest(
        comando: 'capturar',
        status: true,
        origem: 'flutter_camera_card',
        extraPayload: {'reason': reason},
      ),
    );
  }

  /// Salva a agenda da câmera e publica a nova configuração para o firmware.
  Future<void> saveSchedule(String deviceId, CameraSchedule schedule) async {
    await _scheduleRef(deviceId).set(
      schedule.toMap(includeServerTimestamp: true),
      SetOptions(merge: true),
    );

    await _commandRepository.sendCommand(
      deviceId,
      CommandRequest(
        comando: 'configurar_camera',
        status: schedule.enabled,
        origem: 'flutter_camera_card',
        extraPayload: schedule.toCommandPayload(),
      ),
    );
  }
}
