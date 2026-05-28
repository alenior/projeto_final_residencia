import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';
import 'widgets/module_card.dart';

class DashboardPage extends StatelessWidget {
  const DashboardPage({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Dashboard Estufa IoT'),
        actions: [
          IconButton(
            icon: const Icon(Icons.memory),
            onPressed: () => context.push('/device'),
            tooltip: 'Info do Dispositivo',
          ),
        ],
      ),
      body: ListView(
        padding: const EdgeInsets.all(12),
        children: [
          ModuleCard(
            title: 'Clima',
            subtitle: 'Temperatura, umidade e ventoinha (GPIO 44)',
            icon: Icons.thermostat,
            onTap: () => context.push('/climate'),
          ),
          ModuleCard(
            title: 'Rega',
            subtitle: 'Umidade do solo e irrigação manual',
            icon: Icons.water_drop,
            onTap: () => context.push('/irrigation'),
          ),
          ModuleCard(
            title: 'Câmera',
            subtitle: 'Histórico no Storage e captura sob demanda',
            icon: Icons.photo_camera,
            onTap: () => context.push('/camera'),
          ),
        ],
      ),
    );
  }
}