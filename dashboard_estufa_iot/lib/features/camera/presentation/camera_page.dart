import 'package:flutter/material.dart';

class CameraPage extends StatelessWidget {
  const CameraPage({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Câmera')),
      body: const Center(
        child: Text('Histórico de imagens (Storage) + Capturar agora'),
      ),
    );
  }
}