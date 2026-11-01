import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import 'theme/theme.dart';
import 'screens/chat_screen.dart';
import 'state/app_state.dart';
import 'rpc/client.dart';
import 'rpc/moss_rpc.dart';

void main() {
  runApp(const MossApp());
}

class MossApp extends StatelessWidget {
  const MossApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Moss',
      theme: MossTheme.light(),
      darkTheme: MossTheme.dark(),
      themeMode: ThemeMode.system,
      home: const SplashScreen(),
      debugShowCheckedModeBanner: false,
    );
  }
}

/// Pantalla de carga que inicializa el cliente RPC
class SplashScreen extends StatefulWidget {
  const SplashScreen({super.key});

  @override
  State<SplashScreen> createState() => _SplashScreenState();
}

class _SplashScreenState extends State<SplashScreen> {
  String? _error;

  @override
  void initState() {
    super.initState();
    _initialize();
  }

  Future<void> _initialize() async {
    try {
      // TODO: Buscar moss-rpc (--rpc junto al ejecutable, no en PATH)
      // Por ahora usamos un path de ejemplo
      final client = RpcClient('moss-rpc');
      final rpc = MossRpc(client);

      await client.start(clientName: 'moss-gui', clientVersion: '0.1.0');

      final appState = AppState(rpc);
      await appState.loadInitialState();

      if (mounted) {
        Navigator.of(context).pushReplacement(
          MaterialPageRoute(
            builder: (_) => ChangeNotifierProvider.value(
              value: appState,
              child: const ChatScreen(),
            ),
          ),
        );
      }
    } catch (e) {
      if (mounted) {
        setState(() {
          _error = e.toString();
        });
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    if (_error != null) {
      return Scaffold(
        body: Center(
          child: Padding(
            padding: const EdgeInsets.all(32),
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                const Icon(Icons.error_outline, size: 64, color: Colors.red),
                const SizedBox(height: 24),
                Text(
                  'Error al inicializar',
                  style: Theme.of(context).textTheme.titleMedium,
                ),
                const SizedBox(height: 12),
                Text(
                  _error!,
                  style: Theme.of(context).textTheme.bodyMedium,
                  textAlign: TextAlign.center,
                ),
              ],
            ),
          ),
        ),
      );
    }

    return const Scaffold(body: Center(child: CircularProgressIndicator()));
  }
}
