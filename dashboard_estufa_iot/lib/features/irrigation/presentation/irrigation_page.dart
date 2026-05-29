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
      backgroundColor: Colors.lightBlue.shade100,

      appBar: AppBar(
        backgroundColor: Colors.blue,
        foregroundColor: Colors.black,
        title: const Text('Rega'),
      ),

      body: Center(
        child: ElevatedButton.icon(
          icon: const Icon(Icons.play_arrow),
          label: const Text('Irrigar agora'),
          style: ElevatedButton.styleFrom(
            backgroundColor: Colors.blue,
            foregroundColor: Colors.black,
            padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 16),
          ),
          onPressed: () async {
            await repo.sendCommand(
              deviceId,
              CommandRequest(comando: 'irrigar', status: true),
            );

            if (context.mounted) {
              ScaffoldMessenger.of(context).showSnackBar(
                const SnackBar(
                  content: Text('Comando enviado para fila Firebase'),
                ),
              );
            }
          },
        ),
      ),
    );
  }
}
