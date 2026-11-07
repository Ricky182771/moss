import 'package:flutter/material.dart';

import 'colors.dart';

const _geist = 'Geist';
const _mono = 'Geist Mono';

/// Estilos de texto con los tamaños de las maquetas. Ninguna otra parte de la
/// app escribe un tamaño de letra.
class MossText extends ThemeExtension<MossText> {
  /// Respuesta del modelo: 15/1.65.
  final TextStyle answer;

  /// Globo del usuario: 15/1.55.
  final TextStyle bubble;

  /// Caja del mensaje: 15/1.5.
  final TextStyle input;

  /// Botones de la barra y del encabezado: 14/500.
  final TextStyle nav;

  /// Fila de la lista de chats: 14.
  final TextStyle row;

  /// Fila del chat abierto: 14/500.
  final TextStyle rowOpen;

  /// Negrita del Markdown: solo el peso (600), sobre el estilo del párrafo.
  final TextStyle strong;

  /// Grupo de la lista: 12/500, mayúsculas con 0.06em.
  final TextStyle group;

  /// Atajo junto a un botón (Ctrl+N): Geist Mono 12.
  final TextStyle kbd;

  /// `moss_` del logotipo: Geist Mono 20/500.
  final TextStyle logotype;

  /// Título del encabezado: 17/600.
  final TextStyle title;

  /// "¿En qué te ayudo?": 30/600.
  final TextStyle heading;

  /// Bloque de código: Geist Mono 13.5/1.6.
  final TextStyle code;

  /// Código en línea: Geist Mono 13.
  final TextStyle inlineCode;

  /// Lenguaje del bloque de código: Geist Mono 12.
  final TextStyle codeLabel;

  /// Botón chico (Copiar del bloque): 13.
  final TextStyle smallButton;

  /// Línea "Pensamiento · N palabras": 13.
  final TextStyle thoughtSummary;

  /// Texto del pensamiento: 14/1.6.
  final TextStyle thought;

  /// Ayuda bajo la caja y avisos: 12.
  final TextStyle help;

  /// Etiqueta de un botón de ícono: 12/500.
  final TextStyle tooltip;

  /// Aviso corto: 14.
  final TextStyle toast;

  /// Modelo en la lista de Opciones: Geist Mono 13.
  final TextStyle model;

  /// Pastilla "Búsqueda web": 13/500 en el color de código.
  final TextStyle pill;

  /// Botón verde lleno (Guardar): 14/600.
  final TextStyle primaryButton;

  /// "Configuración" junto al logo: 18/600.
  final TextStyle brand;

  /// Título de una sección de Configuración: 28/600.
  final TextStyle pageTitle;

  /// Texto bajo el título de la sección: 15/1.5, tenue.
  final TextStyle lead;

  /// Título de una tarjeta: 16/600.
  final TextStyle cardTitle;

  /// Etiqueta de un campo: 14/500.
  final TextStyle label;

  /// Nombre de una opción (tema, casilla): 15/500.
  final TextStyle option;

  /// Texto de un campo: 15.
  final TextStyle field;

  /// Campo de URL, key o JSON: Geist Mono 15.
  final TextStyle fieldMono;

  /// Ayuda bajo un campo y notas: 13/1.5, tenue.
  final TextStyle note;

  /// Vista previa de la petición: Geist Mono 13/1.6.
  final TextStyle preview;

  const MossText({
    required this.answer,
    required this.bubble,
    required this.input,
    required this.nav,
    required this.row,
    required this.rowOpen,
    required this.strong,
    required this.group,
    required this.kbd,
    required this.logotype,
    required this.title,
    required this.heading,
    required this.code,
    required this.inlineCode,
    required this.codeLabel,
    required this.smallButton,
    required this.thoughtSummary,
    required this.thought,
    required this.help,
    required this.tooltip,
    required this.toast,
    required this.model,
    required this.pill,
    required this.primaryButton,
    required this.brand,
    required this.pageTitle,
    required this.lead,
    required this.cardTitle,
    required this.label,
    required this.option,
    required this.field,
    required this.fieldMono,
    required this.note,
    required this.preview,
  });

  factory MossText.of(MossColors c) {
    TextStyle geist(double size, {double? height, FontWeight? weight}) =>
        TextStyle(
          fontFamily: _geist,
          fontSize: size,
          height: height,
          fontWeight: weight ?? FontWeight.w400,
          color: c.text,
        );
    TextStyle mono(double size, {double? height, FontWeight? weight}) =>
        geist(size, height: height, weight: weight).copyWith(fontFamily: _mono);

    return MossText(
      answer: geist(15, height: 1.65),
      bubble: geist(15, height: 1.55),
      input: geist(15, height: 1.5),
      nav: geist(14, weight: FontWeight.w500),
      row: geist(14),
      rowOpen: geist(14, weight: FontWeight.w500),
      strong: const TextStyle(fontWeight: FontWeight.w600),
      group: geist(
        12,
        weight: FontWeight.w500,
      ).copyWith(letterSpacing: 12 * 0.06, color: c.textMuted),
      kbd: mono(12).copyWith(color: c.textMuted),
      logotype: mono(
        20,
        weight: FontWeight.w500,
      ).copyWith(letterSpacing: 20 * -0.02),
      title: geist(17, weight: FontWeight.w600),
      heading: geist(
        30,
        weight: FontWeight.w600,
      ).copyWith(letterSpacing: 30 * -0.02),
      code: mono(13.5, height: 1.6),
      inlineCode: mono(13).copyWith(color: c.code),
      codeLabel: mono(12).copyWith(color: c.textMuted),
      smallButton: geist(13).copyWith(color: c.textMuted),
      thoughtSummary: geist(13).copyWith(color: c.textMuted),
      thought: geist(14, height: 1.6).copyWith(color: c.textMuted),
      help: geist(12).copyWith(color: c.textMuted),
      tooltip: geist(12, weight: FontWeight.w500).copyWith(color: c.background),
      toast: geist(14).copyWith(color: c.background),
      model: mono(13),
      pill: geist(13, weight: FontWeight.w500).copyWith(color: c.code),
      primaryButton: geist(14, weight: FontWeight.w600),
      brand: geist(18, weight: FontWeight.w600),
      pageTitle: geist(
        28,
        weight: FontWeight.w600,
      ).copyWith(letterSpacing: 28 * -0.02),
      lead: geist(15, height: 1.5).copyWith(color: c.textMuted),
      cardTitle: geist(16, weight: FontWeight.w600),
      label: geist(14, weight: FontWeight.w500),
      option: geist(15, weight: FontWeight.w500),
      field: geist(15),
      fieldMono: mono(15),
      note: geist(13, height: 1.5).copyWith(color: c.textMuted),
      preview: mono(13, height: 1.6),
    );
  }

  @override
  MossText copyWith() => this;

  @override
  MossText lerp(ThemeExtension<MossText>? other, double t) =>
      other is MossText && t >= 0.5 ? other : this;
}

extension MossTextExtension on BuildContext {
  MossText get styles => Theme.of(this).extension<MossText>()!;
}

/// `ThemeData` armado a partir de los tokens.
class MossTheme {
  static ThemeData dark({bool highContrast = false}) => _build(
    highContrast ? MossColors.darkHigh : MossColors.dark,
    Brightness.dark,
  );
  static ThemeData light({bool highContrast = false}) => _build(
    highContrast ? MossColors.lightHigh : MossColors.light,
    Brightness.light,
  );

  static ThemeData _build(MossColors c, Brightness brightness) {
    final text = MossText.of(c);
    return ThemeData(
      brightness: brightness,
      fontFamily: _geist,
      extensions: [c, text],
      scaffoldBackgroundColor: c.background,
      colorScheme: ColorScheme(
        brightness: brightness,
        primary: c.accent,
        onPrimary: c.textOnAccent,
        secondary: c.accent,
        onSecondary: c.textOnAccent,
        error: c.error,
        onError: c.background,
        surface: c.background,
        onSurface: c.text,
      ),
      textTheme: TextTheme(bodyMedium: text.answer),
      textSelectionTheme: TextSelectionThemeData(
        cursorColor: c.accent,
        selectionColor: c.accent.withValues(alpha: 0.35),
      ),
      scrollbarTheme: ScrollbarThemeData(
        thumbColor: WidgetStatePropertyAll(c.border),
      ),
      focusColor: c.accentSoft,
      hoverColor: c.accentSoft,
      splashFactory: NoSplash.splashFactory,
      highlightColor: Colors.transparent,
    );
  }
}
