import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_riverpod/legacy.dart';
import 'repositories/command_repository.dart';
import 'repositories/device_repository.dart';

var selectedDeviceIdProvider = StateProvider<String>(
  (_) => 'esp32s3-estufa-001',
);

final firestoreProvider = Provider<FirebaseFirestore>(
  (_) => FirebaseFirestore.instance,
);

final deviceRepositoryProvider = Provider<DeviceRepository>(
  (ref) => DeviceRepository(ref.watch(firestoreProvider)),
);

final commandRepositoryProvider = Provider<CommandRepository>(
  (ref) => CommandRepository(ref.watch(firestoreProvider)),
);
