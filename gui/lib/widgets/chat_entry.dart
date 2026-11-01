import 'package:flutter/material.dart';
import 'package:flutter_markdown/flutter_markdown.dart';

import '../models/conversation.dart';
import '../theme/colors.dart';

/// Widget para una entrada de la conversación según DISENO.md
class ChatEntry extends StatelessWidget {
  final Entry entry;
  final int index;

  const ChatEntry({super.key, required this.entry, required this.index});

  @override
  Widget build(BuildContext context) {
    final colors = context.colors;

    switch (entry.kind) {
      case EntryKind.user:
        return _buildUserMessage(context, colors);
      case EntryKind.assistant:
        return _buildAssistantMessage(context, colors);
      case EntryKind.error:
        return _buildErrorMessage(context, colors);
      case EntryKind.notice:
        return _buildNoticeMessage(context, colors);
      case EntryKind.sources:
        return _buildSourcesMessage(context, colors);
    }
  }

  Widget _buildUserMessage(BuildContext context, MossColors colors) {
    return Align(
      alignment: Alignment.centerRight,
      child: Container(
        constraints: const BoxConstraints(maxWidth: 600),
        margin: const EdgeInsets.only(left: 80, bottom: 16),
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
        decoration: BoxDecoration(
          color: colors.userBubble,
          borderRadius: BorderRadius.circular(12),
        ),
        child: SelectableText(
          entry.text,
          style: Theme.of(context).textTheme.bodyMedium,
        ),
      ),
    );
  }

  Widget _buildAssistantMessage(BuildContext context, MossColors colors) {
    return Container(
      constraints: const BoxConstraints(maxWidth: 700),
      margin: const EdgeInsets.only(right: 80, bottom: 16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // Pensamiento (plegado si hay)
          if (entry.reasoning.isNotEmpty) _buildReasoning(context, colors),

          // Texto de la respuesta
          MarkdownBody(
            data: entry.text,
            styleSheet: MarkdownStyleSheet(
              p: Theme.of(context).textTheme.bodyLarge,
              code: Theme.of(context).textTheme.displaySmall,
              codeblockDecoration: BoxDecoration(
                color: colors.codeBg,
                borderRadius: BorderRadius.circular(8),
              ),
              codeblockPadding: const EdgeInsets.all(12),
            ),
            selectable: true,
          ),

          // Nota (finish_reason)
          if (entry.note.isNotEmpty)
            Padding(
              padding: const EdgeInsets.only(top: 8),
              child: Text(
                entry.note,
                style: Theme.of(context).textTheme.labelSmall,
              ),
            ),

          // Indicadores de estado
          if (entry.inProgress)
            Padding(
              padding: const EdgeInsets.only(top: 8),
              child: SizedBox(
                width: 16,
                height: 16,
                child: CircularProgressIndicator(
                  strokeWidth: 2,
                  valueColor: AlwaysStoppedAnimation(colors.textMuted),
                ),
              ),
            ),
          if (entry.cancelled)
            Padding(
              padding: const EdgeInsets.only(top: 8),
              child: Text(
                '(cancelado)',
                style: Theme.of(context).textTheme.labelSmall,
              ),
            ),
          if (entry.incomplete)
            Padding(
              padding: const EdgeInsets.only(top: 8),
              child: Text(
                '(incompleto)',
                style: Theme.of(context).textTheme.labelSmall,
              ),
            ),
        ],
      ),
    );
  }

  Widget _buildReasoning(BuildContext context, MossColors colors) {
    return Container(
      margin: const EdgeInsets.only(bottom: 12),
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: colors.field,
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: colors.border),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('Pensamiento', style: Theme.of(context).textTheme.titleSmall),
          const SizedBox(height: 8),
          MarkdownBody(
            data: entry.reasoning,
            styleSheet: MarkdownStyleSheet(
              p: Theme.of(context).textTheme.bodySmall,
            ),
            selectable: true,
          ),
        ],
      ),
    );
  }

  Widget _buildErrorMessage(BuildContext context, MossColors colors) {
    return Container(
      margin: const EdgeInsets.only(bottom: 16),
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: colors.errorBg,
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: colors.error),
      ),
      child: Row(
        children: [
          Icon(Icons.error_outline, color: colors.error, size: 20),
          const SizedBox(width: 12),
          Expanded(
            child: SelectableText(
              entry.text,
              style: Theme.of(context).textTheme.bodyMedium
                  ?.copyWith(color: colors.error),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildNoticeMessage(BuildContext context, MossColors colors) {
    return Container(
      margin: const EdgeInsets.only(bottom: 16),
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: colors.field,
        borderRadius: BorderRadius.circular(8),
      ),
      child: SelectableText(
        entry.text,
        style: Theme.of(context).textTheme.labelSmall,
        textAlign: TextAlign.center,
      ),
    );
  }

  Widget _buildSourcesMessage(BuildContext context, MossColors colors) {
    return Container(
      margin: const EdgeInsets.only(bottom: 16),
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: colors.field,
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: colors.border),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            'Fuentes de búsqueda',
            style: Theme.of(context).textTheme.titleSmall,
          ),
          const SizedBox(height: 8),
          ...entry.sources.map(
            (source) => Padding(
              padding: const EdgeInsets.only(bottom: 4),
              child: Text(
                '${source.n}. ${source.title}',
                style: Theme.of(context).textTheme.bodySmall,
              ),
            ),
          ),
        ],
      ),
    );
  }
}
