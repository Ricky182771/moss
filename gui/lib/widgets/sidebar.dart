import 'package:flutter/material.dart';

import '../models/summary.dart';
import '../strings.dart';
import '../theme/colors.dart';
import '../theme/theme.dart';
import 'controls.dart';
import 'logo.dart';

const kSidebarWidth = 272.0;

/// Barra lateral: logo, Nuevo chat y la lista agrupada por `group`.
class Sidebar extends StatelessWidget {
  final List<ConversationSummary> summaries;

  /// `id` de la conversación abierta.
  final String? openId;
  final VoidCallback onNewChat;
  final ValueChanged<String> onOpen;

  const Sidebar({
    super.key,
    required this.summaries,
    required this.openId,
    required this.onNewChat,
    required this.onOpen,
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
                              item: item,
                              open: item.id == openId,
                              onOpen: () => onOpen(item.id),
                            ),
                        ],
                      ),
                    ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _Row extends StatelessWidget {
  final ConversationSummary item;
  final bool open;
  final VoidCallback onOpen;

  const _Row({required this.item, required this.open, required this.onOpen});

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final title = item.title.isEmpty ? item.id : item.title;
    final row = Pressable(
      // Los ilegibles no se abren.
      onPressed: item.readable ? onOpen : null,
      selected: open,
      label: item.readable ? null : '$title. ${item.error}',
      minHeight: 40,
      padding: const EdgeInsets.symmetric(horizontal: 10),
      color: open ? c.accentSoft : null,
      hoverColor: open ? c.accentSoft : c.field,
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
              title,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: open
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
    return item.readable ? row : HoverLabel(label: item.error, child: row);
  }
}
