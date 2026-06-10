import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:firebase_storage/firebase_storage.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_riverpod/legacy.dart';

import 'repositories/camera_repository.dart';
import 'repositories/climate_repository.dart';
import 'repositories/irrigation_repository.dart';
import 'repositories/predator_repository.dart';
import 'repositories/command_repository.dart';
import 'repositories/device_repository.dart';

/// Dispositivo selecionado no dashboard.
///
/// O valor inicial acompanha o `MQTT_DEVICE_ID` usado nos exemplos do firmware,
/// mas pode ser alterado posteriormente por uma tela de seleção/configuração.
const defaultDeviceId = 'esp32s3-estufa-001';

final selectedDeviceIdProvider = StateProvider<String>((ref) {
  return defaultDeviceId;
});

/// Instância compartilhada do Firestore usada pelos repositórios do dashboard.
final firebaseFirestoreProvider = Provider<FirebaseFirestore>((ref) {
  return FirebaseFirestore.instance;
});

/// Instância compartilhada do Firebase Storage usada para o histórico da câmera.
final firebaseStorageProvider = Provider<FirebaseStorage>((ref) {
  return FirebaseStorage.instance;
});

/// Repositório responsável por gravar comandos em `devices/{deviceId}/commands`.
final commandRepositoryProvider = Provider<CommandRepository>((ref) {
  return CommandRepository(firestore: ref.watch(firebaseFirestoreProvider));
});

/// Repositório do módulo de câmera.
///
/// Atenção: `CameraRepository` usa construtor com parâmetros nomeados. Por isso
/// `commandRepository` deve ser informado pelo nome, evitando o erro Dart
/// `extra_positional_arguments_could_be_named`.
final cameraRepositoryProvider = Provider<CameraRepository>((ref) {
  return CameraRepository(
    storage: ref.watch(firebaseStorageProvider),
    firestore: ref.watch(firebaseFirestoreProvider),
    commandRepository: ref.watch(commandRepositoryProvider),
  );
});

/// Repositório do módulo Clima: histórico de LDR/HDC1080 e comando
/// manual da iluminação.
final climateRepositoryProvider = Provider<ClimateRepository>((ref) {
  return ClimateRepository(
    firestore: ref.watch(firebaseFirestoreProvider),
    commandRepository: ref.watch(commandRepositoryProvider),
  );
});

/// Repositório do módulo Rega: histórico do sensor de solo, configuração de
/// periodicidade/limiar e comando manual da bomba.
final irrigationRepositoryProvider = Provider<IrrigationRepository>((ref) {
  return IrrigationRepository(
    firestore: ref.watch(firebaseFirestoreProvider),
    commandRepository: ref.watch(commandRepositoryProvider),
  );
});

/// Repositório do módulo Predadores: histórico do PIR/buzzer, configuração de
/// monitoramento e comandos de silenciar/testar alarme.
final predatorRepositoryProvider = Provider<PredatorRepository>((ref) {
  return PredatorRepository(
    firestore: ref.watch(firebaseFirestoreProvider),
    commandRepository: ref.watch(commandRepositoryProvider),
  );
});

/// Repositório de status atual do dispositivo: consome `devices/{deviceId}/status/current`.
final deviceRepositoryProvider = Provider<DeviceRepository>((ref) {
  return DeviceRepository(firestore: ref.watch(firebaseFirestoreProvider));
});
