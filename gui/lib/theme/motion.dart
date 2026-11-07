import 'package:flutter/widgets.dart';

/// Duraciones y curvas de DISENO.md → Animaciones. Ninguna otra parte de la
/// app escribe una duración.
class Motion {
  const Motion._(this.reduced);

  /// Animaciones apagadas: todo dura 0 ms y lo que se repite queda fijo.
  final bool reduced;

  /// Lee `MediaQuery.disableAnimations`, que `PrefsApp` (main.dart) ya
  /// enciende también con "Reducir animaciones" de Colores y accesibilidad.
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

  /// Salir un poco más rápido que entrar (DISENO.md → Animaciones, regla 1).
  Duration get sidebarClose => _ms(160);
  Duration get inputGrow => _ms(140);
  Duration get messageIn => _ms(160);
  Duration get word => _ms(120);
  Duration get thinkingFold => _ms(180);

  /// Avanzado en Configuración del modelo: como el bloque Pensamiento.
  Duration get fold => _ms(180);
  Duration get thinkingAutoFold => _ms(200);
  Duration get copySwap => _ms(120);
  Duration get fieldError => _ms(120);
  Duration get toast => _ms(160);
  Duration get cursorBlink => _ms(1000);
  Duration get thinkingPulse => _ms(1200);

  /// La curva de todas las animaciones; al salir, la duración es más corta.
  static const Curve enter = Curves.easeOutCubic;

  // Tiempos de espera, no animaciones: Reducir animaciones no los cambia.
  static const Duration tooltipDelay = Duration(milliseconds: 400);
  static const Duration toastVisible = Duration(seconds: 4);
  static const Duration copiedVisible = Duration(seconds: 2);

  /// "Cargando modelos…" solo si tarda más que esto.
  static const Duration loadingDelay = Duration(milliseconds: 300);
}
