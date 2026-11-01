import 'package:flutter/material.dart';

/// Duraciones y curvas de animación según DISENO.md
class Motion {
  // Duraciones
  static const Duration hover = Duration(milliseconds: 100);
  static const Duration tooltip = Duration(milliseconds: 120);
  static const Duration tooltipDelay = Duration(milliseconds: 400);
  static const Duration menu = Duration(milliseconds: 140);
  static const Duration menuClose = Duration(milliseconds: 100);
  static const Duration searchDialog = Duration(milliseconds: 180);
  static const Duration screenTransition = Duration(milliseconds: 160);
  static const Duration sidebarToggle = Duration(milliseconds: 200);
  static const Duration inputExpand = Duration(milliseconds: 140);
  static const Duration messageAppear = Duration(milliseconds: 160);
  static const Duration wordFadeIn = Duration(milliseconds: 120);
  static const Duration thinkingFold = Duration(milliseconds: 180);
  static const Duration thinkingAutoFold = Duration(milliseconds: 200);
  static const Duration buttonTextSwap = Duration(milliseconds: 120);
  static const Duration errorAppear = Duration(milliseconds: 120);
  static const Duration snackbar = Duration(milliseconds: 160);
  static const Duration snackbarVisible = Duration(seconds: 4);
  static const Duration cursorBlink = Duration(milliseconds: 1000);
  static const Duration thinkingPulse = Duration(milliseconds: 1200);

  // Curvas
  static const Curve easeOut = Curves.easeOutCubic;
  static const Curve easeIn = Curves.easeInCubic;

  /// Devuelve las duraciones respetando la configuración de accesibilidad
  /// (cuando el usuario tiene "Reducir animaciones" activado, todo es 0ms)
  static Duration duration(BuildContext context, Duration base) {
    if (MediaQuery.of(context).disableAnimations) {
      return Duration.zero;
    }
    return base;
  }

  /// Acceso conveniente a través del contexto
  static Motion of(BuildContext context) {
    return Motion();
  }

  // Métodos de conveniencia para cada duración
  Duration hoverDuration(BuildContext context) => duration(context, hover);
  Duration tooltipDuration(BuildContext context) => duration(context, tooltip);
  Duration menuDuration(BuildContext context) => duration(context, menu);
  Duration menuCloseDuration(BuildContext context) =>
      duration(context, menuClose);
  Duration searchDialogDuration(BuildContext context) =>
      duration(context, searchDialog);
  Duration screenTransitionDuration(BuildContext context) =>
      duration(context, screenTransition);
  Duration sidebarToggleDuration(BuildContext context) =>
      duration(context, sidebarToggle);
  Duration inputExpandDuration(BuildContext context) =>
      duration(context, inputExpand);
  Duration messageAppearDuration(BuildContext context) =>
      duration(context, messageAppear);
  Duration wordFadeInDuration(BuildContext context) =>
      duration(context, wordFadeIn);
  Duration thinkingFoldDuration(BuildContext context) =>
      duration(context, thinkingFold);
  Duration thinkingAutoFoldDuration(BuildContext context) =>
      duration(context, thinkingAutoFold);
  Duration buttonTextSwapDuration(BuildContext context) =>
      duration(context, buttonTextSwap);
  Duration errorAppearDuration(BuildContext context) =>
      duration(context, errorAppear);
  Duration snackbarDuration(BuildContext context) =>
      duration(context, snackbar);
}
