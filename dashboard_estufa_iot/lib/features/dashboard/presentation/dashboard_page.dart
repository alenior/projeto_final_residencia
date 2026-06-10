import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../core/models/device_status.dart';
import '../../../data/providers.dart';
import 'widgets/module_card.dart';

class DashboardPage extends ConsumerWidget {
  const DashboardPage({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final deviceId = ref.watch(selectedDeviceIdProvider);
    final statusStream = ref
        .watch(deviceRepositoryProvider)
        .watchCurrentStatus(deviceId);

    return Scaffold(
      backgroundColor: Colors.brown.shade100,
      appBar: AppBar(
        backgroundColor: Colors.lightGreenAccent,
        foregroundColor: Colors.black,
        title: const Text('Dashboard Estufa IoT'),
        actions: [
          StreamBuilder<DeviceStatus>(
            stream: statusStream,
            builder: (context, snapshot) {
              final status = snapshot.data ?? DeviceStatus.empty(deviceId);
              return _DeviceStatusButton(status: status);
            },
          ),
        ],
      ),
      body: ListView(
        padding: const EdgeInsets.all(12),
        children: [
          ModuleCard(
            title: 'Clima',
            subtitle: 'Temperatura, umidade e luminosidade',
            icon: Icons.thermostat,
            color: Colors.orange,
            onTap: () => context.push('/climate'),
          ),
          ModuleCard(
            title: 'Rega',
            subtitle: 'Umidade do solo e irrigação manual',
            icon: Icons.water_drop,
            color: Colors.lightBlue.shade200,
            onTap: () => context.push('/irrigation'),
          ),
          ModuleCard(
            title: 'Predadores',
            subtitle: 'PIR HC-SR501, buzzer e histórico de alertas',
            icon: Icons.pest_control,
            color: Colors.deepOrange.shade200,
            onTap: () => context.push('/predators'),
          ),
          ModuleCard(
            title: 'Câmera',
            subtitle: 'Histórico no Storage e captura sob demanda',
            icon: Icons.photo_camera,
            color: Colors.grey.shade300,
            onTap: () => context.push('/camera'),
          ),
        ],
      ),
    );
  }
}

class _DeviceStatusButton extends StatelessWidget {
  final DeviceStatus status;

  const _DeviceStatusButton({required this.status});

  @override
  Widget build(BuildContext context) {
    final online = status.effectiveOnline;
    final color = online ? Colors.green.shade700 : Colors.red.shade700;
    final text = online ? 'Online' : 'Offline';

    return Padding(
      padding: const EdgeInsets.only(right: 8),
      child: TextButton.icon(
        onPressed: () => context.push('/device'),
        icon: Stack(
          clipBehavior: Clip.none,
          children: [
            const Icon(Icons.memory, color: Colors.black),
            Positioned(
              right: -2,
              bottom: -2,
              child: Container(
                width: 10,
                height: 10,
                decoration: BoxDecoration(
                  color: color,
                  shape: BoxShape.circle,
                  border: Border.all(color: Colors.white, width: 1.5),
                ),
              ),
            ),
          ],
        ),
        label: Text(
          text,
          style: TextStyle(color: color, fontWeight: FontWeight.bold),
        ),
      ),
    );
  }
}
