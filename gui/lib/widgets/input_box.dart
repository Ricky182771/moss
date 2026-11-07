import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../models/settings.dart';
import '../state/app_state.dart';
import '../state/slash_menu.dart';
import '../strings.dart';
import '../theme/colors.dart';
import '../theme/motion.dart';
import '../theme/theme.dart';
import 'controls.dart';
import 'list_keys.dart';
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

  void _openOptions(OptionsRequest? request) {
    setState(() {
      _opened++;
      _request = request;
      _optionsOpen = true;
    });
    _syncSlash();
  }

  void _closeOptions() {
    setState(() => _optionsOpen = false);
    _syncSlash();
  }

  /// `/model` sin modelo abre Opciones.
  void _onOptionsRequest() {
    final r = widget.app.openOptions.value;
    if (r != null) _openOptions(r);
  }

  // Menú de / (como la TUI: cli/src/command_menu.cpp)
  final _slashLink = LayerLink();

  /// `commands.list`, pedido al abrir el menú (no en cada tecla). Al
  /// reabrirlo se ve la lista anterior hasta que llega la nueva.
  List<CommandInfo>? _commands;
  int _slashGeneration = 0;

  /// Esc lo cerró; vuelve a salir al cambiar el texto.
  bool _slashDismissed = false;
  bool _slashWasOpen = false;
  int _slashIndex = 0;

  bool get _slashOpen =>
      !_slashDismissed &&
      !_optionsOpen &&
      _textFocus.hasFocus &&
      typingCommand(_text.text);

  List<CommandInfo> get _matches =>
      filterCommands(_commands ?? const [], _text.text);

  bool _canPick(List<CommandInfo> m, int i) => m[i].available;

  /// Abre o cierra el menú según el texto y el foco; al abrirlo, pide la
  /// lista.
  void _syncSlash() {
    if (!mounted) return;
    final open = _slashOpen;
    if (open && !_slashWasOpen) {
      final generation = ++_slashGeneration;
      widget.app.commands().then((list) {
        // Una respuesta de una apertura anterior no cuenta.
        if (!mounted || generation != _slashGeneration) return;
        setState(() {
          _commands = list;
          _slashIndex = firstEnabled(
            _matches.length,
            (i) => _canPick(_matches, i),
          );
        });
      });
    }
    if (open || _slashWasOpen) setState(() {});
    _slashWasOpen = open;
  }

  void _complete(CommandInfo c) {
    final text = completeCommand(c);
    _text.value = TextEditingValue(
      text: text,
      selection: TextSelection.collapsed(offset: text.length),
    );
    setState(() => _slashDismissed = true); // Completo: se cierra.
    _syncSlash();
  }

  /// ↑ ↓, Tab, Enter y Esc con el menú abierto.
  KeyEventResult _slashKeys(KeyEvent e) {
    if (e is KeyUpEvent) return KeyEventResult.ignored;
    final key = e.logicalKey;
    if (key == LogicalKeyboardKey.escape) {
      setState(() => _slashDismissed = true);
      _syncSlash();
      return KeyEventResult.handled;
    }
    final m = _matches;
    final i = _slashIndex;
    final picked = i >= 0 && i < m.length && m[i].available ? m[i] : null;
    final keys = HardwareKeyboard.instance;
    if (key == LogicalKeyboardKey.tab) {
      if (keys.isShiftPressed) return KeyEventResult.ignored;
      if (picked != null && e is KeyDownEvent) _complete(picked);
      return m.isEmpty ? KeyEventResult.ignored : KeyEventResult.handled;
    }
    final enter =
        key == LogicalKeyboardKey.enter ||
        key == LogicalKeyboardKey.numpadEnter;
    if (enter) {
      // Sin nada que elegir, Enter manda lo escrito, como siempre.
      if (picked == null ||
          keys.isShiftPressed ||
          keys.isControlPressed ||
          keys.isAltPressed ||
          keys.isMetaPressed) {
        return KeyEventResult.ignored;
      }
      if (e is KeyDownEvent) {
        if (picked.needsArgument) {
          _complete(picked);
        } else {
          _text.text = picked.name;
          setState(() => _slashDismissed = true);
          _syncSlash();
          _send();
        }
      }
      return KeyEventResult.handled;
    }
    return listKeys(
      e,
      count: m.length,
      index: i,
      move: (n) => setState(() => _slashIndex = n),
      activate: null,
      enabled: (n) => _canPick(m, n),
      wrap: true,
    );
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
      _slashDismissed = false;
      if (_commands case final all?) {
        final m = filterCommands(all, _text.text);
        _slashIndex = firstEnabled(m.length, (i) => _canPick(m, i));
      }
      _syncSlash();
    });
    _textFocus.addListener(_syncSlash);
    widget.app.restoreText.addListener(_restore);
    widget.app.openOptions.addListener(_onOptionsRequest);
  }

  @override
  void dispose() {
    widget.app.restoreText.removeListener(_restore);
    widget.app.openOptions.removeListener(_onOptionsRequest);
    _text.dispose();
    _textFocus.removeListener(_syncSlash);
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
    if (_slashOpen && _commands != null && !_text.value.composing.isValid) {
      final r = _slashKeys(e);
      if (r == KeyEventResult.handled) return r;
    }
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
              LayoutBuilder(
                builder: (context, box) => Popover(
                  open: _slashOpen && _commands != null,
                  link: _slashLink,
                  offset: const Offset(0, -8),
                  builder: (context) => _SlashMenu(
                    width: box.maxWidth,
                    matches: _matches,
                    index: _slashIndex,
                    onPick: _complete,
                  ),
                  child: ValueListenableBuilder(
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
                                  crossAxisAlignment:
                                      CrossAxisAlignment.stretch,
                                  spacing: 2,
                                  children: [
                                    field(
                                      const EdgeInsets.fromLTRB(8, 10, 8, 6),
                                    ),
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

/// El menú de `/` arriba de la caja: nombre con argumentos y descripción; los
/// no disponibles, atenuados con su nota y sin poder elegirse. El foco se
/// queda en la caja (↑ ↓ desde ahí); el clic cuenta como dentro del campo.
class _SlashMenu extends StatelessWidget {
  final double width;
  final List<CommandInfo> matches;
  final int index;
  final ValueChanged<CommandInfo> onPick;

  const _SlashMenu({
    required this.width,
    required this.matches,
    required this.index,
    required this.onPick,
  });

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    return TextFieldTapRegion(
      child: PopoverFrame(
        width: width,
        child: Semantics(
          label: Strings.commands,
          explicitChildNodes: true,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            spacing: 2,
            children: [
              if (matches.isEmpty)
                Padding(
                  padding: const EdgeInsets.symmetric(
                    horizontal: 10,
                    vertical: 8,
                  ),
                  child: Text(Strings.noCommands, style: s.help),
                ),
              for (final (i, cmd) in matches.indexed)
                Container(
                  foregroundDecoration: i == index
                      ? BoxDecoration(
                          border: Border.all(color: c.accent, width: 2),
                          borderRadius: BorderRadius.circular(8),
                        )
                      : null,
                  child: Pressable(
                    onPressed: cmd.available ? () => onPick(cmd) : null,
                    selected: i == index,
                    radius: 8,
                    minHeight: 36,
                    padding: const EdgeInsets.symmetric(horizontal: 10),
                    color: i == index ? c.accentSoft : null,
                    child: Row(
                      spacing: 12,
                      children: [
                        Text(
                          cmd.arguments.isEmpty
                              ? cmd.name
                              : '${cmd.name} ${cmd.arguments}',
                          style: cmd.available
                              ? s.model
                              : s.model.copyWith(color: c.textMuted),
                        ),
                        Expanded(
                          child: Text(
                            cmd.available ? cmd.description : cmd.note,
                            style: s.help,
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                          ),
                        ),
                      ],
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
