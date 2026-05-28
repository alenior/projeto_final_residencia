class MqttContract {
  static const namespace = 'embarcatech2026';

  static String commandTopic(String deviceId) =>
      'estufa/$namespace/$deviceId/comandos';

  static const validCommands = <String>{
    'irrigar',
    'aquecer',
    'capturar',
    'ventilar',
  };
}