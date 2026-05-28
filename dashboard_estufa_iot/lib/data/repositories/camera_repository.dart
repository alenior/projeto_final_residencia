import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:firebase_storage/firebase_storage.dart';

import '../../core/models/camera_item.dart';
import '../../core/models/command_request.dart';
import 'command_repository.dart';

/// Repositório do módulo de câmera.
///
/// A leitura de histórico usa Firebase Storage, que armazenará imagens por
/// dispositivo. O comando de captura usa o mesmo contrato de comandos do
/// firmware (`capturar`) para que a Cloud Function publique MQTT ao ESP32.
class CameraRepository {
  final FirebaseStorage _storage;
  final FirebaseFirestore _firestore;
  final CommandRepository _commandRepository;

  CameraRepository({
    FirebaseStorage? storage,
    FirebaseFirestore? firestore,
    required CommandRepository commandRepository,
  })  : _storage = storage ?? FirebaseStorage.instance,
        _firestore = firestore ?? FirebaseFirestore.instance,
        _commandRepository = commandRepository;

  /// Prefixo padrão recomendado para imagens no Storage.
  ///
  /// Exemplo: `devices/esp32s3-estufa-001/images/2026-05-28_120000.jpg`.
  String storagePrefix(String deviceId) => 'devices/$deviceId/images';

  /// Lista as imagens mais recentes no Firebase Storage.
  ///
  /// O Firebase Storage não ordena por data no `list`, por isso ordenamos pelo
  /// metadata `updated` após carregar os metadados de cada arquivo.
  Future<List<CameraItem>> listStorageImages(
    String deviceId, {
    int maxResults = 30,
  }) async {
    final result = await _storage.ref(storagePrefix(deviceId)).list(
          ListOptions(maxResults: maxResults),
        );

    final items = await Future.wait(
      result.items.map((ref) => CameraItem.fromStorageReference(ref: ref, deviceId: deviceId)),
    );

    items.sort((a, b) {
      final aDate = a.updatedAt ?? a.createdAt ?? DateTime.fromMillisecondsSinceEpoch(0);
      final bDate = b.updatedAt ?? b.createdAt ?? DateTime.fromMillisecondsSinceEpoch(0);
      return bDate.compareTo(aDate);
    });

    return items;
  }

  /// Observa metadados de imagens no Firestore, se o backend passar a gravá-los.
  ///
  /// Este stream é opcional para a primeira versão, mas já deixa o dashboard
  /// pronto para enriquecer histórico com dados como motivo da captura,
  /// luminosidade, temperatura e origem do comando.
  Stream<List<CameraItem>> watchImageMetadata(String deviceId, {int limit = 50}) {
    return _firestore
        .collection('devices/$deviceId/images')
        .orderBy('created_at', descending: true)
        .limit(limit)
        .snapshots()
        .map((snapshot) => snapshot.docs.map(CameraItem.fromFirestore).toList());
  }

  /// Solicita captura imediata ao ESP32.
  Future<void> requestCapture(String deviceId) {
    return _commandRepository.sendCommand(
      deviceId,
      CommandRequest(
        comando: 'capturar',
        status: true,
        origem: 'flutter_camera_card',
      ),
    );
  }
}
