import 'package:flutter/material.dart';

import '../models/gui_prefs.dart';
import '../state/colors_section.dart';
import '../strings.dart';
import '../theme/colors.dart';
import '../theme/theme.dart';
import 'controls.dart';
import 'settings_form.dart';

/// Colores y accesibilidad (maqueta `Config-colores`): lo que se elige se ve
/// al momento en toda la app.
class ColorsPanel extends StatelessWidget {
  final ColorsSection section;
  const ColorsPanel(this.section, {super.key});

  @override
  Widget build(BuildContext context) {
    final p = section.draft;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      spacing: 24,
      children: [
        SettingsCard(
          title: Strings.theme,
          children: [
            LayoutBuilder(
              builder: (context, box) {
                // Como la maqueta: columnas de 200 px como mínimo.
                const gap = 12.0;
                final columns = ((box.maxWidth + gap) / (200 + gap))
                    .floor()
                    .clamp(1, 3);
                final width = (box.maxWidth - gap * (columns - 1)) / columns;
                return Semantics(
                  label: Strings.theme,
                  container: true,
                  explicitChildNodes: true,
                  child: Wrap(
                    spacing: gap,
                    runSpacing: gap,
                    children: [
                      for (final (choice, name, note) in const [
                        (
                          ThemeChoice.system,
                          Strings.themeSystem,
                          Strings.themeSystemNote,
                        ),
                        (
                          ThemeChoice.dark,
                          Strings.themeDark,
                          Strings.themeDarkNote,
                        ),
                        (
                          ThemeChoice.light,
                          Strings.themeLight,
                          Strings.themeLightNote,
                        ),
                      ])
                        SizedBox(
                          width: width,
                          child: _ThemeCard(
                            choice: choice,
                            name: name,
                            note: note,
                            selected: p.theme == choice,
                            onTap: () => section.set(p.copyWith(theme: choice)),
                          ),
                        ),
                    ],
                  ),
                );
              },
            ),
          ],
        ),
        SettingsCard(
          title: Strings.reading,
          children: [
            Segmented<TextSize>(
              label: Strings.textSize,
              value: p.textSize,
              onChanged: (v) => section.set(p.copyWith(textSize: v)),
              options: const [
                (TextSize.small, Strings.textSmall),
                (TextSize.normal, Strings.textNormal),
                (TextSize.large, Strings.textLarge),
              ],
            ),
            CheckRow(
              value: p.reduceMotion,
              onChanged: (v) => section.set(p.copyWith(reduceMotion: v)),
              title: Strings.reduceMotion,
              note: Strings.reduceMotionNote,
            ),
            CheckRow(
              value: p.highContrast,
              onChanged: (v) => section.set(p.copyWith(highContrast: v)),
              title: Strings.highContrast,
              note: Strings.highContrastNote,
            ),
          ],
        ),
      ],
    );
  }
}

/// Una opción de tema con su miniatura: barra, chat, un globo y el botón.
class _ThemeCard extends StatelessWidget {
  final ThemeChoice choice;
  final String name;
  final String note;
  final bool selected;
  final VoidCallback onTap;

  const _ThemeCard({
    required this.choice,
    required this.name,
    required this.note,
    required this.selected,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    // Sistema muestra el tema que el sistema tiene ahora.
    final light = switch (choice) {
      ThemeChoice.light => true,
      ThemeChoice.dark => false,
      ThemeChoice.system =>
        MediaQuery.platformBrightnessOf(context) == Brightness.light,
    };
    final m = light ? MossColors.light : MossColors.dark;
    return Semantics(
      inMutuallyExclusiveGroup: true,
      checked: selected,
      label: name,
      excludeSemantics: true,
      child: Pressable(
        onPressed: onTap,
        selected: selected,
        color: c.field,
        hoverColor: c.accentSoft,
        radius: 14,
        child: Container(
          padding: const EdgeInsets.all(12),
          decoration: BoxDecoration(
            borderRadius: BorderRadius.circular(14),
            border: Border.all(
              color: selected ? c.accent : c.border,
              width: selected ? 2 : 1,
            ),
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            spacing: 12,
            children: [
              Container(
                height: 88,
                clipBehavior: Clip.antiAlias,
                decoration: BoxDecoration(
                  borderRadius: BorderRadius.circular(8),
                  border: Border.all(color: m.border),
                ),
                child: Row(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    Expanded(flex: 3, child: ColoredBox(color: m.sidebar)),
                    Expanded(
                      flex: 7,
                      child: Container(
                        color: m.background,
                        padding: const EdgeInsets.all(10),
                        child: Column(
                          mainAxisAlignment: MainAxisAlignment.end,
                          crossAxisAlignment: CrossAxisAlignment.start,
                          spacing: 6,
                          children: [
                            _bar(
                              m.accent.withValues(alpha: 0.35),
                              0.55,
                              10,
                              end: true,
                            ),
                            _bar(m.text.withValues(alpha: 0.6), 0.7, 6),
                            _bar(m.text.withValues(alpha: 0.6), 0.45, 6),
                            Align(
                              alignment: Alignment.centerRight,
                              child: Container(
                                width: 16,
                                height: 16,
                                decoration: BoxDecoration(
                                  color: m.accent,
                                  borderRadius: BorderRadius.circular(4),
                                ),
                              ),
                            ),
                          ],
                        ),
                      ),
                    ),
                  ],
                ),
              ),
              Row(
                spacing: 10,
                children: [
                  _Radio(selected: selected),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(name, style: s.option),
                        Text(note, style: s.note),
                      ],
                    ),
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _bar(
    Color color,
    double widthFactor,
    double height, {
    bool end = false,
  }) => Align(
    alignment: end ? Alignment.centerRight : Alignment.centerLeft,
    child: FractionallySizedBox(
      widthFactor: widthFactor,
      child: Container(
        height: height,
        decoration: BoxDecoration(
          color: color,
          borderRadius: BorderRadius.circular(height / 2),
        ),
      ),
    ),
  );
}

/// El círculo de una opción (sin el `Radio` de Material, que trae su propio
/// foco: aquí lo lleva la tarjeta entera).
class _Radio extends StatelessWidget {
  final bool selected;
  const _Radio({required this.selected});

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    return Container(
      width: 18,
      height: 18,
      decoration: BoxDecoration(
        shape: BoxShape.circle,
        border: Border.all(
          color: selected ? c.accent : c.textMuted,
          width: 1.5,
        ),
      ),
      padding: const EdgeInsets.all(3),
      child: selected
          ? DecoratedBox(
              decoration: BoxDecoration(
                color: c.accent,
                shape: BoxShape.circle,
              ),
            )
          : null,
    );
  }
}
