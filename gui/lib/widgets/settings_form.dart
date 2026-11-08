import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../strings.dart';
import '../theme/colors.dart';
import '../theme/motion.dart';
import '../theme/theme.dart';
import 'controls.dart';
import 'indicators.dart';
import 'list_keys.dart';
import 'popover.dart';

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

/// El círculo de una opción (sin el `Radio` de Material, que trae su propio
/// foco: aquí lo lleva la fila o la tarjeta entera).
class RadioMark extends StatelessWidget {
  final bool selected;
  const RadioMark({super.key, required this.selected});

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

/// Casilla con su título y nota; toda la fila la cambia y lleva el foco (con
/// el anillo de `Pressable`, más grueso con Más contraste).
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
    return Semantics(
      checked: value,
      child: Pressable(
        onPressed: enabled ? () => onChanged!(!value) : null,
        minHeight: 44,
        padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 6),
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          spacing: 12,
          children: [
            Container(
              width: 18,
              height: 18,
              margin: const EdgeInsets.only(top: 2),
              decoration: BoxDecoration(
                color: value ? c.accent : null,
                borderRadius: BorderRadius.circular(4),
                border: Border.all(
                  color: value ? c.accent : c.textMuted,
                  width: 1.5,
                ),
              ),
              child: value
                  ? Icon(Icons.check_rounded, size: 14, color: c.textOnAccent)
                  : null,
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
                                  ? s.primaryButton.copyWith(
                                      color: c.textOnAccent,
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

/// Un campo con su etiqueta, deshabilitado con la razón (`Fijado por
/// CHAT_…`), su ayuda y su error.
class LabeledField extends StatelessWidget {
  final String label;
  final TextEditingController controller;
  final ValueChanged<String>? onChanged;
  final bool mono;
  final bool enabled;
  final String? lockedBy;
  final String? help;
  final String? error;
  final String? hint;
  final FocusNode? focusNode;
  final Widget? trailing;

  const LabeledField({
    super.key,
    required this.label,
    required this.controller,
    this.onChanged,
    this.mono = false,
    this.enabled = true,
    this.lockedBy,
    this.help,
    this.error,
    this.hint,
    this.focusNode,
    this.trailing,
  });

  @override
  Widget build(BuildContext context) {
    final s = context.styles;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      spacing: 8,
      children: [
        Text(label, style: s.label),
        Row(
          spacing: 12,
          children: [
            Expanded(
              child: FieldBox(
                controller: controller,
                label: label,
                onChanged: onChanged,
                mono: mono,
                enabled: enabled && lockedBy == null,
                hint: hint,
                focusNode: focusNode,
                invalid: error != null,
              ),
            ),
            ?trailing,
          ],
        ),
        if (lockedBy != null) Text(Strings.lockedBy(lockedBy!), style: s.note),
        if (help != null) Text(help!, style: s.note),
        FieldMessage(error),
      ],
    );
  }
}

/// Un campo con una lista que se filtra al escribir y flota encima de lo demás,
/// sin moverlo. Lo que no está en la lista se escribe a mano. ↑ ↓ y Enter eligen;
/// Esc cierra la lista.
class ChoiceField extends StatefulWidget {
  final String label;
  final TextEditingController controller;
  final List<String> options;
  final ValueChanged<String>? onChanged;
  final bool enabled;
  final String? hint;
  final String? help;
  final String? error;

  /// Al entrar al campo (para pedir las opciones).
  final VoidCallback? onFocus;

  const ChoiceField({
    super.key,
    required this.label,
    required this.controller,
    required this.options,
    this.onChanged,
    this.enabled = true,
    this.hint,
    this.help,
    this.error,
    this.onFocus,
  });

  @override
  State<ChoiceField> createState() => _ChoiceFieldState();
}

class _ChoiceFieldState extends State<ChoiceField> {
  static const _rowHeight = 44.0;
  static const _listHeight = 240.0;
  static const _minListWidth = 280.0;

  final _link = LayerLink();
  final _scroll = ScrollController();
  late final _focus = FocusNode(onKeyEvent: _onKey)..addListener(_focusChanged);
  bool _open = false;
  bool _above = false;
  bool _alignEnd = false;
  int _index = -1;

  List<String> get _shown {
    final q = widget.controller.text.trim().toLowerCase();
    if (q.isEmpty) return widget.options;
    return widget.options.where((o) => o.toLowerCase().contains(q)).toList();
  }

  void _show() {
    final box = context.findRenderObject() as RenderBox?;
    if (box != null && box.hasSize) {
      final corner = box.localToGlobal(Offset(0, box.size.height));
      final window = MediaQuery.sizeOf(context);
      _above = corner.dy + _listHeight > window.height;
      _alignEnd = corner.dx + _minListWidth > window.width;
    }
    setState(() {
      _open = true;
      _index = -1;
    });
  }

  void _focusChanged() {
    if (!mounted) return;
    if (_focus.hasFocus) {
      widget.onFocus?.call();
      _show();
    } else {
      setState(() => _open = false);
    }
  }

  void _edited(String text) {
    widget.onChanged?.call(text);
    _show();
  }

  void _pick(String option) {
    widget.controller.value = TextEditingValue(
      text: option,
      selection: TextSelection.collapsed(offset: option.length),
    );
    widget.onChanged?.call(option);
    setState(() => _open = false);
  }

  KeyEventResult _onKey(FocusNode node, KeyEvent e) {
    final shown = _shown;
    if (!_open || shown.isEmpty) return KeyEventResult.ignored;
    if (e.logicalKey == LogicalKeyboardKey.escape) {
      if (e is KeyDownEvent) setState(() => _open = false);
      return KeyEventResult.handled;
    }
    return listKeys(
      e,
      count: shown.length,
      index: _index,
      move: (i) {
        setState(() => _index = i);
        revealRow(_scroll, i, _rowHeight);
      },
      activate: _index >= 0 ? () => _pick(shown[_index]) : null,
    );
  }

  @override
  void dispose() {
    _focus.dispose();
    _scroll.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final shown = _shown;
    final side = _alignEnd ? 1.0 : -1.0;
    return LayoutBuilder(
      builder: (context, box) => Popover(
        open: _open && widget.enabled && shown.isNotEmpty,
        link: _link,
        targetAnchor: Alignment(side, _above ? -1 : 1),
        followerAnchor: Alignment(side, _above ? 1 : -1),
        offset: Offset(0, _above ? -4 : 4),
        builder: (context) => _ChoiceList(
          width: box.maxWidth < _minListWidth ? _minListWidth : box.maxWidth,
          height: _listHeight,
          rowHeight: _rowHeight,
          scroll: _scroll,
          options: _shown,
          index: _index,
          onPick: _pick,
        ),
        // Un clic en el campo que ya tiene el foco también abre la lista.
        child: Listener(
          onPointerDown: (_) {
            if (_focus.hasFocus && !_open) _show();
          },
          child: LabeledField(
            label: widget.label,
            controller: widget.controller,
            focusNode: _focus,
            mono: true,
            enabled: widget.enabled,
            hint: widget.hint,
            help: widget.help,
            error: widget.error,
            onChanged: _edited,
          ),
        ),
      ),
    );
  }
}

class _ChoiceList extends StatelessWidget {
  final double width;
  final double height;
  final double rowHeight;
  final ScrollController scroll;
  final List<String> options;
  final int index;
  final ValueChanged<String> onPick;

  const _ChoiceList({
    required this.width,
    required this.height,
    required this.rowHeight,
    required this.scroll,
    required this.options,
    required this.index,
    required this.onPick,
  });

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    // Un clic aquí es parte del campo: no le quita el foco. Las filas tampoco.
    return TextFieldTapRegion(
      child: ExcludeFocus(
        child: PopoverFrame(
          width: width,
          child: ConstrainedBox(
            constraints: BoxConstraints(maxHeight: height),
            child: ListView.builder(
              controller: scroll,
              shrinkWrap: true,
              itemExtent: rowHeight,
              itemCount: options.length,
              itemBuilder: (context, i) => Pressable(
                onPressed: () => onPick(options[i]),
                selected: i == index,
                color: i == index ? c.accentSoft : null,
                padding: const EdgeInsets.symmetric(horizontal: 12),
                child: Align(
                  alignment: AlignmentDirectional.centerStart,
                  child: Text(
                    options[i],
                    style: s.model,
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}

/// La caja de texto de 44 px de las maquetas.
class FieldBox extends StatelessWidget {
  final TextEditingController controller;
  final String label;
  final ValueChanged<String>? onChanged;
  final bool mono;
  final bool enabled;
  final bool invalid;
  final String? hint;
  final FocusNode? focusNode;
  final bool obscure;

  /// Una key: sin menú de copiar ni Ctrl+C / Ctrl+X, también a la vista.
  final bool secret;
  final Widget? suffix;
  final int? maxLines;
  final int? minLines;

  const FieldBox({
    super.key,
    required this.controller,
    required this.label,
    this.onChanged,
    this.mono = false,
    this.enabled = true,
    this.invalid = false,
    this.hint,
    this.focusNode,
    this.obscure = false,
    this.secret = false,
    this.suffix,
    this.maxLines = 1,
    this.minLines,
  });

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final style = mono ? s.fieldMono : s.field;
    OutlineInputBorder border(Color color, [double width = 1]) =>
        OutlineInputBorder(
          borderRadius: BorderRadius.circular(10),
          borderSide: BorderSide(color: color, width: width),
        );
    final field = Semantics(
      label: label,
      textField: true,
      child: TextField(
        controller: controller,
        focusNode: focusNode,
        onChanged: onChanged,
        enabled: enabled,
        obscureText: obscure,
        contextMenuBuilder: secret
            ? null
            : (context, state) => AdaptiveTextSelectionToolbar.editableText(
                editableTextState: state,
              ),
        maxLines: obscure ? 1 : maxLines,
        minLines: minLines,
        // Keys: sin autocorrector, sugerencias ni aprendizaje del teclado.
        autocorrect: !obscure && !mono,
        enableSuggestions: !obscure && !mono,
        enableIMEPersonalizedLearning: !obscure,
        keyboardType: obscure
            ? TextInputType.visiblePassword
            : maxLines == 1
            ? TextInputType.text
            : TextInputType.multiline,
        style: enabled ? style : style.copyWith(color: c.textMuted),
        cursorColor: c.accent,
        decoration: InputDecoration(
          isDense: true,
          filled: true,
          fillColor: c.field,
          hintText: hint,
          hintStyle: style.copyWith(color: c.textMuted),
          contentPadding: const EdgeInsets.symmetric(
            horizontal: 14,
            vertical: 12,
          ),
          suffixIcon: suffix,
          border: border(c.border),
          enabledBorder: border(invalid ? c.error : c.border),
          disabledBorder: border(c.border),
          focusedBorder: border(invalid ? c.error : c.accent, c.focusWidth),
        ),
      ),
    );
    if (!secret) return field;
    const block = DoNothingAndStopPropagationTextIntent();
    return Shortcuts(
      shortcuts: const {
        SingleActivator(LogicalKeyboardKey.keyC, control: true): block,
        SingleActivator(LogicalKeyboardKey.keyX, control: true): block,
        SingleActivator(LogicalKeyboardKey.insert, control: true): block,
        SingleActivator(LogicalKeyboardKey.delete, shift: true): block,
      },
      child: field,
    );
  }
}

/// API key: enmascarada y vacía (vacía = se conserva la guardada). El botón
/// de ver muestra lo que se escribe hasta que el campo pierde el foco. No se
/// puede copiar ni cortar, tampoco a la vista.
class SecretField extends StatefulWidget {
  final String label;
  final TextEditingController controller;
  final ValueChanged<String>? onChanged;
  final bool enabled;

  /// `key_status` del servidor (`guardada: …abcd`), al lado del campo.
  final String status;
  final String? lockedBy;
  final String? help;
  final String? error;
  final String hint;

  /// Un botón junto al estado (Quitar).
  final Widget? trailing;

  const SecretField({
    super.key,
    required this.label,
    required this.controller,
    this.onChanged,
    this.enabled = true,
    this.status = '',
    this.lockedBy,
    this.help,
    this.error,
    this.hint = Strings.newKeyHint,
    this.trailing,
  });

  @override
  State<SecretField> createState() => _SecretFieldState();
}

class _SecretFieldState extends State<SecretField> {
  final _focus = FocusNode();
  final _eye = FocusNode();
  bool _reveal = false;

  @override
  void initState() {
    super.initState();
    _focus.addListener(_blur);
    _eye.addListener(_blur);
  }

  /// Al salir del campo (y de su botón de ver) se tapa.
  void _blur() {
    if (!_focus.hasFocus && !_eye.hasFocus && _reveal) {
      setState(() => _reveal = false);
    }
  }

  @override
  void dispose() {
    _focus.dispose();
    _eye.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final enabled = widget.enabled && widget.lockedBy == null;
    final eye = Padding(
      padding: const EdgeInsets.only(right: 2),
      child: IconAction(
        size: 40,
        focusNode: _eye,
        label: _reveal ? Strings.hideKey : Strings.showKey,
        onPressed: enabled ? () => setState(() => _reveal = !_reveal) : null,
        icon: Icon(
          _reveal ? Icons.visibility_off_rounded : Icons.visibility_rounded,
          size: 18,
          color: c.textMuted,
        ),
      ),
    );
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      spacing: 8,
      children: [
        Text(widget.label, style: s.label),
        Wrap(
          crossAxisAlignment: WrapCrossAlignment.center,
          spacing: 12,
          runSpacing: 8,
          children: [
            ConstrainedBox(
              // Con un botón al lado (Quitar), más angosto para que quepa en
              // la misma fila, como en la maqueta.
              constraints: BoxConstraints(
                minWidth: 280,
                maxWidth: widget.trailing == null ? 520 : 440,
              ),
              child: FieldBox(
                controller: widget.controller,
                label: widget.label,
                focusNode: _focus,
                onChanged: widget.onChanged,
                enabled: enabled,
                invalid: widget.error != null,
                mono: true,
                obscure: !_reveal,
                secret: true,
                hint: widget.hint,
                suffix: eye,
              ),
            ),
            if (widget.status.isNotEmpty)
              Text(widget.status, style: s.noteMono),
            ?widget.trailing,
          ],
        ),
        if (widget.lockedBy != null)
          Text(Strings.lockedBy(widget.lockedBy!), style: s.note),
        Text(widget.help ?? Strings.keepKeyHelp, style: s.note),
        FieldMessage(widget.error),
      ],
    );
  }
}

/// Botón secundario con borde (Reiniciar, Generar código, Quitar). Con
/// [waiting] queda deshabilitado; con [slow], además, el indicador en lugar
/// del ícono (una acción larga que pasa de 300 ms).
class SecondaryButton extends StatelessWidget {
  final String label;
  final IconData? icon;
  final VoidCallback? onPressed;
  final bool waiting;
  final bool slow;
  final bool bordered;
  final FocusNode? focusNode;

  const SecondaryButton({
    super.key,
    required this.label,
    this.icon,
    required this.onPressed,
    this.waiting = false,
    this.slow = false,
    this.bordered = true,
    this.focusNode,
  });

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final enabled = onPressed != null && !waiting;
    final ink = enabled ? c.text : c.textMuted;
    return Semantics(
      label: label,
      excludeSemantics: true,
      button: true,
      enabled: enabled,
      child: Pressable(
        focusNode: focusNode,
        onPressed: enabled ? onPressed : null,
        borderColor: bordered ? c.border : null,
        minHeight: 44,
        padding: const EdgeInsets.symmetric(horizontal: 16),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          spacing: 8,
          children: [
            if (slow)
              const Spinner()
            else if (icon != null)
              Icon(icon, size: 16, color: ink),
            Text(label, style: s.row.copyWith(color: ink)),
          ],
        ),
      ),
    );
  }
}
