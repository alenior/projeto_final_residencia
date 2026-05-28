import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'router.dart';
import 'theme.dart';

class EstufaApp extends ConsumerWidget {
  const EstufaApp({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final router = ref.watch(routerProvider);
    return MaterialApp.router(
      title: 'Dashboard Estufa IoT',
      theme: buildEstufaTheme(),
      routerConfig: router,
      debugShowCheckedModeBanner: false,
    );
  }
}