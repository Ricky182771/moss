/// Preferencias propias de la GUI (Colores y accesibilidad), guardadas con
/// `settings.gui.set` como un objeto con `version: 1` (PROTOCOL.md §6.7:
/// el servidor no lo interpreta).
library;

enum ThemeChoice { system, dark, light }

/// Los tres pasos de la maqueta `Config-colores`.
enum TextSize {
  small(0.9),
  normal(1.0),
  large(1.2);

  final double scale;
  const TextSize(this.scale);
}

class GuiPrefs {
  static const version = 1;

  final ThemeChoice theme;
  final TextSize textSize;
  final bool reduceMotion;
  final bool highContrast;

  const GuiPrefs({
    this.theme = ThemeChoice.system,
    this.textSize = TextSize.normal,
    this.reduceMotion = false,
    this.highContrast = false,
  });

  /// Lo de `settings.gui.get`. Una versión que no es 1 o un valor que no se
  /// conoce usan el predeterminado, sin romper el arranque.
  factory GuiPrefs.fromJson(Object? json) {
    if (json is! Map || json['version'] != version) return const GuiPrefs();
    T pick<T extends Enum>(List<T> values, Object? name, T fallback) =>
        values.asNameMap()[name] ?? fallback;
    bool flag(Object? v) => v is bool && v;
    return GuiPrefs(
      theme: pick(ThemeChoice.values, json['theme'], ThemeChoice.system),
      textSize: pick(TextSize.values, json['text_size'], TextSize.normal),
      reduceMotion: flag(json['reduce_motion']),
      highContrast: flag(json['high_contrast']),
    );
  }

  Map<String, Object> toJson() => {
    'version': version,
    'theme': theme.name,
    'text_size': textSize.name,
    'reduce_motion': reduceMotion,
    'high_contrast': highContrast,
  };

  GuiPrefs copyWith({
    ThemeChoice? theme,
    TextSize? textSize,
    bool? reduceMotion,
    bool? highContrast,
  }) => GuiPrefs(
    theme: theme ?? this.theme,
    textSize: textSize ?? this.textSize,
    reduceMotion: reduceMotion ?? this.reduceMotion,
    highContrast: highContrast ?? this.highContrast,
  );

  @override
  bool operator ==(Object other) =>
      other is GuiPrefs &&
      other.theme == theme &&
      other.textSize == textSize &&
      other.reduceMotion == reduceMotion &&
      other.highContrast == highContrast;

  @override
  int get hashCode => Object.hash(theme, textSize, reduceMotion, highContrast);
}
