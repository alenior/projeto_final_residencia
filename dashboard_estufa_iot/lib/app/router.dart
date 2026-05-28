import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';
import '../features/dashboard/presentation/dashboard_page.dart';
import '../features/device_info/presentation/device_info_page.dart';
import '../features/climate/presentation/climate_page.dart';
import '../features/irrigation/presentation/irrigation_page.dart';
import '../features/camera/presentation/camera_page.dart';

final routerProvider = Provider<GoRouter>((ref) {
  return GoRouter(
    initialLocation: '/',
    routes: [
      GoRoute(path: '/', builder: (_, __) => const DashboardPage()),
      GoRoute(path: '/device', builder: (_, __) => const DeviceInfoPage()),
      GoRoute(path: '/climate', builder: (_, __) => const ClimatePage()),
      GoRoute(path: '/irrigation', builder: (_, __) => const IrrigationPage()),
      GoRoute(path: '/camera', builder: (_, __) => const CameraPage()),
    ],
  );
});
