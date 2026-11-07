import 'dart:io';
import 'dart:ui';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import 'frame_stats.dart';
import 'models/gui_prefs.dart';
import 'rpc/client.dart';
import 'screens/home_screen.dart';
import 'screens/error_screen.dart';
import 'state/app_state.dart';
import 'strings.dart';
import 'theme/theme.dart';

void main(List<String> args) {
  if (args.contains('--frame-stats')) startFrameStats();
  runApp(MossApp(args: args));
}

class MossApp extends StatefulWidget {
  final List<String> args;
  const MossApp({super.key, this.args = const []});

  @override
  State<MossApp> createState() => _MossAppState();
}

class _MossAppState extends State<MossApp> {
  /// Lo de Colores y accesibilidad; `Boot` lo lee antes del primer chat.
  final _prefs = ValueNotifier(const GuiPrefs());

  @override
  void dispose() {
    _prefs.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => PrefsApp(
    prefs: _prefs,
    home: Boot(args: widget.args, prefs: _prefs),
  );
}

/// La `MaterialApp` con lo de Colores y accesibilidad: tema, Más contraste,
/// tamaño del texto y Reducir animaciones (que se suma a la del sistema).
class PrefsApp extends StatelessWidget {
  final ValueListenable<GuiPrefs> prefs;
  final Widget home;
  const PrefsApp({super.key, required this.prefs, required this.home});

  @override
  Widget build(BuildContext context) => ValueListenableBuilder(
    valueListenable: prefs,
    builder: (context, p, home) => MaterialApp(
      title: 'Moss',
      theme: MossTheme.light(highContrast: p.highContrast),
      darkTheme: MossTheme.dark(highContrast: p.highContrast),
      themeMode: switch (p.theme) {
        ThemeChoice.system => ThemeMode.system,
        ThemeChoice.dark => ThemeMode.dark,
        ThemeChoice.light => ThemeMode.light,
      },
      // DISENO.md: el cambio de tema es instantáneo.
      themeAnimationDuration: Duration.zero,
      debugShowCheckedModeBanner: false,
      builder: (context, child) {
        final mq = MediaQuery.of(context);
        return MediaQuery(
          data: mq.copyWith(
            disableAnimations: mq.disableAnimations || p.reduceMotion,
            textScaler: TextScaler.linear(
              mq.textScaler.scale(1) * p.textSize.scale,
            ),
          ),
          child: child!,
        );
      },
      home: home,
    ),
    child: home,
  );
}

/// Busca y arranca `moss-rpc`; muestra el chat o la pantalla de error.
class Boot extends StatefulWidget {
  final List<String> args;
  final ValueNotifier<GuiPrefs> prefs;
  const Boot({super.key, required this.args, required this.prefs});

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
      // Antes de dibujar el chat, para que salga ya con su tema y tamaño.
      widget.prefs.value = await loadGuiPrefs(client);
      final app = AppState(
        client,
        prefs: widget.prefs,
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
    if (_app case final app?) return HomeScreen(app);
    return Material(color: Theme.of(context).scaffoldBackgroundColor);
  }
}
