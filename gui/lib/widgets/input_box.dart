import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../state/app_state.dart';
import '../strings.dart';
import '../theme/colors.dart';
import '../theme/motion.dart';
import '../theme/theme.dart';
import 'controls.dart';
import 'options_panel.dart';
import 'popover.dart';
import 'select.dart';

/// Caja del mensaje: una fila con el texto y Enviar o Detener, y la ayuda.
class InputBox extends StatefulWidget {
  final AppState app;
  const InputBox(this.app, {super.key});

  @override
  State<InputBox> createState() => _InputBoxState();
}

class _InputBoxState extends State<InputBox> {
  final _text = TextEditingController();
  late final _busyOrText = Listenable.merge([widget.app, _text]);
  final _textFocus = FocusNode(debugLabel: 'Mensaje');
  final _fieldKey = GlobalKey();

  // Opciones
  final _link = LayerLink();
  final _gear = FocusNode(debugLabel: 'Opciones');
  bool _optionsOpen = false;
  OptionsRequest? _request;

  /// Cada vez que se abre, una ventana nueva (aunque la anterior siga
  /// cerrándose).
  int _opened = 0;

  void _openOptions(OptionsRequest? request) => setState(() {
    _opened++;
    _request = request;
    _optionsOpen = true;
  });

  void _closeOptions() => setState(() => _optionsOpen = false);

  /// `/model` sin modelo abre Opciones.
  void _onOptionsRequest() {
    final r = widget.app.openOptions.value;
    if (r != null) _openOptions(r);
  }

  String _lastText = '';

  @override
  void initState() {
    super.initState();
    // Escribir quita el error de junto a la caja (mover el cursor no).
    _text.addListener(() {
      if (_text.text == _lastText) return;
      _lastText = _text.text;
      widget.app.inputError.value = null;
    });
    widget.app.restoreText.addListener(_restore);
    widget.app.openOptions.addListener(_onOptionsRequest);
  }

  @override
  void dispose() {
    widget.app.restoreText.removeListener(_restore);
    widget.app.openOptions.removeListener(_onOptionsRequest);
    _text.dispose();
    _textFocus.dispose();
    _gear.dispose();
    super.dispose();
  }

  /// El texto vuelve a la caja tras un error, si no se escribió otro.
  void _restore() {
    final t = widget.app.restoreText.value;
    if (t == null) return;
    widget.app.restoreText.value = null;
    if (_text.text.isEmpty) {
      _text.value = TextEditingValue(
        text: t,
        selection: TextSelection.collapsed(offset: t.length),
      );
    }
  }

  Future<void> _send() async {
    final text = _text.text;
    if (text.trim().isEmpty) return;
    if (await widget.app.submit(text) && _text.text == text) _text.clear();
  }

  KeyEventResult _onKey(FocusNode node, KeyEvent e) {
    final enter =
        e.logicalKey == LogicalKeyboardKey.enter ||
        e.logicalKey == LogicalKeyboardKey.numpadEnter;
    // Shift+Enter y Enter con una composición del teclado siguen de largo.
    if (e is KeyUpEvent ||
        !enter ||
        HardwareKeyboard.instance.isShiftPressed ||
        _text.value.composing.isValid) {
      return KeyEventResult.ignored;
    }
    // Enter mantenido: se envía una vez y la repetición no mete saltos de
    // línea en la caja.
    if (e is KeyDownEvent) _send();
    return KeyEventResult.handled;
  }

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final app = widget.app;
    // El mismo campo en una fila o en dos: la llave conserva el texto y el
    // foco al cambiar de lugar.
    Widget field(EdgeInsets padding) => Focus(
      key: _fieldKey,
      onKeyEvent: _onKey,
      child: MergeSemantics(
        child: Semantics(
          label: Strings.message,
          child: TextField(
            controller: _text,
            focusNode: _textFocus,
            autofocus: true,
            minLines: 1,
            maxLines: 8,
            style: s.input,
            cursorColor: c.accent,
            decoration: InputDecoration(
              isDense: true,
              border: InputBorder.none,
              contentPadding: padding,
              hintText: Strings.messageHint,
              hintStyle: s.input.copyWith(color: c.textMuted),
            ),
          ),
        ),
      ),
    );
    final gear = Semantics(
      expanded: _optionsOpen,
      child: IconAction(
        label: Strings.options,
        focusNode: _gear,
        color: _optionsOpen ? c.accentSoft : null,
        onPressed: _optionsOpen ? _closeOptions : () => _openOptions(null),
        icon: Icon(
          Icons.settings_outlined,
          size: 20,
          color: _optionsOpen ? c.text : c.textMuted,
        ),
      ),
    );
    final sendOrStop = Select(
      listenable: _busyOrText,
      select: () => (app.busy, _text.text.trim().isEmpty),
      builder: (context, v) => v.$1
          ? IconAction(
              label: Strings.stop,
              onPressed: app.cancel,
              icon: Icon(Icons.stop_rounded, size: 22, color: c.text),
            )
          : IconAction(
              label: Strings.send,
              onPressed: v.$2 ? null : _send,
              icon: Icon(
                Icons.arrow_upward_rounded,
                size: 22,
                color: v.$2 ? c.textMuted : c.accent,
              ),
            ),
    );
    return Padding(
      padding: const EdgeInsets.fromLTRB(24, 0, 24, 20),
      child: Center(
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 760),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            spacing: 8,
            children: [
              ValueListenableBuilder(
                valueListenable: app.searchOn,
                builder: (context, search, _) => Popover(
                  open: _optionsOpen,
                  link: _link,
                  offset: const Offset(0, -8),
                  onDismiss: _closeOptions,
                  returnFocus: _gear,
                  builder: (context) => OptionsPanel(
                    key: ValueKey(_opened),
                    app: app,
                    request: _request,
                    onClose: _closeOptions,
                  ),
                  child: Container(
                    padding: const EdgeInsets.all(6),
                    decoration: BoxDecoration(
                      color: c.field,
                      borderRadius: BorderRadius.circular(14),
                      border: Border.all(color: c.border),
                    ),
                    // La caja crece a dos filas con la búsqueda activa: la
                    // única excepción a no reacomodar (DISENO.md).
                    // AnimatedSize no admite 0 ms (Reducir animaciones).
                    child: _Grow(
                      child: search
                          ? Column(
                              crossAxisAlignment: CrossAxisAlignment.stretch,
                              spacing: 2,
                              children: [
                                field(const EdgeInsets.fromLTRB(8, 10, 8, 6)),
                                Row(
                                  spacing: 6,
                                  children: [
                                    gear,
                                    _SearchPill(
                                      app,
                                      onRemoved: _textFocus.requestFocus,
                                    ),
                                    const Spacer(),
                                    sendOrStop,
                                  ],
                                ),
                              ],
                            )
                          : Row(
                              crossAxisAlignment: CrossAxisAlignment.end,
                              spacing: 6,
                              children: [
                                gear,
                                Expanded(
                                  child: field(
                                    const EdgeInsets.symmetric(
                                      horizontal: 4,
                                      vertical: 10,
                                    ),
                                  ),
                                ),
                                sendOrStop,
                              ],
                            ),
                    ),
                  ),
                ),
              ),
              // Los errores de JSON-RPC ocupan el lugar de la ayuda: la caja
              // no se mueve.
              ValueListenableBuilder(
                valueListenable: app.inputError,
                builder: (context, error, _) => AnimatedSwitcher(
                  duration: Motion.of(context).fieldError,
                  switchInCurve: Motion.enter,
                  switchOutCurve: Motion.enter,
                  child: error == null
                      ? Text(
                          Strings.help,
                          key: const ValueKey(Strings.help),
                          style: s.help,
                          textAlign: TextAlign.center,
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis,
                        )
                      : Semantics(
                          key: ValueKey(error),
                          liveRegion: true,
                          child: Text(
                            error,
                            style: s.help.copyWith(color: c.error),
                            textAlign: TextAlign.center,
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                          ),
                        ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// "Búsqueda web ×" junto al engranaje; la × la apaga.
class _SearchPill extends StatelessWidget {
  final AppState app;

  /// La × desaparece con la pastilla: el foco va a otro lado.
  final VoidCallback onRemoved;
  const _SearchPill(this.app, {required this.onRemoved});

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    return Container(
      constraints: const BoxConstraints(minHeight: 32),
      padding: const EdgeInsets.only(left: 10, right: 4),
      decoration: BoxDecoration(
        color: c.accentSoft,
        borderRadius: BorderRadius.circular(999),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        spacing: 6,
        children: [
          Icon(Icons.language_rounded, size: 14, color: s.pill.color),
          Text(Strings.webSearch, style: s.pill),
          IconAction(
            label: Strings.removeWebSearch,
            size: 26,
            onPressed: () {
              app.searchOn.value = false;
              onRemoved();
            },
            icon: Icon(Icons.close_rounded, size: 14, color: s.pill.color),
          ),
        ],
      ),
    );
  }
}

/// La altura de la caja, animada (140 ms); sin animación, el hijo tal cual.
class _Grow extends StatelessWidget {
  final Widget child;
  const _Grow({required this.child});

  @override
  Widget build(BuildContext context) {
    final motion = Motion.of(context);
    if (motion.reduced) return child;
    return AnimatedSize(
      duration: motion.inputGrow,
      curve: Motion.enter,
      alignment: Alignment.bottomCenter,
      child: child,
    );
  }
}
