import 'package:flutter/material.dart';

import 'colors.dart';

/// Construye el ThemeData de Moss a partir de los tokens de color
class MossTheme {
  static ThemeData dark() {
    return _buildTheme(MossColors.dark, Brightness.dark);
  }

  static ThemeData light() {
    return _buildTheme(MossColors.light, Brightness.light);
  }

  static ThemeData _buildTheme(MossColors colors, Brightness brightness) {
    const geist = 'Geist';
    const geistMono = 'Geist Mono';

    return ThemeData(
      brightness: brightness,
      extensions: [colors],
      scaffoldBackgroundColor: colors.background,

      // Fuentes
      fontFamily: geist,

      // Colores primarios
      colorScheme: ColorScheme(
        brightness: brightness,
        primary: colors.accent,
        onPrimary: colors.textOnAccent,
        secondary: colors.accent,
        onSecondary: colors.textOnAccent,
        error: colors.error,
        onError: brightness == Brightness.dark
            ? const Color(0xFF0F1411)
            : const Color(0xFFFFFFFF),
        surface: colors.field,
        onSurface: colors.text,
      ),

      // Estilos de texto según DISENO.md
      textTheme: TextTheme(
        // Respuesta del modelo: 15/1.65
        bodyLarge: TextStyle(
          fontFamily: geist,
          fontSize: 15,
          height: 1.65,
          color: colors.text,
        ),
        // Globo del usuario: 15/1.55
        bodyMedium: TextStyle(
          fontFamily: geist,
          fontSize: 15,
          height: 1.55,
          color: colors.text,
        ),
        // Barra lateral: 14
        labelLarge: TextStyle(
          fontFamily: geist,
          fontSize: 14,
          fontWeight: FontWeight.w500,
          color: colors.text,
        ),
        // Grupos (mayúsculas, espaciado 0.06em): 12
        labelMedium: TextStyle(
          fontFamily: geist,
          fontSize: 12,
          fontWeight: FontWeight.w500,
          letterSpacing: 0.06 * 12, // 0.06em
          color: colors.textMuted,
        ),
        // Ayuda: 12
        labelSmall: TextStyle(
          fontFamily: geist,
          fontSize: 12,
          color: colors.textMuted,
        ),
        // Título del encabezado: 17/600
        titleMedium: TextStyle(
          fontFamily: geist,
          fontSize: 17,
          fontWeight: FontWeight.w600,
          color: colors.text,
        ),
        // Pensamiento resumen: 13
        titleSmall: TextStyle(
          fontFamily: geist,
          fontSize: 13,
          color: colors.textMuted,
        ),
        // Pensamiento texto: 14/1.6
        bodySmall: TextStyle(
          fontFamily: geist,
          fontSize: 14,
          height: 1.6,
          color: colors.textMuted,
        ),
        // Código: 13.5 Geist Mono
        displaySmall: TextStyle(
          fontFamily: geistMono,
          fontSize: 13.5,
          height: 1.6,
          color: colors.text,
        ),
      ),

      // Transiciones instantáneas para el cambio de tema
      pageTransitionsTheme: const PageTransitionsTheme(
        builders: {
          TargetPlatform.linux: FadeUpwardsPageTransitionsBuilder(),
          TargetPlatform.windows: FadeUpwardsPageTransitionsBuilder(),
        },
      ),
    );
  }
}
