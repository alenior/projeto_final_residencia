class CommandRequest {
  final String comando;
  final bool status;
  final String origem;
  final String namespace;
  final String? topic;

  CommandRequest({
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
        if (topic != null) 'topic': topic,
      };
}