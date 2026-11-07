import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../state/app_state.dart';
import '../strings.dart';
import '../theme/colors.dart';
import '../theme/motion.dart';
import '../theme/theme.dart';
import 'controls.dart';

/// Caja del mensaje: una fila con el texto y Enviar o Detener, y la ayuda.
class InputBox extends StatefulWidget {
  final AppState app;
  const InputBox(this.app, {super.key});

  @override
  State<InputBox> createState() => _InputBoxState();
}

class _InputBoxState extends State<InputBox> {
  final _text = TextEditingController();
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
  }

  @override
  void dispose() {
    widget.app.restoreText.removeListener(_restore);
    _text.dispose();
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
    // Shift+Enter, Enter con una composición del teclado y la repetición de
    // la tecla siguen de largo.
    if (e is! KeyDownEvent ||
        !enter ||
        HardwareKeyboard.instance.isShiftPressed ||
        _text.value.composing.isValid) {
      return KeyEventResult.ignored;
    }
    _send();
    return KeyEventResult.handled;
  }

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final app = widget.app;
    return Padding(
      padding: const EdgeInsets.fromLTRB(24, 0, 24, 20),
      child: Center(
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 760),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            spacing: 8,
            children: [
              Container(
                padding: const EdgeInsets.all(6),
                decoration: BoxDecoration(
                  color: c.field,
                  borderRadius: BorderRadius.circular(14),
                  border: Border.all(color: c.border),
                ),
                child: Row(
                  crossAxisAlignment: CrossAxisAlignment.end,
                  spacing: 6,
                  children: [
                    // G2: aquí, a la izquierda, va el engranaje de Opciones.
                    Expanded(
                      child: Focus(
                        onKeyEvent: _onKey,
                        child: MergeSemantics(
                          child: Semantics(
                            label: Strings.message,
                            child: TextField(
                              controller: _text,
                              autofocus: true,
                              minLines: 1,
                              maxLines: 8,
                              style: s.input,
                              cursorColor: c.accent,
                              decoration: InputDecoration(
                                isDense: true,
                                border: InputBorder.none,
                                contentPadding: const EdgeInsets.symmetric(
                                  horizontal: 8,
                                  vertical: 10,
                                ),
                                hintText: Strings.messageHint,
                                hintStyle: s.input.copyWith(color: c.textMuted),
                              ),
                            ),
                          ),
                        ),
                      ),
                    ),
                    ListenableBuilder(
                      listenable: Listenable.merge([app, _text]),
                      builder: (context, _) => app.busy
                          ? IconAction(
                              label: Strings.stop,
                              onPressed: app.cancel,
                              icon: Icon(
                                Icons.stop_rounded,
                                size: 22,
                                color: c.text,
                              ),
                            )
                          : IconAction(
                              label: Strings.send,
                              onPressed: _text.text.trim().isEmpty
                                  ? null
                                  : _send,
                              icon: Icon(
                                Icons.arrow_upward_rounded,
                                size: 22,
                                color: _text.text.trim().isEmpty
                                    ? c.textMuted
                                    : c.accent,
                              ),
                            ),
                    ),
                  ],
                ),
              ),
              // Los errores de JSON-RPC ocupan el lugar de la ayuda: la caja
              // no se mueve.
              ValueListenableBuilder(
                valueListenable: app.inputError,
                builder: (context, error, _) => AnimatedSwitcher(
                  duration: Motion.of(context).fieldError,
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
