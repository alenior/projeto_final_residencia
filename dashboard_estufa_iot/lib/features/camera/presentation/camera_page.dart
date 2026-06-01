import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/models/camera_item.dart';
import '../../../core/models/camera_schedule.dart';
import '../../../data/providers.dart';

class CameraPage extends ConsumerStatefulWidget {
  const CameraPage({super.key});

  @override
  ConsumerState<CameraPage> createState() => _CameraPageState();
}

class _CameraPageState extends ConsumerState<CameraPage> {
  Future<List<CameraItem>>? _imagesFuture;
  String? _loadedDeviceId;
  bool _requestingCapture = false;

  void _ensureImagesLoaded(String deviceId) {
    if (_loadedDeviceId == deviceId && _imagesFuture != null) return;
    _loadedDeviceId = deviceId;
    _imagesFuture = ref
        .read(cameraRepositoryProvider)
        .listStorageImages(deviceId);
  }

  void _refreshImages(String deviceId) {
    setState(() {
      _loadedDeviceId = deviceId;
      _imagesFuture = ref
          .read(cameraRepositoryProvider)
          .listStorageImages(deviceId);
    });
  }

  Future<void> _requestCapture(String deviceId) async {
    setState(() => _requestingCapture = true);

    try {
      await ref.read(cameraRepositoryProvider).requestCapture(deviceId);
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Comando de captura enviado para o ESP32.'),
        ),
      );
    } catch (error) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Falha ao solicitar captura: $error')),
      );
    } finally {
      if (mounted) setState(() => _requestingCapture = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final deviceId = ref.watch(selectedDeviceIdProvider);
    final repo = ref.watch(cameraRepositoryProvider);
    _ensureImagesLoaded(deviceId);

    return Scaffold(
      backgroundColor: Colors.grey.shade200,
      appBar: AppBar(
        backgroundColor: Colors.grey.shade700,
        foregroundColor: Colors.white,
        title: const Text('Câmera'),
        actions: [
          IconButton(
            tooltip: 'Atualizar histórico',
            onPressed: () => _refreshImages(deviceId),
            icon: const Icon(Icons.refresh),
          ),
        ],
      ),
      body: RefreshIndicator(
        onRefresh: () async => _refreshImages(deviceId),
        child: ListView(
          physics: const AlwaysScrollableScrollPhysics(),
          padding: const EdgeInsets.all(16),
          children: [
            _CameraCommandCard(
              deviceId: deviceId,
              requestingCapture: _requestingCapture,
              onCapturePressed: () => _requestCapture(deviceId),
              onSchedulePressed: () => _openScheduleDialog(context, deviceId),
            ),
            const SizedBox(height: 16),
            _CameraScheduleSummary(
              scheduleStream: repo.watchSchedule(deviceId),
            ),
            const SizedBox(height: 16),
            _ImageHistorySection(
              imagesFuture: _imagesFuture!,
              onRetry: () => _refreshImages(deviceId),
            ),
          ],
        ),
      ),
    );
  }

  Future<void> _openScheduleDialog(
    BuildContext context,
    String deviceId,
  ) async {
    final repo = ref.read(cameraRepositoryProvider);
    final currentSchedule = await repo.watchSchedule(deviceId).first;

    if (!context.mounted) return;

    await showDialog<void>(
      context: context,
      builder: (context) {
        var draft = currentSchedule;

        return StatefulBuilder(
          builder: (context, setDialogState) {
            Future<void> pickTime() async {
              final picked = await showTimePicker(
                context: context,
                initialTime: TimeOfDay(hour: draft.hour, minute: draft.minute),
              );
              if (picked == null) return;
              setDialogState(() {
                draft = draft.copyWith(
                  hour: picked.hour,
                  minute: picked.minute,
                );
              });
            }

            return AlertDialog(
              title: const Text('Rotina automática da câmera'),
              content: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  SwitchListTile(
                    value: draft.enabled,
                    contentPadding: EdgeInsets.zero,
                    title: const Text('Captura automática'),
                    subtitle: const Text(
                      'Permite que o ESP32 capture imagens pela agenda.',
                    ),
                    onChanged: (value) {
                      setDialogState(
                        () => draft = draft.copyWith(enabled: value),
                      );
                    },
                  ),
                  ListTile(
                    contentPadding: EdgeInsets.zero,
                    leading: const Icon(Icons.schedule),
                    title: const Text('Horário base'),
                    subtitle: Text(draft.formattedTime),
                    trailing: TextButton(
                      onPressed: pickTime,
                      child: const Text('Alterar'),
                    ),
                  ),
                  DropdownButtonFormField<int>(
                    initialValue: draft.intervalHours,
                    decoration: const InputDecoration(
                      labelText: 'Periodicidade',
                      border: OutlineInputBorder(),
                    ),
                    items: const [
                      DropdownMenuItem(value: 1, child: Text('A cada 1 hora')),
                      DropdownMenuItem(value: 2, child: Text('A cada 2 horas')),
                      DropdownMenuItem(value: 4, child: Text('A cada 4 horas')),
                      DropdownMenuItem(value: 6, child: Text('A cada 6 horas')),
                      DropdownMenuItem(
                        value: 12,
                        child: Text('A cada 12 horas'),
                      ),
                      DropdownMenuItem(
                        value: 24,
                        child: Text('Uma vez ao dia'),
                      ),
                    ],
                    onChanged: (value) {
                      if (value == null) return;
                      setDialogState(
                        () => draft = draft.copyWith(intervalHours: value),
                      );
                    },
                  ),
                ],
              ),
              actions: [
                TextButton(
                  onPressed: () => Navigator.of(context).pop(),
                  child: const Text('Cancelar'),
                ),
                FilledButton.icon(
                  onPressed: () async {
                    await repo.saveSchedule(deviceId, draft);
                    if (!context.mounted) return;
                    Navigator.of(context).pop();
                    ScaffoldMessenger.of(context).showSnackBar(
                      const SnackBar(
                        content: Text('Agenda da câmera enviada ao ESP32.'),
                      ),
                    );
                  },
                  icon: const Icon(Icons.save),
                  label: const Text('Salvar'),
                ),
              ],
            );
          },
        );
      },
    );
  }
}

class _CameraCommandCard extends StatelessWidget {
  final String deviceId;
  final bool requestingCapture;
  final VoidCallback onCapturePressed;
  final VoidCallback onSchedulePressed;

  const _CameraCommandCard({
    required this.deviceId,
    required this.requestingCapture,
    required this.onCapturePressed,
    required this.onSchedulePressed,
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
                Icon(Icons.camera_alt, color: Colors.green.shade800, size: 32),
                const SizedBox(width: 12),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        'OV5640',
                        style: Theme.of(context).textTheme.titleLarge,
                      ),
                      Text('Dispositivo: $deviceId'),
                    ],
                  ),
                ),
              ],
            ),
            const SizedBox(height: 16),
            FilledButton.icon(
              onPressed: requestingCapture ? null : onCapturePressed,
              icon: requestingCapture
                  ? const SizedBox.square(
                      dimension: 18,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : const Icon(Icons.photo_camera),
              label: const Text('Capturar imagem agora'),
            ),
            const SizedBox(height: 8),
            OutlinedButton.icon(
              onPressed: onSchedulePressed,
              icon: const Icon(Icons.event_repeat),
              label: const Text('Configurar captura automática'),
            ),
          ],
        ),
      ),
    );
  }
}

class _CameraScheduleSummary extends StatelessWidget {
  final Stream<CameraSchedule> scheduleStream;

  const _CameraScheduleSummary({required this.scheduleStream});

  @override
  Widget build(BuildContext context) {
    return StreamBuilder<CameraSchedule>(
      stream: scheduleStream,
      builder: (context, snapshot) {
        final schedule = snapshot.data ?? const CameraSchedule();
        final enabledText = schedule.enabled ? 'ativa' : 'desativada';
        final intervalText = schedule.intervalHours >= 24
            ? 'uma vez ao dia'
            : 'a cada ${schedule.intervalHours}h';

        return Card(
          child: ListTile(
            leading: Icon(
              schedule.enabled ? Icons.event_available : Icons.event_busy,
              color: schedule.enabled ? Colors.green.shade800 : Colors.grey,
            ),
            title: const Text('Captura automática'),
            subtitle: Text(
              'Agenda $enabledText: $intervalText, base ${schedule.formattedTime}.',
            ),
          ),
        );
      },
    );
  }
}

class _ImageHistorySection extends StatelessWidget {
  final Future<List<CameraItem>> imagesFuture;
  final VoidCallback onRetry;

  const _ImageHistorySection({
    required this.imagesFuture,
    required this.onRetry,
  });

  @override
  Widget build(BuildContext context) {
    return FutureBuilder<List<CameraItem>>(
      future: imagesFuture,
      builder: (context, snapshot) {
        if (snapshot.connectionState == ConnectionState.waiting) {
          return const Card(
            child: Padding(
              padding: EdgeInsets.all(24),
              child: Center(child: CircularProgressIndicator()),
            ),
          );
        }

        if (snapshot.hasError) {
          return Card(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  const Text(
                    'Não foi possível carregar o histórico de imagens.',
                  ),
                  const SizedBox(height: 8),
                  Text(snapshot.error.toString()),
                  const SizedBox(height: 12),
                  OutlinedButton.icon(
                    onPressed: onRetry,
                    icon: const Icon(Icons.refresh),
                    label: const Text('Tentar novamente'),
                  ),
                ],
              ),
            ),
          );
        }

        final images = snapshot.data ?? const <CameraItem>[];
        if (images.isEmpty) {
          return Card(
            child: Padding(
              padding: const EdgeInsets.all(24),
              child: Column(
                children: [
                  Icon(
                    Icons.photo_library_outlined,
                    size: 48,
                    color: Colors.grey.shade700,
                  ),
                  const SizedBox(height: 12),
                  const Text(
                    'Nenhuma imagem encontrada no Firebase Storage ainda.',
                    textAlign: TextAlign.center,
                  ),
                  const SizedBox(height: 8),
                  const Text(
                    'Use “Capturar imagem agora” e atualize esta tela após o ESP32 concluir o upload.',
                    textAlign: TextAlign.center,
                  ),
                ],
              ),
            ),
          );
        }

        return Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Histórico de imagens',
              style: Theme.of(context).textTheme.titleLarge,
            ),
            const SizedBox(height: 8),
            LayoutBuilder(
              builder: (context, constraints) {
                final crossAxisCount = constraints.maxWidth >= 900
                    ? 4
                    : constraints.maxWidth >= 600
                    ? 3
                    : 2;

                return GridView.builder(
                  shrinkWrap: true,
                  physics: const NeverScrollableScrollPhysics(),
                  gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
                    crossAxisCount: crossAxisCount,
                    crossAxisSpacing: 12,
                    mainAxisSpacing: 12,
                    childAspectRatio: 0.78,
                  ),
                  itemCount: images.length,
                  itemBuilder: (context, index) {
                    return _CameraImageTile(item: images[index]);
                  },
                );
              },
            ),
          ],
        );
      },
    );
  }
}

class _CameraImageTile extends StatelessWidget {
  final CameraItem item;

  const _CameraImageTile({required this.item});

  @override
  Widget build(BuildContext context) {
    return Card(
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: item.hasPreview ? () => _openPreview(context, item) : null,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Expanded(
              child: item.hasPreview
                  ? Image.network(
                      item.downloadUrl!,
                      fit: BoxFit.cover,
                      loadingBuilder: (context, child, loadingProgress) {
                        if (loadingProgress == null) return child;
                        return const Center(child: CircularProgressIndicator());
                      },
                      errorBuilder: (context, error, stackTrace) {
                        return const _ImagePlaceholder(
                          label: 'Erro ao carregar',
                        );
                      },
                    )
                  : const _ImagePlaceholder(label: 'Sem prévia'),
            ),
            Padding(
              padding: const EdgeInsets.all(8),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    item.id,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: Theme.of(context).textTheme.labelLarge,
                  ),
                  const SizedBox(height: 4),
                  Text(_formatDate(item.updatedAt ?? item.createdAt)),
                  Text(_formatBytes(item.sizeBytes)),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  void _openPreview(BuildContext context, CameraItem item) {
    showDialog<void>(
      context: context,
      builder: (context) {
        return Dialog(
          insetPadding: const EdgeInsets.all(16),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              AppBar(
                automaticallyImplyLeading: false,
                title: Text(item.id, overflow: TextOverflow.ellipsis),
                actions: [
                  IconButton(
                    onPressed: () => Navigator.of(context).pop(),
                    icon: const Icon(Icons.close),
                  ),
                ],
              ),
              Flexible(
                child: InteractiveViewer(
                  child: Image.network(item.downloadUrl!, fit: BoxFit.contain),
                ),
              ),
              Padding(
                padding: const EdgeInsets.all(12),
                child: Text(item.path, textAlign: TextAlign.center),
              ),
            ],
          ),
        );
      },
    );
  }
}

class _ImagePlaceholder extends StatelessWidget {
  final String label;

  const _ImagePlaceholder({required this.label});

  @override
  Widget build(BuildContext context) {
    return ColoredBox(
      color: Colors.grey.shade300,
      child: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(Icons.broken_image_outlined, color: Colors.grey.shade700),
            const SizedBox(height: 8),
            Text(label),
          ],
        ),
      ),
    );
  }
}

String _formatDate(DateTime? value) {
  if (value == null) return 'Data indisponível';
  final local = value.toLocal();
  final date =
      '${local.day.toString().padLeft(2, '0')}/${local.month.toString().padLeft(2, '0')}/${local.year}';
  final time =
      '${local.hour.toString().padLeft(2, '0')}:${local.minute.toString().padLeft(2, '0')}';
  return '$date $time';
}

String _formatBytes(int? value) {
  if (value == null) return 'Tamanho indisponível';
  if (value < 1024) return '$value B';
  final kb = value / 1024;
  if (kb < 1024) return '${kb.toStringAsFixed(1)} KB';
  return '${(kb / 1024).toStringAsFixed(1)} MB';
}
