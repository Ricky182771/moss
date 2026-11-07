import 'package:flutter/material.dart';

import '../theme/colors.dart';
import '../theme/motion.dart';
import '../theme/theme.dart';
import 'controls.dart';

/// Tarjeta de una sección de Configuración (las maquetas: 24 px de relleno,
/// radio 16, fondo de barra y borde).
class SettingsCard extends StatelessWidget {
  final String? title;

  /// Texto a la derecha del título (tenue) o un botón.
  final Widget? trailing;

  /// Texto debajo del título.
  final String? subtitle;
  final List<Widget> children;
  final double spacing;

  const SettingsCard({
    super.key,
    this.title,
    this.trailing,
    this.subtitle,
    required this.children,
    this.spacing = 20,
  });

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    return Container(
      padding: const EdgeInsets.all(24),
      decoration: BoxDecoration(
        color: c.sidebar,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: c.border),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        spacing: spacing,
        children: [
          if (title != null)
            Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              spacing: 4,
              children: [
                Wrap(
                  alignment: WrapAlignment.spaceBetween,
                  crossAxisAlignment: WrapCrossAlignment.center,
                  spacing: 8,
                  runSpacing: 8,
                  children: [
                    Semantics(
                      header: true,
                      child: Text(title!, style: s.cardTitle),
                    ),
                    ?trailing,
                  ],
                ),
                if (subtitle != null) Text(subtitle!, style: s.note),
              ],
            ),
          ...children,
        ],
      ),
    );
  }
}

/// Casilla con su título y nota; toda la fila la cambia.
class CheckRow extends StatelessWidget {
  final bool value;
  final ValueChanged<bool>? onChanged;
  final String title;
  final String? note;

  const CheckRow({
    super.key,
    required this.value,
    required this.onChanged,
    required this.title,
    this.note,
  });

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final enabled = onChanged != null;
    return MergeSemantics(
      child: GestureDetector(
        onTap: enabled ? () => onChanged!(!value) : null,
        behavior: HitTestBehavior.opaque,
        child: ConstrainedBox(
          constraints: const BoxConstraints(minHeight: 44),
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            spacing: 12,
            children: [
              SizedBox.square(
                dimension: 24,
                child: Checkbox(
                  value: value,
                  onChanged: enabled ? (v) => onChanged!(v ?? false) : null,
                  activeColor: c.accent,
                  checkColor: c.textOnAccent,
                  side: BorderSide(color: c.textMuted, width: 1.5),
                  focusColor: c.accentSoft,
                  materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                ),
              ),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  spacing: 2,
                  children: [
                    Text(title, style: s.option),
                    if (note != null) Text(note!, style: s.note),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// Opciones en una fila (Tamaño del texto): la elegida, en verde lleno.
class Segmented<T> extends StatelessWidget {
  final String label;
  final List<(T, String)> options;
  final T value;
  final ValueChanged<T> onChanged;
  final double maxWidth;

  const Segmented({
    super.key,
    required this.label,
    required this.options,
    required this.value,
    required this.onChanged,
    this.maxWidth = 420,
  });

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      spacing: 8,
      children: [
        Text(label, style: s.label),
        Semantics(
          label: label,
          container: true,
          explicitChildNodes: true,
          child: ConstrainedBox(
            constraints: BoxConstraints(maxWidth: maxWidth),
            child: Container(
              padding: const EdgeInsets.all(4),
              decoration: BoxDecoration(
                color: c.field,
                borderRadius: BorderRadius.circular(12),
                border: Border.all(color: c.border),
              ),
              child: Row(
                spacing: 4,
                children: [
                  for (final (v, text) in options)
                    Expanded(
                      child: Semantics(
                        inMutuallyExclusiveGroup: true,
                        checked: v == value,
                        child: Pressable(
                          onPressed: () => onChanged(v),
                          selected: v == value,
                          color: v == value ? c.accent : null,
                          hoverColor: v == value ? c.accent : null,
                          radius: 8,
                          minHeight: 40,
                          padding: const EdgeInsets.symmetric(horizontal: 8),
                          child: Center(
                            child: Text(
                              text,
                              style: v == value
                                  ? s.row.copyWith(
                                      color: c.textOnAccent,
                                      fontWeight: FontWeight.w600,
                                    )
                                  : s.row,
                            ),
                          ),
                        ),
                      ),
                    ),
                ],
              ),
            ),
          ),
        ),
      ],
    );
  }
}

/// Un error o aviso junto a un campo: aparece con opacidad (DISENO.md →
/// Animaciones: error en un campo, 120 ms), sin mover lo demás de golpe.
class FieldMessage extends StatelessWidget {
  final String? text;
  final bool error;
  const FieldMessage(this.text, {super.key, this.error = true});

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    return AnimatedSwitcher(
      duration: Motion.of(context).fieldError,
      switchInCurve: Motion.enter,
      switchOutCurve: Motion.enter,
      layoutBuilder: (current, previous) => Stack(
        alignment: Alignment.topLeft,
        children: [...previous, ?current],
      ),
      child: text == null
          ? const SizedBox.shrink()
          : Semantics(
              key: ValueKey(text),
              liveRegion: error,
              child: Text(
                text!,
                style: error ? s.note.copyWith(color: c.error) : s.note,
              ),
            ),
    );
  }
}
