import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:uuid/uuid.dart';

import '../../core/models/command_request.dart';

class CommandRepository {
  final FirebaseFirestore _firestore;
  final Uuid _uuid;

  CommandRepository(FirebaseFirestore watch, {FirebaseFirestore? firestore, Uuid? uuid})
      : _firestore = firestore ?? FirebaseFirestore.instance,
        _uuid = uuid ?? const Uuid();

  Future<DocumentReference<Map<String, dynamic>>> sendCommand(
    String deviceId,
    CommandRequest command,
  ) async {
    final doc = _firestore.collection('devices/$deviceId/commands').doc(_uuid.v4());
    await doc.set({
      ...command.toMap(),
      'created_at': FieldValue.serverTimestamp(),
    });
    return doc;
  }
}
