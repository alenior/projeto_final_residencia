import 'package:firebase_auth/firebase_auth.dart';
import 'package:flutter/material.dart';

/// Garante que as telas do dashboard sejam exibidas apenas para usuários
/// autenticados.
///
/// Nesta fase do projeto, o gate oferece autenticação anônima para facilitar os
/// testes de campo com o ESP32 sem abrir as regras do Firebase para acesso
/// público. Quando o app ganhar cadastro/login definitivo, basta substituir a
/// ação de entrada por email/senha, Google Sign-In ou outro provedor.
class AuthGate extends StatelessWidget {
  final Widget child;

  const AuthGate({super.key, required this.child});

  @override
  Widget build(BuildContext context) {
    return StreamBuilder<User?>(
      stream: FirebaseAuth.instance.authStateChanges(),
      builder: (context, snapshot) {
        if (snapshot.connectionState == ConnectionState.waiting) {
          return const _AuthLoadingView();
        }

        if (snapshot.hasData) {
          return child;
        }

        return const _AnonymousSignInView();
      },
    );
  }
}

class _AuthLoadingView extends StatelessWidget {
  const _AuthLoadingView();

  @override
  Widget build(BuildContext context) {
    return const Scaffold(body: Center(child: CircularProgressIndicator()));
  }
}

class _AnonymousSignInView extends StatefulWidget {
  const _AnonymousSignInView();

  @override
  State<_AnonymousSignInView> createState() => _AnonymousSignInViewState();
}

class _AnonymousSignInViewState extends State<_AnonymousSignInView> {
  bool _loading = false;
  String? _error;

  Future<void> _signIn() async {
    setState(() {
      _loading = true;
      _error = null;
    });

    try {
      await FirebaseAuth.instance.signInAnonymously();
    } on FirebaseAuthException catch (error) {
      if (!mounted) return;
      setState(() => _error = error.message ?? error.code);
    } catch (error) {
      if (!mounted) return;
      setState(() => _error = error.toString());
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;

    return Scaffold(
      body: Center(
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 420),
          child: Card(
            margin: const EdgeInsets.all(24),
            child: Padding(
              padding: const EdgeInsets.all(24),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Icon(Icons.eco, size: 56, color: colorScheme.primary),
                  const SizedBox(height: 16),
                  Text(
                    'Dashboard Estufa IoT',
                    textAlign: TextAlign.center,
                    style: Theme.of(context).textTheme.headlineSmall,
                  ),
                  const SizedBox(height: 8),
                  const Text(
                    'Entre para monitorar sensores, consultar histórico e enviar comandos ao ESP32.',
                    textAlign: TextAlign.center,
                  ),
                  if (_error != null) ...[
                    const SizedBox(height: 16),
                    Text(
                      _error!,
                      textAlign: TextAlign.center,
                      style: TextStyle(color: colorScheme.error),
                    ),
                  ],
                  const SizedBox(height: 24),
                  FilledButton.icon(
                    onPressed: _loading ? null : _signIn,
                    icon: _loading
                        ? const SizedBox.square(
                            dimension: 18,
                            child: CircularProgressIndicator(strokeWidth: 2),
                          )
                        : const Icon(Icons.login),
                    label: const Text('Entrar para testes'),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}
