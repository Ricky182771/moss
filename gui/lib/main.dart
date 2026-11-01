import 'package:flutter/material.dart';

import 'theme/theme.dart';
import 'widgets/logo.dart';

void main() {
  runApp(const MossApp());
}

class MossApp extends StatelessWidget {
  const MossApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Moss',
      debugShowCheckedModeBanner: false,
      themeMode: ThemeMode.system,
      theme: MossTheme.light(),
      darkTheme: MossTheme.dark(),
      home: const ChatScreen(),
    );
  }
}

class ChatScreen extends StatelessWidget {
  const ChatScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            const MossLogo(size: 64),
            const SizedBox(height: 24),
            Text('Moss GUI', style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(height: 8),
            Text(
              'Tema ${Theme.of(context).brightness == Brightness.dark ? "oscuro" : "claro"}',
              style: Theme.of(context).textTheme.bodyMedium,
            ),
          ],
        ),
      ),
    );
  }
}
