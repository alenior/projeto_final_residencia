import 'package:cloud_firestore/cloud_firestore.dart';

import '../../core/models/device_status.dart';

class DeviceRepository {
  final FirebaseFirestore _firestore;

  DeviceRepository({FirebaseFirestore? firestore})
    : _firestore = firestore ?? FirebaseFirestore.instance;

  Stream<DeviceStatus> watchCurrentStatus(String deviceId) {
    return _firestore
        .doc('devices/$deviceId/status/current')
        .snapshots()
        .map(
          (snapshot) => snapshot.exists
              ? DeviceStatus.fromFirestore(snapshot)
              : DeviceStatus.empty(deviceId),
        );
  }
}
