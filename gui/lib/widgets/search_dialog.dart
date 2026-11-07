import 'package:flutter/material.dart';

import '../models/summary.dart';
import '../state/title_search.dart';
import '../strings.dart';
import '../theme/colors.dart';
import '../theme/motion.dart';
import '../theme/theme.dart';
import 'controls.dart';
import 'list_keys.dart';

/// Buscar chats (Ctrl+K): ventana centrada sobre el fondo oscurecido, que
/// aparece y sube 8 px (180 ms). Al cerrarse, el foco vuelve a donde estaba
/// (lo hace la ruta). [onOpen] recibe el `id` elegido.
Future<void> showSearchDialog(
  BuildContext context, {
  required List<ConversationSummary> summaries,
  required ValueChanged<String> onOpen,
}) {
  final motion = Motion.of(context);
  return showGeneralDialog(
    context: context,
    barrierDismissible: true,
    barrierLabel: Strings.searchChats,
    barrierColor: context.colors.scrim,
    transitionDuration: motion.searchDialog,
    transitionBuilder: (context, anim, _, child) {
      final t = CurvedAnimation(parent: anim, curve: Motion.enter);
      return FadeTransition(
        opacity: t,
        child: AnimatedBuilder(
          animation: t,
          builder: (context, child) => Transform.translate(
            offset: Offset(0, 8 * (1 - t.value)),
            child: child,
          ),
          child: child,
        ),
      );
    },
    pageBuilder: (context, _, _) => SearchDialog(
      summaries: summaries,
      onOpen: (id) {
        Navigator.of(context).pop();
        onOpen(id);
      },
    ),
  );
}

class SearchDialog extends StatefulWidget {
  final List<ConversationSummary> summaries;
  final ValueChanged<String> onOpen;

  const SearchDialog({
    super.key,
    required this.summaries,
    required this.onOpen,
  });

  @override
  State<SearchDialog> createState() => _SearchDialogState();
}

class _SearchDialogState extends State<SearchDialog> {
  final _query = TextEditingController();
  final _scroll = ScrollController();
  int _highlight = 0;

  /// Los resultados del texto de ahora (no del último dibujo: una tecla
  /// puede llegar en el mismo cuadro que lo escrito). Los ilegibles no salen.
  List<(ConversationSummary, (int, int))> get _hits => [
    for (final item in widget.summaries)
      if (item.readable)
        if (findInTitle(_title(item), _query.text) case final at?) (item, at),
  ];

  /// La fila resaltada, para desplazar la lista hasta ella.
  final _highlighted = GlobalKey();

  @override
  void initState() {
    super.initState();
    // Solo al cambiar el texto (mover el cursor también notifica).
    var last = '';
    _query.addListener(() {
      if (_query.text == last) return;
      last = _query.text;
      setState(() => _highlight = 0);
    });
  }

  @override
  void dispose() {
    _query.dispose();
    _scroll.dispose();
    super.dispose();
  }

  int _current(List<Object> hits) =>
      hits.isEmpty ? -1 : _highlight.clamp(0, hits.length - 1);

  void _move(int i) {
    final down = i > _highlight;
    setState(() => _highlight = i);
    WidgetsBinding.instance.addPostFrameCallback((_) {
      final row = _highlighted.currentContext;
      if (row == null) return;
      Scrollable.ensureVisible(
        row,
        alignmentPolicy: down
            ? ScrollPositionAlignmentPolicy.keepVisibleAtEnd
            : ScrollPositionAlignmentPolicy.keepVisibleAtStart,
      );
    });
  }

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final hits = _hits;
    final highlight = _current(hits);

    // Las filas con el encabezado de su grupo, en el orden del servidor.
    final rows = <Widget>[];
    String? group;
    for (final (i, (item, at)) in hits.indexed) {
      if (item.group != group) {
        group = item.group;
        rows.add(
          Padding(
            padding: EdgeInsets.fromLTRB(10, rows.isEmpty ? 4 : 14, 10, 4),
            child: Semantics(
              header: true,
              child: Text(group.toUpperCase(), style: s.group),
            ),
          ),
        );
      }
      rows.add(
        _Result(
          key: i == highlight ? _highlighted : null,
          item: item,
          at: at,
          highlighted: i == highlight,
          onTap: () => widget.onOpen(item.id),
        ),
      );
    }

    return Center(
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 560, maxHeight: 520),
          child: Material(
            color: c.sidebar,
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(16),
              side: BorderSide(color: c.border),
            ),
            clipBehavior: Clip.antiAlias,
            child: Semantics(
              scopesRoute: true,
              namesRoute: true,
              label: Strings.searchChats,
              explicitChildNodes: true,
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Container(
                    padding: const EdgeInsets.symmetric(horizontal: 16),
                    decoration: BoxDecoration(
                      border: Border(bottom: BorderSide(color: c.border)),
                    ),
                    child: Row(
                      spacing: 10,
                      children: [
                        Icon(
                          Icons.search_rounded,
                          size: 20,
                          color: c.textMuted,
                        ),
                        Expanded(
                          child: Focus(
                            // Lee el estado al llegar la tecla, no el del
                            // último dibujo: pueden llegar varias juntas.
                            onKeyEvent: (_, e) {
                              final hits = _hits;
                              return listKeys(
                                e,
                                count: hits.length,
                                index: _current(hits),
                                move: _move,
                                activate: hits.isEmpty
                                    ? null
                                    : () => widget.onOpen(
                                        hits[_current(hits)].$1.id,
                                      ),
                              );
                            },
                            child: TextField(
                              controller: _query,
                              autofocus: true,
                              style: s.input,
                              cursorColor: c.accent,
                              decoration: InputDecoration(
                                isDense: true,
                                border: InputBorder.none,
                                contentPadding: const EdgeInsets.symmetric(
                                  vertical: 16,
                                ),
                                hintText: Strings.searchChatsHint,
                                hintStyle: s.input.copyWith(color: c.textMuted),
                              ),
                            ),
                          ),
                        ),
                      ],
                    ),
                  ),
                  Flexible(
                    child: hits.isEmpty
                        ? Padding(
                            padding: const EdgeInsets.all(24),
                            child: Semantics(
                              liveRegion: true,
                              child: Text(
                                Strings.noChatsFound,
                                style: s.row.copyWith(color: c.textMuted),
                                textAlign: TextAlign.center,
                              ),
                            ),
                          )
                        : ExcludeFocus(
                            // Se eligen con ↑ ↓ desde el campo o con el mouse.
                            child: ListView(
                              controller: _scroll,
                              shrinkWrap: true,
                              padding: const EdgeInsets.all(8),
                              children: rows,
                            ),
                          ),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}

String _title(ConversationSummary item) =>
    item.title.isEmpty ? item.id : item.title;

class _Result extends StatelessWidget {
  final ConversationSummary item;
  final (int, int) at;
  final bool highlighted;
  final VoidCallback onTap;

  const _Result({
    super.key,
    required this.item,
    required this.at,
    required this.highlighted,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final title = _title(item);
    final (start, end) = at;
    return Padding(
      padding: const EdgeInsets.only(bottom: 2),
      child: Container(
        foregroundDecoration: highlighted
            ? BoxDecoration(
                border: Border.all(color: c.accent, width: c.focusWidth),
                borderRadius: BorderRadius.circular(10),
              )
            : null,
        child: Pressable(
          onPressed: onTap,
          selected: highlighted,
          minHeight: 40,
          padding: const EdgeInsets.symmetric(horizontal: 10),
          color: highlighted ? c.accentSoft : null,
          hoverColor: c.field,
          child: Row(
            spacing: 8,
            children: [
              if (item.usedSearch)
                Semantics(
                  label: Strings.withWebSearch,
                  child: Icon(
                    Icons.language_rounded,
                    size: 14,
                    color: c.textMuted,
                  ),
                ),
              Expanded(
                child: Text.rich(
                  TextSpan(
                    children: [
                      TextSpan(text: title.substring(0, start)),
                      TextSpan(
                        text: title.substring(start, end),
                        style: s.strong.copyWith(color: c.accent),
                      ),
                      TextSpan(text: title.substring(end)),
                    ],
                  ),
                  style: s.row,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
