import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../../data/providers.dart';
import '../../../core/models/command_request.dart';

class IrrigationPage extends ConsumerWidget {
  const IrrigationPage({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final deviceId = ref.watch(selectedDeviceIdProvider);
    final repo = ref.watch(commandRepositoryProvider);

    return Scaffold(
      appBar: AppBar(title: const Text('Rega')),
      body: Center(
        child: ElevatedButton.icon(
          icon: const Icon(Icons.play_arrow),
          label: const Text('Irrigar agora'),
          onPressed: () async {
            await repo.sendCommand(
              deviceId,
              CommandRequest(comando: 'irrigar', status: true),
            );
            if (context.mounted) {
              ScaffoldMessenger.of(context).showSnackBar(
                const SnackBar(content: Text('Comando enviado para fila Firebase')),
              );
            }
          },
        ),
      ),
    );
  }
}