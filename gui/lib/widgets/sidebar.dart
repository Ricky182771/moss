import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../models/summary.dart';
import '../strings.dart';
import '../theme/colors.dart';
import '../theme/theme.dart';
import 'controls.dart';
import 'logo.dart';
import 'popover.dart';

const kSidebarWidth = 272.0;

/// Barra lateral: logo, Nuevo chat y la lista agrupada por `group`.
class Sidebar extends StatelessWidget {
  final List<ConversationSummary> summaries;

  /// `id` de la conversación abierta.
  final String? openId;
  final VoidCallback onNewChat;
  final VoidCallback onSearch;
  final ValueChanged<String> onOpen;
  final VoidCallback onSettings;
  final ChatActions actions;

  const Sidebar({
    super.key,
    required this.summaries,
    required this.openId,
    required this.onNewChat,
    required this.onSearch,
    required this.onOpen,
    required this.onSettings,
    required this.actions,
  });

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    // Mismo orden que el servidor: un grupo nuevo cada vez que cambia.
    final groups = <(String, List<ConversationSummary>)>[];
    for (final item in summaries) {
      if (groups.isEmpty || groups.last.$1 != item.group) {
        groups.add((item.group, []));
      }
      groups.last.$2.add(item);
    }

    return Container(
      width: kSidebarWidth,
      decoration: BoxDecoration(
        color: c.sidebar,
        border: Border(right: BorderSide(color: c.border)),
      ),
      padding: const EdgeInsets.fromLTRB(14, 20, 14, 20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        spacing: 16,
        children: [
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
            child: Row(
              spacing: 10,
              children: [
                const MossLogo(),
                Text.rich(
                  TextSpan(
                    text: Strings.appName,
                    children: [
                      TextSpan(
                        text: '_',
                        style: TextStyle(color: c.accent),
                      ),
                    ],
                  ),
                  style: s.logotype,
                ),
              ],
            ),
          ),
          // Como la maqueta: las dos filas juntas (2 px).
          Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            spacing: 2,
            children: [
              Pressable(
                onPressed: onNewChat,
                minHeight: 40,
                padding: const EdgeInsets.symmetric(horizontal: 10),
                child: Row(
                  spacing: 10,
                  children: [
                    Icon(Icons.add_rounded, size: 20, color: c.text),
                    Expanded(child: Text(Strings.newChat, style: s.nav)),
                    Text(Strings.newChatKey, style: s.kbd),
                  ],
                ),
              ),
              Pressable(
                onPressed: onSearch,
                minHeight: 40,
                padding: const EdgeInsets.symmetric(horizontal: 10),
                child: Row(
                  spacing: 10,
                  children: [
                    Icon(Icons.search_rounded, size: 20, color: c.text),
                    Expanded(child: Text(Strings.searchChats, style: s.nav)),
                    Text(Strings.searchChatsKey, style: s.kbd),
                  ],
                ),
              ),
            ],
          ),
          Expanded(
            child: Semantics(
              label: Strings.chats,
              container: true,
              child: ListView(
                children: [
                  for (final (i, (group, items)) in groups.indexed)
                    Padding(
                      padding: EdgeInsets.only(top: i == 0 ? 0 : 14),
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.stretch,
                        spacing: 2,
                        children: [
                          Padding(
                            padding: const EdgeInsets.fromLTRB(10, 0, 10, 4),
                            child: Semantics(
                              header: true,
                              child: Text(group.toUpperCase(), style: s.group),
                            ),
                          ),
                          for (final item in items)
                            _Row(
                              key: ValueKey(item.id),
                              item: item,
                              open: item.id == openId,
                              onOpen: () => onOpen(item.id),
                              actions: actions,
                            ),
                        ],
                      ),
                    ),
                ],
              ),
            ),
          ),
          // Fija abajo, con la línea de arriba de la maqueta.
          Container(
            padding: const EdgeInsets.only(top: 12),
            decoration: BoxDecoration(
              border: Border(top: BorderSide(color: c.border)),
            ),
            child: Pressable(
              onPressed: onSettings,
              minHeight: 44,
              padding: const EdgeInsets.symmetric(horizontal: 10),
              child: Row(
                spacing: 10,
                children: [
                  Icon(Icons.tune_rounded, size: 18, color: c.text),
                  Expanded(child: Text(Strings.settings, style: s.row)),
                  Text(Strings.settingsKey, style: s.kbd),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}

/// Lo que se hace desde el menú `⋯` de un chat.
class ChatActions {
  /// Devuelve el error del servidor, o null.
  final Future<String?> Function(String id, String title) rename;
  final void Function(String id) export;

  /// Abre la confirmación de Borrar.
  final void Function(ConversationSummary item) delete;

  const ChatActions({
    required this.rename,
    required this.export,
    required this.delete,
  });
}

/// Una fila de la lista. El botón `⋯` aparece al pasar el mouse, con el foco
/// y siempre en el chat abierto; el mismo menú sale con clic derecho y con
/// Shift+F10 o la tecla de menú. Los ilegibles no tienen menú.
class _Row extends StatefulWidget {
  final ConversationSummary item;
  final bool open;
  final VoidCallback onOpen;
  final ChatActions actions;

  const _Row({
    super.key,
    required this.item,
    required this.open,
    required this.onOpen,
    required this.actions,
  });

  @override
  State<_Row> createState() => _RowState();
}

class _RowState extends State<_Row> {
  bool _hover = false;
  bool _focus = false;
  bool _menu = false;
  final _link = LayerLink();
  final _dots = FocusNode();

  /// La fila misma: siempre está, así el foco puede volver a ella al
  /// terminar Renombrar (el ⋯ solo se ve con el foco o el mouse).
  final _rowFocus = FocusNode();

  // Renombrar
  TextEditingController? _rename;
  final _renameFocus = FocusNode();
  String? _renameError;
  bool _saving = false;

  ConversationSummary get item => widget.item;
  String get _title => item.title.isEmpty ? item.id : item.title;

  @override
  void initState() {
    super.initState();
    _renameFocus.addListener(() {
      // Salir del campo cancela (salvo mientras se guarda).
      if (!_renameFocus.hasFocus && _rename != null && !_saving) _endRename();
    });
  }

  @override
  void dispose() {
    _dots.dispose();
    _rowFocus.dispose();
    _rename?.dispose();
    _renameFocus.dispose();
    super.dispose();
  }

  void _openMenu() {
    if (item.readable) setState(() => _menu = true);
  }

  /// El foco vuelve al ⋯ antes de que se quite `_menu`: así el botón sigue
  /// a la vista (con el foco) y no desaparece con el foco adentro.
  void _closeMenu() {
    _dots.requestFocus();
    setState(() => _menu = false);
  }

  void _startRename() {
    _dots.requestFocus(); // Hasta que aparezca el campo.
    setState(() {
      _menu = false;
      _renameError = null;
      _rename = TextEditingController(text: _title)
        ..selection = TextSelection(baseOffset: 0, extentOffset: _title.length);
    });
    WidgetsBinding.instance.addPostFrameCallback(
      (_) => _renameFocus.requestFocus(),
    );
  }

  /// [refocus]: con Enter o Esc el foco vuelve a la fila; al salir con un
  /// clic se queda donde se hizo clic.
  void _endRename({bool refocus = false}) {
    final old = _rename;
    setState(() {
      _rename = null;
      _renameError = null;
      _focus = false; // El Focus que lo vigila se desmontó mientras.
    });
    WidgetsBinding.instance.addPostFrameCallback((_) {
      old?.dispose();
      if (refocus && mounted) _rowFocus.requestFocus();
    });
  }

  Future<void> _saveRename() async {
    final text = _rename?.text ?? '';
    setState(() => _saving = true);
    final error = await widget.actions.rename(item.id, text);
    if (!mounted) return;
    _saving = false;
    if (error == null) {
      _endRename(refocus: true);
    } else {
      setState(() => _renameError = error);
      _renameFocus.requestFocus();
    }
  }

  KeyEventResult _renameKeys(FocusNode _, KeyEvent e) {
    if (e is! KeyDownEvent) return KeyEventResult.ignored;
    if (e.logicalKey == LogicalKeyboardKey.escape) {
      _endRename(refocus: true);
      return KeyEventResult.handled;
    }
    if (e.logicalKey == LogicalKeyboardKey.enter ||
        e.logicalKey == LogicalKeyboardKey.numpadEnter) {
      if (!_saving) _saveRename();
      return KeyEventResult.handled;
    }
    return KeyEventResult.ignored;
  }

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    if (_rename case final controller?) {
      return Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        spacing: 4,
        children: [
          Container(
            constraints: const BoxConstraints(minHeight: 40),
            padding: const EdgeInsets.symmetric(horizontal: 10),
            decoration: BoxDecoration(
              color: c.field,
              borderRadius: BorderRadius.circular(10),
              border: Border.all(color: c.accent, width: c.focusWidth),
            ),
            child: Focus(
              onKeyEvent: _renameKeys,
              child: Semantics(
                label: Strings.chatTitle,
                textField: true,
                child: TextField(
                  controller: controller,
                  focusNode: _renameFocus,
                  // readOnly y no enabled: así conserva el foco al guardar.
                  readOnly: _saving,
                  style: s.row,
                  cursorColor: c.accent,
                  decoration: InputDecoration(
                    isDense: true,
                    border: InputBorder.none,
                    contentPadding: const EdgeInsets.symmetric(vertical: 10),
                    hintText: Strings.chatTitle,
                    hintStyle: s.row.copyWith(color: c.textMuted),
                  ),
                ),
              ),
            ),
          ),
          if (_renameError case final error?)
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 10),
              child: Semantics(
                liveRegion: true,
                child: Text(error, style: s.help.copyWith(color: c.error)),
              ),
            ),
        ],
      );
    }

    final dots = item.readable && (widget.open || _hover || _focus || _menu);
    final row = Pressable(
      focusNode: _rowFocus,
      // Los ilegibles no se abren.
      onPressed: item.readable ? widget.onOpen : null,
      selected: widget.open,
      label: item.readable ? null : '$_title. ${item.error}',
      minHeight: 40,
      padding: EdgeInsets.only(left: 10, right: dots ? 44 : 10),
      color: widget.open
          ? c.accentSoft
          : _menu
          ? c.field
          : null,
      hoverColor: widget.open ? c.accentSoft : c.field,
      child: Row(
        spacing: 8,
        children: [
          if (item.readable && item.usedSearch)
            Semantics(
              label: Strings.withWebSearch,
              child: Icon(Icons.language_rounded, size: 14, color: c.textMuted),
            ),
          Expanded(
            child: Text(
              _title,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: widget.open
                  ? s.rowOpen
                  : item.readable
                  ? s.row
                  : s.row.copyWith(color: c.textMuted),
            ),
          ),
        ],
      ),
    );
    // El error del ilegible, como etiqueta al pasar el mouse.
    if (!item.readable) return HoverLabel(label: item.error, child: row);

    return CallbackShortcuts(
      bindings: {
        const SingleActivator(LogicalKeyboardKey.f10, shift: true): _openMenu,
        const SingleActivator(LogicalKeyboardKey.contextMenu): _openMenu,
      },
      child: Focus(
        canRequestFocus: false,
        skipTraversal: true,
        onFocusChange: (f) => setState(() => _focus = f),
        child: MouseRegion(
          onEnter: (_) => setState(() => _hover = true),
          onExit: (_) => setState(() => _hover = false),
          child: GestureDetector(
            onSecondaryTap: _openMenu,
            child: Stack(
              children: [
                row,
                if (dots)
                  Positioned(
                    right: 4,
                    top: 4,
                    child: Popover(
                      open: _menu,
                      link: _link,
                      targetAnchor: Alignment.bottomRight,
                      followerAnchor: Alignment.topRight,
                      offset: const Offset(0, 4),
                      onDismiss: _closeMenu,
                      returnFocus: _dots,
                      builder: (context) => _ChatMenu(
                        onRename: _startRename,
                        onExport: () {
                          _closeMenu();
                          widget.actions.export(item.id);
                        },
                        onDelete: () {
                          _closeMenu();
                          widget.actions.delete(item);
                        },
                      ),
                      child: Semantics(
                        expanded: _menu,
                        child: IconAction(
                          label: Strings.moreOptions(_title),
                          size: 32,
                          radius: 8,
                          focusNode: _dots,
                          onPressed: _menu ? _closeMenu : _openMenu,
                          icon: Icon(
                            Icons.more_horiz_rounded,
                            size: 16,
                            color: _menu ? c.text : c.textMuted,
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
    );
  }
}

/// Renombrar, Exportar y, tras un separador, Borrar… en el color de error.
class _ChatMenu extends StatelessWidget {
  final VoidCallback onRename;
  final VoidCallback onExport;
  final VoidCallback onDelete;

  const _ChatMenu({
    required this.onRename,
    required this.onExport,
    required this.onDelete,
  });

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    Widget item(
      IconData icon,
      String label,
      VoidCallback onTap, {
      Color? color,
      bool autofocus = false,
    }) {
      final s = context.styles.row.copyWith(color: color);
      return _MenuItem(
        autofocus: autofocus,
        onTap: onTap,
        child: Row(
          spacing: 10,
          children: [
            Icon(icon, size: 16, color: color ?? c.text),
            Flexible(
              child: Text(
                label,
                style: s,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
              ),
            ),
          ],
        ),
      );
    }

    return PopoverFrame(
      width: 188,
      child: Semantics(
        label: Strings.chatMenu,
        explicitChildNodes: true,
        child: FocusTraversalGroup(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            spacing: 2,
            children: [
              item(
                Icons.edit_outlined,
                Strings.rename,
                onRename,
                autofocus: true,
              ),
              item(Icons.download_rounded, Strings.export, onExport),
              Container(
                height: 1,
                margin: const EdgeInsets.symmetric(horizontal: 6, vertical: 4),
                color: c.border,
              ),
              item(
                Icons.delete_outline_rounded,
                Strings.delete,
                onDelete,
                color: c.error,
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// Opción del menú; ↑ ↓ mueven el foco entre ellas.
class _MenuItem extends StatefulWidget {
  final bool autofocus;
  final VoidCallback onTap;
  final Widget child;
  const _MenuItem({
    required this.autofocus,
    required this.onTap,
    required this.child,
  });

  @override
  State<_MenuItem> createState() => _MenuItemState();
}

class _MenuItemState extends State<_MenuItem> {
  final _node = FocusNode();

  @override
  void initState() {
    super.initState();
    if (widget.autofocus) {
      WidgetsBinding.instance.addPostFrameCallback((_) => _node.requestFocus());
    }
  }

  @override
  void dispose() {
    _node.dispose();
    super.dispose();
  }

  KeyEventResult _keys(FocusNode node, KeyEvent e) {
    if (e is KeyUpEvent) return KeyEventResult.ignored;
    if (e.logicalKey == LogicalKeyboardKey.arrowDown) {
      node.nextFocus();
      return KeyEventResult.handled;
    }
    if (e.logicalKey == LogicalKeyboardKey.arrowUp) {
      node.previousFocus();
      return KeyEventResult.handled;
    }
    return KeyEventResult.ignored;
  }

  @override
  Widget build(BuildContext context) => Focus(
    canRequestFocus: false,
    skipTraversal: true,
    onKeyEvent: (_, e) => _keys(_node, e),
    child: Pressable(
      focusNode: _node,
      onPressed: widget.onTap,
      radius: 8,
      minHeight: 36,
      padding: const EdgeInsets.symmetric(horizontal: 10),
      child: widget.child,
    ),
  );
}
