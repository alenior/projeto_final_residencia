import 'package:flutter/material.dart';

class CameraPage extends StatelessWidget {
  const CameraPage({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.grey.shade200,

      appBar: AppBar(
        backgroundColor: Colors.grey.shade700,
        foregroundColor: Colors.white,
        title: const Text('Câmera'),
      ),

      body: const Center(
        child: Text(
          'Histórico de imagens (Storage) + Capturar agora',
          textAlign: TextAlign.center,
          style: TextStyle(fontSize: 16, color: Colors.black87),
        ),
      ),
    );
  }
}
