import 'package:firebase_core/firebase_core.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'app/app.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized(
    options: DefaultFirebaseOptions.currentPlatform,
  );
  await Firebase.initializeApp(); // use flutterfire configure antes
  runApp(const ProviderScope(child: EstufaApp()));
}
