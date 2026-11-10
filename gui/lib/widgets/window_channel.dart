import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

/// El canal de los runners (`gui/windows/runner/flutter_window.cpp` y
/// `gui/linux/runner/my_application.cc`).
const kWindowChannel = MethodChannel('moss/window');

/// Lo que va y viene con la ventana:
/// - `setDark`: el tema de la app, para que la barra de título lo siga, al
///   arrancar y cada vez que cambia (también con Sistema).
/// - `animationsEnabled` (solo Windows): "Efectos de animación", que Flutter no
///   reporta allá. Se pide al arrancar y el runner avisa si cambia; llega a
///   [builder] para sumarse a Reducir animaciones.
class WindowChannel extends StatefulWidget {
  final Widget Function(BuildContext context, bool systemReducesMotion) builder;
  const WindowChannel({super.key, required this.builder});

  @override
  State<WindowChannel> createState() => _WindowChannelState();
}

class _WindowChannelState extends State<WindowChannel> {
  bool? _dark;
  bool _reducesMotion = false;

  @override
  void initState() {
    super.initState();
    kWindowChannel.setMethodCallHandler((call) async {
      if (call.method == 'animationsEnabled') _animations(call.arguments);
    });
    _call('animationsEnabled').then(_animations);
  }

  @override
  void dispose() {
    kWindowChannel.setMethodCallHandler(null);
    super.dispose();
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final dark = Theme.of(context).brightness == Brightness.dark;
    if (dark == _dark) return;
    _dark = dark;
    _call('setDark', dark);
  }

  void _animations(Object? enabled) {
    if (enabled is bool && mounted) setState(() => _reducesMotion = !enabled);
  }

  Future<Object?> _call(String method, [Object? argument]) async {
    try {
      return await kWindowChannel.invokeMethod<Object?>(method, argument);
    } on MissingPluginException {
      // Sin runner (las pruebas) o sin este método (Linux).
    } on PlatformException {
      // La ventana se queda como está: la app no depende de ella.
    }
    return null;
  }

  @override
  Widget build(BuildContext context) => widget.builder(context, _reducesMotion);
}
