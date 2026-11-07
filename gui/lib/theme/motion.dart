import 'package:flutter/widgets.dart';

/// Duraciones y curvas de DISENO.md → Animaciones. Ninguna otra parte de la
/// app escribe una duración.
class Motion {
  const Motion._(this.reduced);

  /// Animaciones apagadas: todo dura 0 ms y lo que se repite queda fijo.
  final bool reduced;

  /// Lee `MediaQuery.disableAnimations`. En G3 se suma aquí la opción
  /// "Reducir animaciones" de `settings.gui`.
  static Motion of(BuildContext context) =>
      Motion._(MediaQuery.disableAnimationsOf(context));

  Duration _ms(int ms) => reduced ? Duration.zero : Duration(milliseconds: ms);

  Duration get hover => _ms(100);
  Duration get tooltip => _ms(120);
  Duration get menuOpen => _ms(140);
  Duration get menuClose => _ms(100);
  Duration get searchDialog => _ms(180);
  Duration get screenFade => _ms(160);
  Duration get sidebar => _ms(200);
  Duration get inputGrow => _ms(140);
  Duration get messageIn => _ms(160);
  Duration get word => _ms(120);
  Duration get thinkingFold => _ms(180);
  Duration get thinkingAutoFold => _ms(200);
  Duration get copySwap => _ms(120);
  Duration get fieldError => _ms(120);
  Duration get toast => _ms(160);
  Duration get cursorBlink => _ms(1000);
  Duration get thinkingPulse => _ms(1200);

  static const Curve enter = Curves.easeOutCubic;
  static const Curve exit = Curves.easeInCubic;

  // Tiempos de espera, no animaciones: Reducir animaciones no los cambia.
  static const Duration tooltipDelay = Duration(milliseconds: 400);
  static const Duration toastVisible = Duration(seconds: 4);
  static const Duration copiedVisible = Duration(seconds: 2);

  /// PROTOCOL.md §3: `shutdown`, `exit` y `kill` si no sale en 3 s.
  static const Duration rpcExit = Duration(seconds: 3);
}
