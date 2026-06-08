import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/models/climate_reading.dart';
import '../../../data/providers.dart';

class ClimatePage extends ConsumerStatefulWidget {
  const ClimatePage({super.key});

  @override
  ConsumerState<ClimatePage> createState() => _ClimatePageState();
}

class _ClimatePageState extends ConsumerState<ClimatePage> {
  bool _sendingLightCommand = false;

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
                  sending: _sendingLightCommand,
                  onTurnOn: () => _setLighting(deviceId, true),
                  onTurnOff: () => _setLighting(deviceId, false),
                ),
                const SizedBox(height: 16),
                Text(
                  'Histórico das leituras',
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
  final bool sending;
  final VoidCallback onTurnOn;
  final VoidCallback onTurnOff;

  const _ClimateSummaryCard({
    required this.latest,
    required this.sending,
    required this.onTurnOn,
    required this.onTurnOff,
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
              ],
            ),
            const SizedBox(height: 12),
            Text(
              latest == null
                  ? 'Aguardando primeira leitura do ESP32-S3.'
                  : 'Último motivo da iluminação: ${latest!.lampReason}. HDC1080: ${latest!.hdc1080Available ? 'OK' : 'não detectado'}.',
            ),
            const SizedBox(height: 16),
            Row(
              children: [
                Expanded(
                  child: FilledButton.icon(
                    onPressed: sending ? null : onTurnOn,
                    icon: sending
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
                    onPressed: sending ? null : onTurnOff,
                    icon: const Icon(Icons.lightbulb_outline),
                    label: const Text('Apagar'),
                  ),
                ),
              ],
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
    return Card(
      child: ListTile(
        leading: Icon(
          reading.lowLight ? Icons.warning_amber : Icons.eco,
          color: reading.lowLight ? Colors.orange : Colors.green,
        ),
        title: Text(
          '${reading.formattedTemperature} • ${reading.formattedHumidity}',
        ),
        subtitle: Text(
          'LDR ${reading.formattedLuminosity}\nLED: ${reading.lampOn ? 'ligada' : 'desligada'} (${reading.lampReason})',
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
