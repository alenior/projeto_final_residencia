import 'package:flutter/material.dart';

class ClimatePage extends StatelessWidget {
  const ClimatePage({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Clima')),
      body: const Center(
        child: Text('Tela de clima: status térmico, aquecimento e ventoinha GPIO 44'),
      ),
    );
  }
}