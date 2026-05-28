class FirestorePaths {
  static String deviceDoc(String deviceId) => 'devices/$deviceId';
  static String statusDoc(String deviceId) => 'devices/$deviceId/status/current';
  static String telemetryCol(String deviceId) => 'devices/$deviceId/telemetry';
  static String alertsCol(String deviceId) => 'devices/$deviceId/alerts';
  static String commandsCol(String deviceId) => 'devices/$deviceId/commands';
  static String actionsCol(String deviceId) => 'devices/$deviceId/actions';
}