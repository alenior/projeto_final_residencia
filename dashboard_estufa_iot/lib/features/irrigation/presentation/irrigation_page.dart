import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/models/irrigation_reading.dart';
import '../../../data/providers.dart';

class IrrigationPage extends ConsumerStatefulWidget {
  const IrrigationPage({super.key});

  @override
  ConsumerState<IrrigationPage> createState() => _IrrigationPageState();
}

class _IrrigationPageState extends ConsumerState<IrrigationPage> {
  final _thresholdController = TextEditingController(text: '35');
  final _intervalController = TextEditingController(text: '15');
  final _dryRawController = TextEditingController(text: '3200');
  final _wetRawController = TextEditingController(text: '1400');
  bool _sendingPumpCommand = false;
  bool _savingConfig = false;

  @override
  void dispose() {
    _thresholdController.dispose();
    _intervalController.dispose();
    _dryRawController.dispose();
    _wetRawController.dispose();
    super.dispose();
  }

  Future<void> _setPump(String deviceId, bool enabled) async {
    setState(() => _sendingPumpCommand = true);
    try {
      await ref.read(irrigationRepositoryProvider).setPump(deviceId, enabled);
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(
            enabled
                ? 'Comando para ligar bomba enviado.'
                : 'Comando para desligar bomba enviado.',
          ),
        ),
      );
    } catch (error) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Falha ao enviar comando da bomba: $error')),
      );
    } finally {
      if (mounted) setState(() => _sendingPumpCommand = false);
    }
  }

  Future<void> _saveConfig(String deviceId) async {
    final threshold = double.tryParse(
      _thresholdController.text.replaceAll(',', '.'),
    );
    final intervalSeconds = int.tryParse(_intervalController.text);
    final dryRaw = int.tryParse(_dryRawController.text);
    final wetRaw = int.tryParse(_wetRawController.text);

    if (threshold == null ||
        threshold < 1 ||
        threshold > 95 ||
        intervalSeconds == null ||
        intervalSeconds < 5) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text(
            'Informe umidade mínima entre 1% e 95% e intervalo mínimo de 5 segundos.',
          ),
        ),
      );
      return;
    }
    if ((dryRaw != null && (dryRaw < 0 || dryRaw > 4095)) ||
        (wetRaw != null && (wetRaw < 0 || wetRaw > 4095))) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Calibração raw deve estar entre 0 e 4095.'),
        ),
      );
      return;
    }

    setState(() => _savingConfig = true);
    try {
      await ref
          .read(irrigationRepositoryProvider)
          .configureIrrigation(
            deviceId,
            minMoisturePercent: threshold,
            readIntervalSeconds: intervalSeconds,
            dryRaw: dryRaw,
            wetRaw: wetRaw,
          );
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Configuração de rega enviada ao ESP32.')),
      );
    } catch (error) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Falha ao configurar rega: $error')),
      );
    } finally {
      if (mounted) setState(() => _savingConfig = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final deviceId = ref.watch(selectedDeviceIdProvider);
    final stream = ref
        .watch(irrigationRepositoryProvider)
        .watchHistory(deviceId);

    return Scaffold(
      backgroundColor: Colors.green.shade50,
      appBar: AppBar(
        backgroundColor: Colors.green.shade700,
        foregroundColor: Colors.white,
        title: const Text('Rega'),
      ),
      body: StreamBuilder<List<IrrigationReading>>(
        stream: stream,
        builder: (context, snapshot) {
          final readings = snapshot.data ?? const <IrrigationReading>[];
          final latest = readings.isEmpty ? null : readings.first;

          return RefreshIndicator(
            onRefresh: () async => setState(() {}),
            child: ListView(
              physics: const AlwaysScrollableScrollPhysics(),
              padding: const EdgeInsets.all(16),
              children: [
                IrrigationCard(
                  latest: latest,
                  sendingPump: _sendingPumpCommand,
                  savingConfig: _savingConfig,
                  thresholdController: _thresholdController,
                  intervalController: _intervalController,
                  dryRawController: _dryRawController,
                  wetRawController: _wetRawController,
                  onTurnPumpOn: () => _setPump(deviceId, true),
                  onTurnPumpOff: () => _setPump(deviceId, false),
                  onSaveConfig: () => _saveConfig(deviceId),
                ),
                const SizedBox(height: 16),
                Text(
                  'Últimos registros de solo e bomba',
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
                  _EmptyIrrigationMessage(
                    message: 'Erro ao carregar histórico: ${snapshot.error}',
                  )
                else if (readings.isEmpty)
                  const _EmptyIrrigationMessage(
                    message:
                        'Ainda não há leituras de rega registradas no Firebase.',
                  )
                else
                  ...readings.map(_IrrigationReadingTile.new),
              ],
            ),
          );
        },
      ),
    );
  }
}

class IrrigationCard extends StatelessWidget {
  final IrrigationReading? latest;
  final bool sendingPump;
  final bool savingConfig;
  final TextEditingController thresholdController;
  final TextEditingController intervalController;
  final TextEditingController dryRawController;
  final TextEditingController wetRawController;
  final VoidCallback onTurnPumpOn;
  final VoidCallback onTurnPumpOff;
  final VoidCallback onSaveConfig;

  const IrrigationCard({
    super.key,
    required this.latest,
    required this.sendingPump,
    required this.savingConfig,
    required this.thresholdController,
    required this.intervalController,
    required this.dryRawController,
    required this.wetRawController,
    required this.onTurnPumpOn,
    required this.onTurnPumpOff,
    required this.onSaveConfig,
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
                Icon(Icons.water_drop, color: Colors.green.shade700),
                const SizedBox(width: 8),
                Text(
                  'Rega e umidade do solo',
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
                  label: 'Umidade do solo',
                  value: latest?.formattedSoilMoisture ?? '--%',
                  icon: Icons.grass,
                ),
                _MetricChip(
                  label: 'Bomba',
                  value: latest?.pumpOn == true ? 'Ligada' : 'Desligada',
                  icon: latest?.pumpOn == true
                      ? Icons.opacity
                      : Icons.opacity_outlined,
                ),
                _MetricChip(
                  label: 'Limiar mínimo',
                  value: latest?.formattedThreshold ?? '35.0%',
                  icon: Icons.tune,
                ),
                _MetricChip(
                  label: 'Intervalo',
                  value: latest?.formattedReadInterval ?? '15 s',
                  icon: Icons.timer,
                ),
                _MetricChip(
                  label: 'Timeout',
                  value: latest?.formattedPumpTimeout ?? '15 s',
                  icon: Icons.timer_off,
                ),
              ],
            ),
            const SizedBox(height: 12),
            Text(
              latest == null
                  ? 'Aguardando primeira leitura do ESP32-S3.'
                  : 'Status: ${latest!.lowSoilMoisture ? 'solo abaixo do limite' : 'solo dentro do limite'}. Bomba: ${latest!.pumpReason}. Calibração: ${latest!.formattedCalibration}.',
            ),
            const SizedBox(height: 16),
            Text(
              'Acionamento manual da bomba',
              style: Theme.of(
                context,
              ).textTheme.titleSmall?.copyWith(fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 8),
            Row(
              children: [
                Expanded(
                  child: FilledButton.icon(
                    onPressed: sendingPump ? null : onTurnPumpOn,
                    icon: sendingPump
                        ? const SizedBox.square(
                            dimension: 18,
                            child: CircularProgressIndicator(strokeWidth: 2),
                          )
                        : const Icon(Icons.play_arrow),
                    label: const Text('Ligar 15 s'),
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: sendingPump ? null : onTurnPumpOff,
                    icon: const Icon(Icons.stop),
                    label: const Text('Desligar'),
                  ),
                ),
              ],
            ),
            const Divider(height: 28),
            Text(
              'Automação',
              style: Theme.of(
                context,
              ).textTheme.titleSmall?.copyWith(fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 8),
            Text(
              'Liga automaticamente quando a umidade calculada fica menor ou igual ao limiar. A bomba sempre usa timeout de segurança de 15 segundos.',
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                Expanded(
                  child: TextField(
                    controller: thresholdController,
                    keyboardType: const TextInputType.numberWithOptions(
                      decimal: true,
                    ),
                    inputFormatters: [
                      FilteringTextInputFormatter.allow(RegExp(r'[0-9,.]')),
                    ],
                    decoration: const InputDecoration(
                      labelText: 'Umidade mínima (%)',
                      border: OutlineInputBorder(),
                    ),
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: TextField(
                    controller: intervalController,
                    keyboardType: TextInputType.number,
                    inputFormatters: [FilteringTextInputFormatter.digitsOnly],
                    decoration: const InputDecoration(
                      labelText: 'Leitura (s)',
                      border: OutlineInputBorder(),
                    ),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                Expanded(
                  child: TextField(
                    controller: dryRawController,
                    keyboardType: TextInputType.number,
                    inputFormatters: [FilteringTextInputFormatter.digitsOnly],
                    decoration: const InputDecoration(
                      labelText: 'Solo seco raw',
                      border: OutlineInputBorder(),
                    ),
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: TextField(
                    controller: wetRawController,
                    keyboardType: TextInputType.number,
                    inputFormatters: [FilteringTextInputFormatter.digitsOnly],
                    decoration: const InputDecoration(
                      labelText: 'Solo úmido raw',
                      border: OutlineInputBorder(),
                    ),
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
              label: const Text('Salvar automação da rega'),
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

class _IrrigationReadingTile extends StatelessWidget {
  final IrrigationReading reading;

  const _IrrigationReadingTile(this.reading);

  @override
  Widget build(BuildContext context) {
    final timestamp = reading.createdAt ?? reading.timestampDevice;
    final timeText = timestamp == null
        ? 'sem horário'
        : MaterialLocalizations.of(
            context,
          ).formatMediumDate(timestamp.toLocal());

    return Card(
      child: ListTile(
        leading: Icon(
          reading.pumpOn ? Icons.opacity : Icons.grass,
          color: reading.lowSoilMoisture ? Colors.orange : Colors.green,
        ),
        title: Text(
          '${reading.formattedSoilMoisture} • bomba ${reading.pumpOn ? 'ON' : 'OFF'}',
        ),
        subtitle: Text(
          'Motivo: ${reading.pumpReason}\n'
          'Limiar: ${reading.formattedThreshold} • intervalo: ${reading.formattedReadInterval} • timeout: ${reading.formattedPumpTimeout}\n'
          'Calibração: ${reading.formattedCalibration}',
        ),
        trailing: Text(timeText, textAlign: TextAlign.end),
        isThreeLine: true,
      ),
    );
  }
}

class _EmptyIrrigationMessage extends StatelessWidget {
  final String message;

  const _EmptyIrrigationMessage({required this.message});

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
