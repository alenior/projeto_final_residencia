import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:uuid/uuid.dart';
import '../../core/constants/firestore_paths.dart';
import '../../core/models/command_request.dart';

class CommandRepository {
  final FirebaseFirestore _db;
  CommandRepository(this._db);

  Future<void> sendCommand(String deviceId, CommandRequest cmd) async {
    final id = const Uuid().v4();
    final ref = _db.doc('${FirestorePaths.commandsCol(deviceId)}/$id');
    await ref.set({
      ...cmd.toMap(),
      'created_at': FieldValue.serverTimestamp(),
    });
  }
}