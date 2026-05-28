/// Contrato de comando enviado pelo app para `devices/{deviceId}/commands/*`.
///
/// A Cloud Function `dispatchCommandToMqtt` lê esses campos e publica o payload
/// equivalente no tópico MQTT consumido pelo ESP32.
class CommandRequest {
  final String comando;
  final bool status;
  final String origem;
  final String namespace;
  final String? topic;

  const CommandRequest({
    required this.comando,
    required this.status,
    this.origem = 'flutter_app',
    this.namespace = 'embarcatech2026',
    this.topic,
  });

  Map<String, dynamic> toMap() => {
        'comando': comando,
        'status': status,
        'origem': origem,
        'namespace': namespace,
        if (topic != null && topic!.isNotEmpty) 'topic': topic,
      };
}
