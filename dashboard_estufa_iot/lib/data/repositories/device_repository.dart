import 'package:cloud_firestore/cloud_firestore.dart';
import '../../core/constants/firestore_paths.dart';

class DeviceRepository {
  final FirebaseFirestore _db;
  DeviceRepository(this._db);

  Stream<DocumentSnapshot<Map<String, dynamic>>> watchStatus(String deviceId) {
    return _db.doc(FirestorePaths.statusDoc(deviceId)).snapshots();
  }

  Stream<QuerySnapshot<Map<String, dynamic>>> watchTelemetry(String deviceId) {
    return _db
        .collection(FirestorePaths.telemetryCol(deviceId))
        .orderBy('received_at', descending: true)
        .limit(50)
        .snapshots();
  }
}