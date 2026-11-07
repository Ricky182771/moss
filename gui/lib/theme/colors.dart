import 'package:flutter/material.dart';

/// Tokens de color de DISENO.md → Colores, en oscuro y claro.
class MossColors extends ThemeExtension<MossColors> {
  /// Fondo del chat.
  final Color background;

  /// Barra lateral y tarjetas.
  final Color sidebar;

  /// Campos y bloques de código.
  final Color field;
  final Color border;
  final Color text;
  final Color textMuted;
  final Color accent;
  final Color textOnAccent;

  /// Globo del usuario, elemento activo y hover.
  final Color accentSoft;

  /// Código en línea y palabras clave.
  final Color code;
  final Color error;

  /// Sombra de las ventanas que flotan (Opciones, menús).
  final Color shadow;

  /// Fondo oscurecido detrás de una ventana (Buscar chats).
  final Color scrim;

  const MossColors({
    required this.background,
    required this.sidebar,
    required this.field,
    required this.border,
    required this.text,
    required this.textMuted,
    required this.accent,
    required this.textOnAccent,
    required this.accentSoft,
    required this.code,
    required this.error,
    required this.shadow,
    required this.scrim,
  });

  static const dark = MossColors(
    background: Color(0xFF0F1411),
    sidebar: Color(0xFF161D18),
    field: Color(0xFF1E2721),
    border: Color(0xFF2A352D),
    text: Color(0xFFE6EDE7),
    textMuted: Color(0xFF9AA89E),
    accent: Color(0xFF8DBF5F),
    textOnAccent: Color(0xFF0F1411),
    accentSoft: Color(0xFF22301F),
    code: Color(0xFFB5D395),
    error: Color(0xFFE8836B),
    shadow: Color(0x73000000), // rgba(0,0,0,.45) de las maquetas
    scrim: Color(0x9E050806), // rgba(5,8,6,.62) de Config-registro
  );

  static const light = MossColors(
    background: Color(0xFFFFFFFF),
    sidebar: Color(0xFFF6F8F3),
    field: Color(0xFFEDF1E8),
    border: Color(0xFFD5DDD0),
    text: Color(0xFF17201A),
    textMuted: Color(0xFF56645A),
    accent: Color(0xFF3F6B2A),
    textOnAccent: Color(0xFFFFFFFF),
    accentSoft: Color(0xFFE3ECD9),
    code: Color(0xFF2F5221),
    error: Color(0xFFB3402A),
    // Las maquetas claras no tienen ventanas flotantes: el texto, más suave.
    shadow: Color(0x2917201A),
    scrim: Color(0x4D17201A),
  );

  @override
  MossColors copyWith() => this;

  // El cambio de tema es instantáneo (DISENO.md → Animaciones).
  @override
  MossColors lerp(ThemeExtension<MossColors>? other, double t) =>
      other is MossColors && t >= 0.5 ? other : this;
}

extension MossColorsExtension on BuildContext {
  MossColors get colors => Theme.of(this).extension<MossColors>()!;
}
