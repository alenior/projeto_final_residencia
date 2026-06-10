import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/models/predator_event.dart';
import '../../../data/providers.dart';

class PredatorsPage extends ConsumerStatefulWidget {
  const PredatorsPage({super.key});

  @override
  ConsumerState<PredatorsPage> createState() => _PredatorsPageState();
}

class _PredatorsPageState extends ConsumerState<PredatorsPage> {
  final _checkIntervalController = TextEditingController(text: '500');
  final _cooldownController = TextEditingController(text: '30000');
  final _buzzerDurationController = TextEditingController(text: '5000');
  final _buzzerDutyController = TextEditingController(text: '512');
  bool _monitoringEnabled = true;
  bool _buzzerEnabled = true;
  bool _savingConfig = false;
  bool _sendingCommand = false;

  @override
  void dispose() {
    _checkIntervalController.dispose();
    _cooldownController.dispose();
    _buzzerDurationController.dispose();
    _buzzerDutyController.dispose();
    super.dispose();
  }

  Future<void> _saveConfig(String deviceId) async {
    final checkIntervalMs = int.tryParse(_checkIntervalController.text);
    final cooldownMs = int.tryParse(_cooldownController.text);
    final buzzerDurationMs = int.tryParse(_buzzerDurationController.text);
    final buzzerDuty = int.tryParse(_buzzerDutyController.text);

    if (checkIntervalMs == null ||
        checkIntervalMs < 100 ||
        cooldownMs == null ||
        cooldownMs < 5000 ||
        buzzerDurationMs == null ||
        buzzerDurationMs < 500 ||
        buzzerDuty == null ||
        buzzerDuty < 0 ||
        buzzerDuty > 1023) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text(
            'Use intervalo >=100 ms, cooldown >=5000 ms, duração >=500 ms e duty entre 0 e 1023.',
          ),
        ),
      );
      return;
    }

    setState(() => _savingConfig = true);
    try {
      await ref
          .read(predatorRepositoryProvider)
          .configureMonitoring(
            deviceId,
            monitoringEnabled: _monitoringEnabled,
            buzzerEnabled: _buzzerEnabled,
            checkIntervalMs: checkIntervalMs,
            cooldownMs: cooldownMs,
            buzzerDurationMs: buzzerDurationMs,
            buzzerDuty: buzzerDuty,
          );
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Configuração de predadores enviada ao ESP32.'),
        ),
      );
    } catch (error) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Falha ao configurar predadores: $error')),
      );
    } finally {
      if (mounted) setState(() => _savingConfig = false);
    }
  }

  Future<void> _silenceAlarm(String deviceId) async {
    setState(() => _sendingCommand = true);
    try {
      await ref.read(predatorRepositoryProvider).silenceAlarm(deviceId);
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Comando para silenciar alarme enviado.')),
      );
    } catch (error) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Falha ao silenciar alarme: $error')),
      );
    } finally {
      if (mounted) setState(() => _sendingCommand = false);
    }
  }

  Future<void> _testBuzzer(String deviceId, bool enabled) async {
    setState(() => _sendingCommand = true);
    try {
      await ref.read(predatorRepositoryProvider).testBuzzer(deviceId, enabled);
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(
            enabled
                ? 'Comando para testar buzzer enviado.'
                : 'Comando para desligar buzzer enviado.',
          ),
        ),
      );
    } catch (error) {
      if (!mounted) return;
      ScaffoldMessenger.of(
        context,
      ).showSnackBar(SnackBar(content: Text('Falha ao testar buzzer: $error')));
    } finally {
      if (mounted) setState(() => _sendingCommand = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final deviceId = ref.watch(selectedDeviceIdProvider);
    final stream = ref.watch(predatorRepositoryProvider).watchHistory(deviceId);

    return Scaffold(
      backgroundColor: Colors.deepOrange.shade50,
      appBar: AppBar(
        backgroundColor: Colors.deepOrange.shade700,
        foregroundColor: Colors.white,
        title: const Text('Predadores'),
      ),
      body: StreamBuilder<List<PredatorEvent>>(
        stream: stream,
        builder: (context, snapshot) {
          final events = snapshot.data ?? const <PredatorEvent>[];
          final latest = events.isEmpty ? null : events.first;

          return RefreshIndicator(
            onRefresh: () async => setState(() {}),
            child: ListView(
              physics: const AlwaysScrollableScrollPhysics(),
              padding: const EdgeInsets.all(16),
              children: [
                PredatorsCard(
                  latest: latest,
                  monitoringEnabled: _monitoringEnabled,
                  buzzerEnabled: _buzzerEnabled,
                  savingConfig: _savingConfig,
                  sendingCommand: _sendingCommand,
                  checkIntervalController: _checkIntervalController,
                  cooldownController: _cooldownController,
                  buzzerDurationController: _buzzerDurationController,
                  buzzerDutyController: _buzzerDutyController,
                  onMonitoringChanged: (value) =>
                      setState(() => _monitoringEnabled = value),
                  onBuzzerChanged: (value) =>
                      setState(() => _buzzerEnabled = value),
                  onSaveConfig: () => _saveConfig(deviceId),
                  onSilenceAlarm: () => _silenceAlarm(deviceId),
                  onTestBuzzer: () => _testBuzzer(deviceId, true),
                  onStopBuzzer: () => _testBuzzer(deviceId, false),
                ),
                const SizedBox(height: 16),
                Text(
                  'Últimos registros de presença e alarme',
                  style: Theme.of(context).textTheme.titleMedium?.copyWith(
                    fontWeight: FontWeight.bold,
                  ),
                ),
                const SizedBox(height: 8),
                if (snapshot.connectionState == ConnectionState.waiting &&
                    events.isEmpty)
                  const Center(
                    child: Padding(
                      padding: EdgeInsets.all(24),
                      child: CircularProgressIndicator(),
                    ),
                  )
                else if (snapshot.hasError)
                  _EmptyPredatorMessage(
                    message: 'Erro ao carregar histórico: ${snapshot.error}',
                  )
                else if (events.isEmpty)
                  const _EmptyPredatorMessage(
                    message:
                        'Ainda não há eventos de predadores registrados no Firebase.',
                  )
                else
                  ...events.map(_PredatorEventTile.new),
              ],
            ),
          );
        },
      ),
    );
  }
}

class PredatorsCard extends StatelessWidget {
  final PredatorEvent? latest;
  final bool monitoringEnabled;
  final bool buzzerEnabled;
  final bool savingConfig;
  final bool sendingCommand;
  final TextEditingController checkIntervalController;
  final TextEditingController cooldownController;
  final TextEditingController buzzerDurationController;
  final TextEditingController buzzerDutyController;
  final ValueChanged<bool> onMonitoringChanged;
  final ValueChanged<bool> onBuzzerChanged;
  final VoidCallback onSaveConfig;
  final VoidCallback onSilenceAlarm;
  final VoidCallback onTestBuzzer;
  final VoidCallback onStopBuzzer;

  const PredatorsCard({
    super.key,
    required this.latest,
    required this.monitoringEnabled,
    required this.buzzerEnabled,
    required this.savingConfig,
    required this.sendingCommand,
    required this.checkIntervalController,
    required this.cooldownController,
    required this.buzzerDurationController,
    required this.buzzerDutyController,
    required this.onMonitoringChanged,
    required this.onBuzzerChanged,
    required this.onSaveConfig,
    required this.onSilenceAlarm,
    required this.onTestBuzzer,
    required this.onStopBuzzer,
  });

  @override
  Widget build(BuildContext context) {
    return Card(
      elevation: 2,
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Row(
              children: [
                Icon(Icons.pest_control, color: Colors.deepOrange.shade700),
                const SizedBox(width: 8),
                Text(
                  'Predadores',
                  style: Theme.of(context).textTheme.titleLarge,
                ),
              ],
            ),
            const SizedBox(height: 12),
            Wrap(
              spacing: 12,
              runSpacing: 12,
              children: [
                _MetricChip(
                  label: 'PIR',
                  value: latest?.motionDetected == true
                      ? 'Movimento'
                      : 'Sem movimento',
                  icon: Icons.sensors,
                ),
                _MetricChip(
                  label: 'Alarme',
                  value: latest?.alarmActive == true ? 'Ativo' : 'Inativo',
                  icon: latest?.alarmActive == true
                      ? Icons.notifications_active
                      : Icons.notifications_none,
                ),
                _MetricChip(
                  label: 'Monitoramento',
                  value: latest?.monitoringEnabled == false
                      ? 'Desligado'
                      : 'Ligado',
                  icon: Icons.visibility,
                ),
                _MetricChip(
                  label: 'Buzzer',
                  value: latest?.buzzerEnabled == false
                      ? 'Desabilitado'
                      : 'Habilitado',
                  icon: Icons.volume_up,
                ),
              ],
            ),
            const SizedBox(height: 12),
            Text(
              latest == null
                  ? 'Aguardando primeiro evento do ESP32-S3.'
                  : 'Motivo: ${latest!.reason}. Verificação: ${latest!.formattedCheckInterval}. Cooldown: ${latest!.formattedCooldown}. PWM: ${latest!.formattedPwm}.',
            ),
            const SizedBox(height: 16),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('Monitorar presença no PIR HC-SR501'),
              subtitle: const Text(
                'GPIO42 ativo em nível alto; posicione o sensor fora de fontes de calor falsas.',
              ),
              value: monitoringEnabled,
              onChanged: onMonitoringChanged,
            ),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('Buzzer informativo'),
              subtitle: const Text(
                'GPIO3 com PWM de 5 kHz e resolução de 10 bits.',
              ),
              value: buzzerEnabled,
              onChanged: onBuzzerChanged,
            ),
            const Divider(height: 28),
            Text(
              'Controle do alarme',
              style: Theme.of(
                context,
              ).textTheme.titleSmall?.copyWith(fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 8),
            Row(
              children: [
                Expanded(
                  child: FilledButton.icon(
                    onPressed: sendingCommand ? null : onSilenceAlarm,
                    icon: sendingCommand
                        ? const SizedBox.square(
                            dimension: 18,
                            child: CircularProgressIndicator(strokeWidth: 2),
                          )
                        : const Icon(Icons.notifications_off),
                    label: const Text('Silenciar'),
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: sendingCommand ? null : onTestBuzzer,
                    icon: const Icon(Icons.volume_up),
                    label: const Text('Testar'),
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: sendingCommand ? null : onStopBuzzer,
                    icon: const Icon(Icons.stop),
                    label: const Text('Parar'),
                  ),
                ),
              ],
            ),
            const Divider(height: 28),
            Text(
              'Configuração',
              style: Theme.of(
                context,
              ).textTheme.titleSmall?.copyWith(fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                Expanded(
                  child: _NumberField(
                    controller: checkIntervalController,
                    label: 'Verificação (ms)',
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: _NumberField(
                    controller: cooldownController,
                    label: 'Cooldown alerta (ms)',
                  ),
                ),
              ],
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                Expanded(
                  child: _NumberField(
                    controller: buzzerDurationController,
                    label: 'Buzzer (ms)',
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: _NumberField(
                    controller: buzzerDutyController,
                    label: 'Duty PWM (0-1023)',
                  ),
                ),
              ],
            ),
            const SizedBox(height: 12),
            FilledButton.icon(
              onPressed: savingConfig ? null : onSaveConfig,
              icon: savingConfig
                  ? const SizedBox.square(
                      dimension: 18,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : const Icon(Icons.save),
              label: const Text('Salvar configuração de predadores'),
            ),
          ],
        ),
      ),
    );
  }
}

class _NumberField extends StatelessWidget {
  final TextEditingController controller;
  final String label;

  const _NumberField({required this.controller, required this.label});

  @override
  Widget build(BuildContext context) {
    return TextField(
      controller: controller,
      keyboardType: TextInputType.number,
      inputFormatters: [FilteringTextInputFormatter.digitsOnly],
      decoration: InputDecoration(
        labelText: label,
        border: const OutlineInputBorder(),
      ),
    );
  }
}

class _MetricChip extends StatelessWidget {
  final String label;
  final String value;
  final IconData icon;

  const _MetricChip({
    required this.label,
    required this.value,
    required this.icon,
  });

  @override
  Widget build(BuildContext context) {
    return Chip(
      avatar: Icon(icon, size: 18),
      label: Text('$label: $value'),
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
    );
  }
}

class _PredatorEventTile extends StatelessWidget {
  final PredatorEvent event;

  const _PredatorEventTile(this.event);

  @override
  Widget build(BuildContext context) {
    final timestamp = event.createdAt ?? event.timestampDevice;
    final timeText = timestamp == null
        ? 'sem horário'
        : MaterialLocalizations.of(
            context,
          ).formatMediumDate(timestamp.toLocal());

    return Card(
      child: ListTile(
        leading: Icon(
          event.motionDetected ? Icons.warning_amber : Icons.sensors,
          color: event.motionDetected ? Colors.deepOrange : Colors.green,
        ),
        title: Text(
          '${event.motionDetected ? 'Movimento detectado' : 'Sem movimento'} • alarme ${event.alarmActive ? 'ON' : 'OFF'}',
        ),
        subtitle: Text(
          'Motivo: ${event.reason}\n'
          'Buzzer: ${event.formattedBuzzerDuration} • ${event.formattedPwm}\n'
          'Verificação: ${event.formattedCheckInterval} • cooldown: ${event.formattedCooldown}',
        ),
        trailing: Text(timeText, textAlign: TextAlign.end),
        isThreeLine: true,
      ),
    );
  }
}

class _EmptyPredatorMessage extends StatelessWidget {
  final String message;

  const _EmptyPredatorMessage({required this.message});

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: Text(message, textAlign: TextAlign.center),
      ),
    );
  }
}
