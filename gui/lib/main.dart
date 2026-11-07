import 'dart:io';
import 'dart:ui';

import 'package:flutter/material.dart';

import 'rpc/client.dart';
import 'screens/chat_screen.dart';
import 'screens/error_screen.dart';
import 'state/app_state.dart';
import 'strings.dart';
import 'theme/theme.dart';

void main(List<String> args) {
  runApp(MossApp(args: args));
}

class MossApp extends StatelessWidget {
  final List<String> args;
  const MossApp({super.key, this.args = const []});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Moss',
      theme: MossTheme.light(),
      darkTheme: MossTheme.dark(),
      themeMode: ThemeMode.system,
      // DISENO.md: el cambio de tema es instantáneo.
      themeAnimationDuration: Duration.zero,
      debugShowCheckedModeBanner: false,
      home: Boot(args: args),
    );
  }
}

/// Busca y arranca `moss-rpc`; muestra el chat o la pantalla de error.
class Boot extends StatefulWidget {
  final List<String> args;
  const Boot({super.key, required this.args});

  @override
  State<Boot> createState() => _BootState();
}

class _BootState extends State<Boot> {
  final _stderr = StderrBuffer();
  late final RpcLocation _location = locateRpc(
    widget.args,
    Platform.resolvedExecutable,
    windows: Platform.isWindows,
    exists: (p) => File(p).existsSync(),
  );
  AppState? _app;
  RpcErrorScreen? _error;
  late final AppLifecycleListener _lifecycle;

  @override
  void initState() {
    super.initState();
    // Al cerrar la ventana: shutdown, exit y kill a los 3 s.
    _lifecycle = AppLifecycleListener(
      onExitRequested: () async {
        await _app?.close();
        return AppExitResponse.exit;
      },
    );
    _start();
  }

  Future<void> _start() async {
    if (!_location.found) return _fail(Strings.rpcNotFound);
    RpcClient? client;
    try {
      client = await RpcClient.spawn(_location.path, _stderr);
      final init = await client.initialize();
      final app = AppState(
        client,
        reconnect: () => RpcClient.spawn(_location.path, _stderr),
      );
      await app.load(init['conversation'] as String);
      if (mounted) setState(() => _app = app);
    } on RpcStartError catch (e) {
      await client?.close(); // Que no quede huérfano.
      if (e.otherVersion) {
        _fail(Strings.rpcOtherVersion, Strings.rpcOtherVersionDetail);
      } else if (e.timedOut) {
        _fail(Strings.rpcNotResponding);
      } else {
        _fail(Strings.rpcCannotStart, e.detail);
      }
    } on Object catch (e) {
      await client?.close();
      _fail(Strings.rpcCannotStart, '$e');
    }
  }

  void _fail(String title, [String? detail]) {
    if (!mounted) return;
    setState(
      () => _error = RpcErrorScreen(
        title: title,
        detail: detail,
        path: _location.path,
        stderr: _stderr.lines,
      ),
    );
  }

  @override
  void dispose() {
    _lifecycle.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (_error case final error?) return error;
    if (_app case final app?) return ChatScreen(app);
    return Material(color: Theme.of(context).scaffoldBackgroundColor);
  }
}
