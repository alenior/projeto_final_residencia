import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/models/climate_reading.dart';
import '../../../data/providers.dart';

class ClimatePage extends ConsumerStatefulWidget {
  const ClimatePage({super.key});

  @override
  ConsumerState<ClimatePage> createState() => _ClimatePageState();
}

class _ClimatePageState extends ConsumerState<ClimatePage> {
  final _fanThresholdController = TextEditingController(text: '35');
  final _fanIntervalController = TextEditingController(text: '5');
  final _lightThresholdController = TextEditingController(text: '1800');
  final _lightHysteresisController = TextEditingController(text: '350');
  bool _sendingLightCommand = false;
  bool _sendingFanCommand = false;
  bool _savingFanConfig = false;
  bool _savingLightConfig = false;

  @override
  void dispose() {
    _fanThresholdController.dispose();
    _fanIntervalController.dispose();
    _lightThresholdController.dispose();
    _lightHysteresisController.dispose();
    super.dispose();
  }

  Future<void> _setLighting(String deviceId, bool enabled) async {
    setState(() => _sendingLightCommand = true);
    try {
      await ref.read(climateRepositoryProvider).setLighting(deviceId, enabled);
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(
            enabled
                ? 'Comando para acender iluminação enviado.'
                : 'Comando para apagar iluminação enviado.',
          ),
        ),
      );
    } catch (error) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('Falha ao enviar comando de iluminação: $error'),
        ),
      );
    } finally {
      if (mounted) setState(() => _sendingLightCommand = false);
    }
  }

  Future<void> _setFan(String deviceId, bool enabled) async {
    setState(() => _sendingFanCommand = true);
    try {
      await ref.read(climateRepositoryProvider).setFan(deviceId, enabled);
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(
            enabled
                ? 'Comando para ligar ventoinha enviado.'
                : 'Comando para desligar ventoinha enviado.',
          ),
        ),
      );
    } catch (error) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Falha ao enviar comando da ventoinha: $error')),
      );
    } finally {
      if (mounted) setState(() => _sendingFanCommand = false);
    }
  }

  Future<void> _saveFanConfig(String deviceId) async {
    final threshold = double.tryParse(
      _fanThresholdController.text.replaceAll(',', '.'),
    );
    final intervalMinutes = int.tryParse(_fanIntervalController.text);
    if (threshold == null ||
        threshold < 10 ||
        threshold > 60 ||
        intervalMinutes == null ||
        intervalMinutes < 1) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text(
            'Informe um limiar entre 10 e 60 °C e intervalo mínimo de 1 minuto.',
          ),
        ),
      );
      return;
    }

    setState(() => _savingFanConfig = true);
    try {
      await ref
          .read(climateRepositoryProvider)
          .configureFanAutomation(
            deviceId,
            temperatureThresholdC: threshold,
            checkIntervalMinutes: intervalMinutes,
          );
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Configuração da ventoinha enviada ao ESP32.'),
        ),
      );
    } catch (error) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Falha ao configurar ventoinha: $error')),
      );
    } finally {
      if (mounted) setState(() => _savingFanConfig = false);
    }
  }

  Future<void> _saveLightConfig(String deviceId) async {
    final threshold = int.tryParse(_lightThresholdController.text);
    final hysteresis = int.tryParse(_lightHysteresisController.text);
    if (threshold == null ||
        threshold < 0 ||
        threshold > 4095 ||
        hysteresis == null ||
        hysteresis < 10 ||
        hysteresis > 1500) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text(
            'Informe limiar entre 0 e 4095 ADC e histerese entre 10 e 1500.',
          ),
        ),
      );
      return;
    }

    setState(() => _savingLightConfig = true);
    try {
      await ref
          .read(climateRepositoryProvider)
          .configureLightSensitivity(
            deviceId,
            thresholdRaw: threshold,
            hysteresisRaw: hysteresis,
          );
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Sensibilidade da iluminação enviada ao ESP32.'),
        ),
      );
    } catch (error) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Falha ao configurar iluminação: $error')),
      );
    } finally {
      if (mounted) setState(() => _savingLightConfig = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final deviceId = ref.watch(selectedDeviceIdProvider);
    final climateStream = ref
        .watch(climateRepositoryProvider)
        .watchHistory(deviceId);

    return Scaffold(
      backgroundColor: Colors.lightBlue.shade50,
      appBar: AppBar(
        backgroundColor: Colors.lightBlue.shade700,
        foregroundColor: Colors.white,
        title: const Text('Clima'),
      ),
      body: StreamBuilder<List<ClimateReading>>(
        stream: climateStream,
        builder: (context, snapshot) {
          final readings = snapshot.data ?? const <ClimateReading>[];
          final latest = readings.isEmpty ? null : readings.first;

          return RefreshIndicator(
            onRefresh: () async => setState(() {}),
            child: ListView(
              physics: const AlwaysScrollableScrollPhysics(),
              padding: const EdgeInsets.all(16),
              children: [
                _ClimateSummaryCard(
                  latest: latest,
                  sendingLight: _sendingLightCommand,
                  sendingFan: _sendingFanCommand,
                  savingFanConfig: _savingFanConfig,
                  savingLightConfig: _savingLightConfig,
                  fanThresholdController: _fanThresholdController,
                  fanIntervalController: _fanIntervalController,
                  lightThresholdController: _lightThresholdController,
                  lightHysteresisController: _lightHysteresisController,
                  onTurnLightOn: () => _setLighting(deviceId, true),
                  onTurnLightOff: () => _setLighting(deviceId, false),
                  onSaveLightConfig: () => _saveLightConfig(deviceId),
                  onTurnFanOn: () => _setFan(deviceId, true),
                  onTurnFanOff: () => _setFan(deviceId, false),
                  onSaveFanConfig: () => _saveFanConfig(deviceId),
                ),
                const SizedBox(height: 16),
                Text(
                  'Últimos registros do clima e ventoinha',
                  style: Theme.of(context).textTheme.titleMedium?.copyWith(
                    fontWeight: FontWeight.bold,
                  ),
                ),
                const SizedBox(height: 8),
                if (snapshot.connectionState == ConnectionState.waiting &&
                    readings.isEmpty)
                  const Center(
                    child: Padding(
                      padding: EdgeInsets.all(24),
                      child: CircularProgressIndicator(),
                    ),
                  )
                else if (snapshot.hasError)
                  _EmptyClimateMessage(
                    message: 'Erro ao carregar histórico: ${snapshot.error}',
                  )
                else if (readings.isEmpty)
                  const _EmptyClimateMessage(
                    message:
                        'Ainda não há leituras de clima registradas no Firebase.',
                  )
                else
                  ...readings.map(_ClimateReadingTile.new),
              ],
            ),
          );
        },
      ),
    );
  }
}

class _ClimateSummaryCard extends StatelessWidget {
  final ClimateReading? latest;
  final bool sendingLight;
  final bool sendingFan;
  final bool savingFanConfig;
  final bool savingLightConfig;
  final TextEditingController fanThresholdController;
  final TextEditingController fanIntervalController;
  final TextEditingController lightThresholdController;
  final TextEditingController lightHysteresisController;
  final VoidCallback onTurnLightOn;
  final VoidCallback onTurnLightOff;
  final VoidCallback onSaveLightConfig;
  final VoidCallback onTurnFanOn;
  final VoidCallback onTurnFanOff;
  final VoidCallback onSaveFanConfig;

  const _ClimateSummaryCard({
    required this.latest,
    required this.sendingLight,
    required this.sendingFan,
    required this.savingFanConfig,
    required this.savingLightConfig,
    required this.fanThresholdController,
    required this.fanIntervalController,
    required this.lightThresholdController,
    required this.lightHysteresisController,
    required this.onTurnLightOn,
    required this.onTurnLightOff,
    required this.onSaveLightConfig,
    required this.onTurnFanOn,
    required this.onTurnFanOff,
    required this.onSaveFanConfig,
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
                Icon(Icons.thermostat, color: Colors.lightBlue.shade700),
                const SizedBox(width: 8),
                Text(
                  'Rotina de clima',
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
                  label: 'Temperatura',
                  value: latest?.formattedTemperature ?? '-- °C',
                  icon: Icons.device_thermostat,
                ),
                _MetricChip(
                  label: 'Umidade',
                  value: latest?.formattedHumidity ?? '-- %',
                  icon: Icons.water_drop,
                ),
                _MetricChip(
                  label: 'Luminosidade',
                  value: latest?.formattedLuminosity ?? '--',
                  icon: Icons.wb_sunny,
                ),
                _MetricChip(
                  label: 'Lâmpada LED',
                  value: latest?.lampOn == true ? 'Ligada' : 'Desligada',
                  icon: latest?.lampOn == true
                      ? Icons.lightbulb
                      : Icons.lightbulb_outline,
                ),
                _MetricChip(
                  label: 'Ventoinha',
                  value: latest?.fanOn == true ? 'Ligada' : 'Desligada',
                  icon: latest?.fanOn == true ? Icons.air : Icons.air_outlined,
                ),
              ],
            ),
            const SizedBox(height: 12),
            Text(
              latest == null
                  ? 'Aguardando primeira leitura do ESP32-S3.'
                  : 'Iluminação: ${latest!.lampReason}. Ventoinha: ${latest!.fanReason}. HDC1080: ${latest!.hdc1080Available ? 'OK' : 'não detectado'}.',
            ),
            const SizedBox(height: 16),
            Text(
              'Iluminação manual',
              style: Theme.of(
                context,
              ).textTheme.titleSmall?.copyWith(fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 8),
            Row(
              children: [
                Expanded(
                  child: FilledButton.icon(
                    onPressed: sendingLight ? null : onTurnLightOn,
                    icon: sendingLight
                        ? const SizedBox.square(
                            dimension: 18,
                            child: CircularProgressIndicator(strokeWidth: 2),
                          )
                        : const Icon(Icons.light_mode),
                    label: const Text('Acender'),
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: sendingLight ? null : onTurnLightOff,
                    icon: const Icon(Icons.lightbulb_outline),
                    label: const Text('Apagar'),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 12),
            Text(
              'Sensibilidade atual: ${latest?.formattedLightConfig ?? 'liga em <= 1800 ADC; apaga com histerese 350'}. Valores maiores acionam a lâmpada mais cedo se seu divisor LDR usa leitura menor no escuro.',
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                Expanded(
                  child: TextField(
                    controller: lightThresholdController,
                    keyboardType: TextInputType.number,
                    inputFormatters: [FilteringTextInputFormatter.digitsOnly],
                    decoration: const InputDecoration(
                      labelText: 'Limiar LDR raw',
                      border: OutlineInputBorder(),
                    ),
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: TextField(
                    controller: lightHysteresisController,
                    keyboardType: TextInputType.number,
                    inputFormatters: [FilteringTextInputFormatter.digitsOnly],
                    decoration: const InputDecoration(
                      labelText: 'Histerese raw',
                      border: OutlineInputBorder(),
                    ),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 12),
            FilledButton.icon(
              onPressed: savingLightConfig ? null : onSaveLightConfig,
              icon: savingLightConfig
                  ? const SizedBox.square(
                      dimension: 18,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : const Icon(Icons.tune),
              label: const Text('Salvar sensibilidade da lâmpada'),
            ),
            const Divider(height: 28),
            Text(
              'Ventoinha',
              style: Theme.of(
                context,
              ).textTheme.titleSmall?.copyWith(fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 8),
            Text(
              'Automação atual: liga em ${latest?.formattedFanThreshold ?? '35.0 °C'}, verifica a cada ${latest?.formattedFanCheckInterval ?? '5 min'} e usa timeout de ${latest?.formattedFanTimeout ?? '30 s'}.',
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                Expanded(
                  child: FilledButton.icon(
                    onPressed: sendingFan ? null : onTurnFanOn,
                    icon: sendingFan
                        ? const SizedBox.square(
                            dimension: 18,
                            child: CircularProgressIndicator(strokeWidth: 2),
                          )
                        : const Icon(Icons.air),
                    label: const Text('Ligar'),
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: sendingFan ? null : onTurnFanOff,
                    icon: const Icon(Icons.power_settings_new),
                    label: const Text('Desligar'),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                Expanded(
                  child: TextField(
                    controller: fanThresholdController,
                    keyboardType: const TextInputType.numberWithOptions(
                      decimal: true,
                    ),
                    inputFormatters: [
                      FilteringTextInputFormatter.allow(RegExp(r'[0-9,.]')),
                    ],
                    decoration: const InputDecoration(
                      labelText: 'Limiar (°C)',
                      border: OutlineInputBorder(),
                    ),
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: TextField(
                    controller: fanIntervalController,
                    keyboardType: TextInputType.number,
                    inputFormatters: [FilteringTextInputFormatter.digitsOnly],
                    decoration: const InputDecoration(
                      labelText: 'Verificação (min)',
                      border: OutlineInputBorder(),
                    ),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 12),
            FilledButton.icon(
              onPressed: savingFanConfig ? null : onSaveFanConfig,
              icon: savingFanConfig
                  ? const SizedBox.square(
                      dimension: 18,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : const Icon(Icons.save),
              label: const Text('Salvar automação da ventoinha'),
            ),
          ],
        ),
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

class _ClimateReadingTile extends StatelessWidget {
  final ClimateReading reading;

  const _ClimateReadingTile(this.reading);

  @override
  Widget build(BuildContext context) {
    final timestamp = reading.createdAt ?? reading.timestampDevice;
    final isAlert =
        reading.lowLight || reading.autoFanTriggered || reading.fanEvent;
    return Card(
      child: ListTile(
        leading: Icon(
          isAlert ? Icons.warning_amber : Icons.eco,
          color: isAlert ? Colors.orange : Colors.green,
        ),
        title: Text(
          '${reading.formattedTemperature} • ${reading.formattedHumidity}',
        ),
        subtitle: Text(
          'LDR ${reading.formattedLuminosity}\n'
          'LED: ${reading.lampOn ? 'ligada' : 'desligada'} (${reading.lampReason})\n'
          'Ventoinha: ${reading.fanOn ? 'ligada' : 'desligada'} (${reading.fanReason})',
        ),
        isThreeLine: true,
        trailing: Text(
          timestamp == null ? '--:--' : _formatTimestamp(timestamp),
          textAlign: TextAlign.end,
        ),
      ),
    );
  }

  static String _formatTimestamp(DateTime value) {
    final local = value.toLocal();
    String two(int n) => n.toString().padLeft(2, '0');
    return '${two(local.day)}/${two(local.month)}\n${two(local.hour)}:${two(local.minute)}';
  }
}

class _EmptyClimateMessage extends StatelessWidget {
  final String message;

  const _EmptyClimateMessage({required this.message});

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
