import 'package:flutter/material.dart';

/// Tokens de colores de Moss según DISENO.md
class MossColors extends ThemeExtension<MossColors> {
  final Color background;
  final Color sidebar;
  final Color bar;
  final Color field;
  final Color fieldDisabled;
  final Color border;
  final Color text;
  final Color textMuted;
  final Color accent;
  final Color textOnAccent;
  final Color accentSoft;
  final Color code;
  final Color codeBg;
  final Color error;
  final Color errorBg;
  final Color userBubble;

  const MossColors({
    required this.background,
    required this.sidebar,
    required this.bar,
    required this.field,
    required this.fieldDisabled,
    required this.border,
    required this.text,
    required this.textMuted,
    required this.accent,
    required this.textOnAccent,
    required this.accentSoft,
    required this.code,
    required this.codeBg,
    required this.error,
    required this.errorBg,
    required this.userBubble,
  });

  /// Tema oscuro
  static const dark = MossColors(
    background: Color(0xFF0F1411),
    sidebar: Color(0xFF161D18),
    bar: Color(0xFF161D18),
    field: Color(0xFF1E2721),
    fieldDisabled: Color(0xFF1A2420),
    border: Color(0xFF2A352D),
    text: Color(0xFFE6EDE7),
    textMuted: Color(0xFF9AA89E),
    accent: Color(0xFF8DBF5F),
    textOnAccent: Color(0xFF0F1411),
    accentSoft: Color(0xFF22301F),
    code: Color(0xFFB5D395),
    codeBg: Color(0xFF1A2420),
    error: Color(0xFFE8836B),
    errorBg: Color(0xFF2E1F1C),
    userBubble: Color(0xFF1E2721),
  );

  /// Tema claro
  static const light = MossColors(
    background: Color(0xFFFFFFFF),
    sidebar: Color(0xFFF6F8F3),
    bar: Color(0xFFF6F8F3),
    field: Color(0xFFEDF1E8),
    fieldDisabled: Color(0xFFF0F3EB),
    border: Color(0xFFD5DDD0),
    text: Color(0xFF17201A),
    textMuted: Color(0xFF56645A),
    accent: Color(0xFF3F6B2A),
    textOnAccent: Color(0xFFFFFFFF),
    accentSoft: Color(0xFFE3ECD9),
    code: Color(0xFF2F5221),
    codeBg: Color(0xFFF0F3EB),
    error: Color(0xFFB3402A),
    errorBg: Color(0xFFFCEBE7),
    userBubble: Color(0xFFEDF1E8),
  );

  @override
  MossColors copyWith({
    Color? background,
    Color? sidebar,
    Color? bar,
    Color? field,
    Color? fieldDisabled,
    Color? border,
    Color? text,
    Color? textMuted,
    Color? accent,
    Color? textOnAccent,
    Color? accentSoft,
    Color? code,
    Color? codeBg,
    Color? error,
    Color? errorBg,
    Color? userBubble,
  }) {
    return MossColors(
      background: background ?? this.background,
      sidebar: sidebar ?? this.sidebar,
      bar: bar ?? this.bar,
      field: field ?? this.field,
      fieldDisabled: fieldDisabled ?? this.fieldDisabled,
      border: border ?? this.border,
      text: text ?? this.text,
      textMuted: textMuted ?? this.textMuted,
      accent: accent ?? this.accent,
      textOnAccent: textOnAccent ?? this.textOnAccent,
      accentSoft: accentSoft ?? this.accentSoft,
      code: code ?? this.code,
      codeBg: codeBg ?? this.codeBg,
      error: error ?? this.error,
      errorBg: errorBg ?? this.errorBg,
      userBubble: userBubble ?? this.userBubble,
    );
  }

  @override
  MossColors lerp(ThemeExtension<MossColors>? other, double t) {
    if (other is! MossColors) {
      return this;
    }
    return MossColors(
      background: Color.lerp(background, other.background, t)!,
      sidebar: Color.lerp(sidebar, other.sidebar, t)!,
      bar: Color.lerp(bar, other.bar, t)!,
      field: Color.lerp(field, other.field, t)!,
      fieldDisabled: Color.lerp(fieldDisabled, other.fieldDisabled, t)!,
      border: Color.lerp(border, other.border, t)!,
      text: Color.lerp(text, other.text, t)!,
      textMuted: Color.lerp(textMuted, other.textMuted, t)!,
      accent: Color.lerp(accent, other.accent, t)!,
      textOnAccent: Color.lerp(textOnAccent, other.textOnAccent, t)!,
      accentSoft: Color.lerp(accentSoft, other.accentSoft, t)!,
      code: Color.lerp(code, other.code, t)!,
      codeBg: Color.lerp(codeBg, other.codeBg, t)!,
      error: Color.lerp(error, other.error, t)!,
      errorBg: Color.lerp(errorBg, other.errorBg, t)!,
      userBubble: Color.lerp(userBubble, other.userBubble, t)!,
    );
  }
}

/// Extensión para acceder fácilmente a los colores
extension MossColorsExtension on BuildContext {
  MossColors get colors => Theme.of(this).extension<MossColors>()!;
}
