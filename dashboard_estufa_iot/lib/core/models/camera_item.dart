import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:firebase_storage/firebase_storage.dart';

/// Representa uma imagem capturada pela câmera da Estufa IoT.
///
/// O item pode vir diretamente do Firebase Storage ou de um documento de
/// metadados no Firestore. Isso permite evoluir o módulo de câmera sem quebrar o
/// dashboard: primeiro listamos arquivos, depois enriquecemos com metadados.
class CameraItem {
  final String id;
  final String deviceId;
  final String path;
  final String? downloadUrl;
  final String? contentType;
  final int? sizeBytes;
  final DateTime? createdAt;
  final DateTime? updatedAt;
  final Map<String, dynamic> metadata;

  const CameraItem({
    required this.id,
    required this.deviceId,
    required this.path,
    required this.metadata,
    this.downloadUrl,
    this.contentType,
    this.sizeBytes,
    this.createdAt,
    this.updatedAt,
  });

  factory CameraItem.fromFirestore(
    QueryDocumentSnapshot<Map<String, dynamic>> snapshot,
  ) {
    final data = snapshot.data();
    final deviceDoc = snapshot.reference.parent.parent;
    return CameraItem(
      id: snapshot.id,
      deviceId: _asString(data['device_id'], fallback: deviceDoc?.id ?? ''),
      path: _asString(data['path']),
      downloadUrl: data['download_url']?.toString(),
      contentType: data['content_type']?.toString(),
      sizeBytes: _asInt(data['size_bytes']),
      createdAt: _asDateTime(data['created_at']),
      updatedAt: _asDateTime(data['updated_at']),
      metadata: Map<String, dynamic>.from(data),
    );
  }

  static Future<CameraItem> fromStorageReference({
    required Reference ref,
    required String deviceId,
  }) async {
    final metadata = await ref.getMetadata();
    String? url;
    try {
      url = await ref.getDownloadURL();
    } on FirebaseException {
      url = null;
    }

    return CameraItem(
      id: ref.name,
      deviceId: deviceId,
      path: ref.fullPath,
      downloadUrl: url,
      contentType: metadata.contentType,
      sizeBytes: metadata.size,
      createdAt: metadata.timeCreated,
      updatedAt: metadata.updated,
      metadata: {
        'bucket': ref.bucket,
        'full_path': ref.fullPath,
        'name': ref.name,
        ...?metadata.customMetadata,
      },
    );
  }

  bool get hasPreview => downloadUrl != null && downloadUrl!.isNotEmpty;

  Map<String, dynamic> toMap() => {
        'device_id': deviceId,
        'path': path,
        'download_url': downloadUrl,
        'content_type': contentType,
        'size_bytes': sizeBytes,
        'created_at': createdAt,
        'updated_at': updatedAt,
        'metadata': metadata,
      };

  static String _asString(Object? value, {String fallback = ''}) {
    if (value == null) return fallback;
    return value.toString();
  }

  static int? _asInt(Object? value) {
    if (value is int) return value;
    if (value is num) return value.toInt();
    if (value is String) return int.tryParse(value);
    return null;
  }

  static DateTime? _asDateTime(Object? value) {
    if (value == null) return null;
    if (value is Timestamp) return value.toDate();
    if (value is DateTime) return value;
    if (value is String) return DateTime.tryParse(value);
    return null;
  }
}
