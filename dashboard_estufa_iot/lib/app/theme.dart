import 'package:flutter/material.dart';

ThemeData buildEstufaTheme() {
  const green = Color(0xFF2E7D32);
  const brown = Color(0xFF6D4C41);
  const blue = Color(0xFF0288D1);

  final scheme = ColorScheme.fromSeed(
    seedColor: green,
    primary: green,
    secondary: blue,
    tertiary: brown,
    brightness: Brightness.light,
  );

  return ThemeData(
    useMaterial3: true,
    colorScheme: scheme,
    scaffoldBackgroundColor: const Color(0xFFF6F8F3),
    cardTheme: const CardTheme(
      elevation: 2,
      margin: EdgeInsets.symmetric(vertical: 8, horizontal: 4),
    ),
  );
}