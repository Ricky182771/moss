import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

/// El canal de los runners (`gui/windows/runner/flutter_window.cpp` y
/// `gui/linux/runner/my_application.cc`).
const kWindowChannel = MethodChannel('moss/window');

/// Le dice a la ventana el tema de la app para que su barra de título lo siga:
/// al arrancar y cada vez que cambia, también con Sistema.
class WindowTheme extends StatefulWidget {
  final Widget child;
  const WindowTheme({super.key, required this.child});

  @override
  State<WindowTheme> createState() => _WindowThemeState();
}

class _WindowThemeState extends State<WindowTheme> {
  bool? _dark;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final dark = Theme.of(context).brightness == Brightness.dark;
    if (dark == _dark) return;
    _dark = dark;
    _send(dark);
  }

  Future<void> _send(bool dark) async {
    try {
      await kWindowChannel.invokeMethod<void>('setDark', dark);
    } on MissingPluginException {
      // Sin runner (las pruebas): la barra no es de nadie.
    } on PlatformException {
      // La barra del sistema se queda como está: la app no depende de ella.
    }
  }

  @override
  Widget build(BuildContext context) => widget.child;
}
