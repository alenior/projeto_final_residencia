import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/models/device_status.dart';
import '../../../data/providers.dart';

class DevicePage extends ConsumerWidget {
  const DevicePage({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final deviceId = ref.watch(selectedDeviceIdProvider);
    final stream = ref
        .watch(deviceRepositoryProvider)
        .watchCurrentStatus(deviceId);

    return Scaffold(
      backgroundColor: Colors.blueGrey.shade50,
      appBar: AppBar(
        title: const Text('Informações do dispositivo'),
        backgroundColor: Colors.blueGrey.shade800,
        foregroundColor: Colors.white,
      ),
      body: StreamBuilder<DeviceStatus>(
        stream: stream,
        builder: (context, snapshot) {
          final status = snapshot.data ?? DeviceStatus.empty(deviceId);
          if (snapshot.connectionState == ConnectionState.waiting &&
              !snapshot.hasData) {
            return const Center(child: CircularProgressIndicator());
          }

          return ListView(
            padding: const EdgeInsets.all(16),
            children: [
              _ConnectivityCard(status: status),
              const SizedBox(height: 12),
              _InfoSection(
                title: 'Identificação',
                children: [
                  _InfoRow(label: 'Device ID', value: status.deviceId),
                  _InfoRow(label: 'Namespace', value: status.namespace),
                  _InfoRow(
                    label: 'Firmware',
                    value: status.firmware ?? 'não informado',
                  ),
                  _InfoRow(label: 'MAC', value: status.mac ?? 'não informado'),
                  _InfoRow(label: 'UID', value: status.uid ?? 'não informado'),
                ],
              ),
              const SizedBox(height: 12),
              _InfoSection(
                title: 'Rede',
                children: [
                  _InfoRow(label: 'IP', value: status.ip ?? 'não informado'),
                  _InfoRow(
                    label: 'SSID',
                    value: status.ssid ?? 'não informado',
                  ),
                  _InfoRow(
                    label: 'RSSI',
                    value: status.rssi == null
                        ? 'não informado'
                        : '${status.rssi} dBm',
                  ),
                  _InfoRow(
                    label: 'Tópico status',
                    value: status.topic.isEmpty
                        ? 'não informado'
                        : status.topic,
                  ),
                ],
              ),
              const SizedBox(height: 12),
              _InfoSection(
                title: 'Runtime',
                children: [
                  _InfoRow(
                    label: 'Uptime',
                    value: _formatDuration(status.uptimeMs),
                  ),
                  _InfoRow(
                    label: 'Heap livre',
                    value: _formatBytes(status.memFree),
                  ),
                  _InfoRow(
                    label: 'Heap alocado',
                    value: _formatBytes(status.memAlloc),
                  ),
                  _InfoRow(
                    label: 'PSRAM livre',
                    value: _formatBytes(status.psramFree),
                  ),
                  _InfoRow(
                    label: 'Frequência CPU',
                    value: status.frequencyHz == null
                        ? 'não informado'
                        : '${status.frequencyHz} Hz',
                  ),
                  _InfoRow(
                    label: 'Reset cause',
                    value: status.resetCause?.toString() ?? 'não informado',
                  ),
                  _InfoRow(
                    label: 'Wake reason',
                    value: status.wakeReason?.toString() ?? 'não informado',
                  ),
                ],
              ),
              const SizedBox(height: 12),
              _InfoSection(
                title: 'Última atualização',
                children: [
                  _InfoRow(
                    label: 'Servidor',
                    value: _formatDateTime(context, status.updatedAt),
                  ),
                  _InfoRow(
                    label: 'Dispositivo',
                    value: _formatDateTime(context, status.deviceTimestamp),
                  ),
                  _InfoRow(
                    label: 'Idade do status',
                    value: _formatAge(status.age),
                  ),
                ],
              ),
            ],
          );
        },
      ),
    );
  }

  static String _formatDateTime(BuildContext context, DateTime? value) {
    if (value == null) return 'não informado';
    final local = value.toLocal();
    final date = MaterialLocalizations.of(context).formatMediumDate(local);
    final time = TimeOfDay.fromDateTime(local).format(context);
    return '$date $time';
  }

  static String _formatAge(Duration? value) {
    if (value == null) return 'sem atualização';
    if (value.inSeconds < 60) return '${value.inSeconds}s';
    if (value.inMinutes < 60) return '${value.inMinutes}min';
    return '${value.inHours}h ${value.inMinutes.remainder(60)}min';
  }

  static String _formatDuration(int? milliseconds) {
    if (milliseconds == null) return 'não informado';
    final duration = Duration(milliseconds: milliseconds);
    if (duration.inMinutes < 1) return '${duration.inSeconds}s';
    if (duration.inHours < 1) {
      return '${duration.inMinutes}min ${duration.inSeconds.remainder(60)}s';
    }
    return '${duration.inHours}h ${duration.inMinutes.remainder(60)}min';
  }

  static String _formatBytes(int? bytes) {
    if (bytes == null) return 'não informado';
    if (bytes < 1024) return '$bytes B';
    if (bytes < 1024 * 1024) {
      return '${(bytes / 1024).toStringAsFixed(1)} KiB';
    }
    return '${(bytes / (1024 * 1024)).toStringAsFixed(1)} MiB';
  }
}

class _ConnectivityCard extends StatelessWidget {
  final DeviceStatus status;

  const _ConnectivityCard({required this.status});

  @override
  Widget build(BuildContext context) {
    final online = status.effectiveOnline;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Row(
          children: [
            Icon(
              online ? Icons.cloud_done : Icons.cloud_off,
              size: 42,
              color: online ? Colors.green : Colors.red,
            ),
            const SizedBox(width: 16),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    online ? 'Dispositivo online' : 'Dispositivo offline',
                    style: Theme.of(context).textTheme.titleLarge?.copyWith(
                      fontWeight: FontWeight.bold,
                    ),
                  ),
                  const SizedBox(height: 4),
                  Text(
                    status.isStale
                        ? 'Status ausente ou sem atualização recente.'
                        : 'Status atualizado recentemente pelo ESP32-S3.',
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _InfoSection extends StatelessWidget {
  final String title;
  final List<Widget> children;

  const _InfoSection({required this.title, required this.children});

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              title,
              style: Theme.of(
                context,
              ).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.bold),
            ),
            const Divider(height: 24),
            ...children,
          ],
        ),
      ),
    );
  }
}

class _InfoRow extends StatelessWidget {
  final String label;
  final String value;

  const _InfoRow({required this.label, required this.value});

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SizedBox(
            width: 130,
            child: Text(
              label,
              style: const TextStyle(fontWeight: FontWeight.w600),
            ),
          ),
          Expanded(child: SelectableText(value)),
        ],
      ),
    );
  }
}
