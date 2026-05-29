import 'package:flutter/material.dart';

class DeviceInfoPage extends StatelessWidget {
  const DeviceInfoPage({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.brown.shade100,

      appBar: AppBar(
        backgroundColor: Colors.green,
        foregroundColor: Colors.white,
        title: const Text('Informações do Dispositivo'),
      ),

      body: const Padding(
        padding: EdgeInsets.all(16),
        child: Text(
          'Exibir aqui dados de boot/status: MAC, UID, memória, reset_cause etc.',
          style: TextStyle(fontSize: 16, color: Colors.black87),
        ),
      ),
    );
  }
}
