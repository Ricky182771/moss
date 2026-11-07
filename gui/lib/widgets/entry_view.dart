import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart' show OverflowBoxFit;

import '../models/conversation.dart';
import '../state/apply.dart';
import '../strings.dart';
import '../theme/colors.dart';
import '../theme/motion.dart';
import '../theme/theme.dart';
import 'controls.dart';
import 'logo.dart';
import 'markdown_view.dart';
import 'message_list.dart';

/// Avatar (28) más su separación (14): la sangría del texto del modelo.
const _avatarIndent = EdgeInsets.only(left: 42);

/// Una entrada del historial (PROTOCOL.md §5).
class EntryView extends StatelessWidget {
  final Entry entry;
  const EntryView(this.entry, {super.key});

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final muted = s.answer.copyWith(color: c.textMuted);
    return switch (entry.kind) {
      EntryKind.user => LayoutBuilder(
        builder: (context, box) => Align(
          alignment: Alignment.centerRight,
          child: Container(
            constraints: BoxConstraints(maxWidth: box.maxWidth * 0.8),
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
            decoration: BoxDecoration(
              color: c.accentSoft,
              borderRadius: const BorderRadius.only(
                topLeft: Radius.circular(18),
                topRight: Radius.circular(18),
                bottomLeft: Radius.circular(18),
                bottomRight: Radius.circular(4),
              ),
            ),
            child: Text(entry.text, style: s.bubble),
          ),
        ),
      ),
      EntryKind.assistant => AssistantRow(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          spacing: 14,
          children: [
            if (entry.reasoning.isNotEmpty) ThoughtView(entry),
            if (entry.text.isNotEmpty || entry.reasoning.isEmpty)
              MarkdownView(
                entry.text,
                streaming: entry.inProgress && entry.text.isNotEmpty,
              ),
            // Como la TUI: cancelada, incompleta o la nota de finish_reason.
            if (entry.cancelled)
              Text(Strings.cancelled, style: muted)
            else if (entry.incomplete)
              Text(Strings.incomplete, style: muted)
            else if (entry.note.isNotEmpty)
              Text(entry.note, style: muted),
          ],
        ),
      ),
      EntryKind.error => Padding(
        padding: _avatarIndent,
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          spacing: 8,
          children: [
            Padding(
              padding: const EdgeInsets.only(top: 3),
              child: Icon(
                Icons.error_outline_rounded,
                size: 18,
                color: c.error,
              ),
            ),
            Expanded(
              child: Text(entry.text, style: s.answer.copyWith(color: c.error)),
            ),
          ],
        ),
      ),
      EntryKind.notice => Padding(
        padding: _avatarIndent,
        child: Text(entry.text, style: muted),
      ),
      EntryKind.sources => Padding(
        padding: _avatarIndent,
        child: _SourcesView(entry.sources),
      ),
    };
  }
}

/// La "m" de avatar y, a su lado, lo del modelo.
class AssistantRow extends StatelessWidget {
  final Widget child;
  const AssistantRow({super.key, required this.child});

  @override
  Widget build(BuildContext context) => Row(
    crossAxisAlignment: CrossAxisAlignment.start,
    spacing: 14,
    children: [
      const Padding(padding: EdgeInsets.only(top: 2), child: MossLogo()),
      Expanded(child: child),
    ],
  );
}

class _SourcesView extends StatelessWidget {
  final List<Source> sources;
  const _SourcesView(this.sources);

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final muted = s.answer.copyWith(color: c.textMuted);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      spacing: 4,
      children: [
        Text(Strings.sources, style: s.thoughtSummary),
        for (final src in sources)
          Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            spacing: 8,
            children: [
              Text('${src.n}.', style: muted),
              Flexible(
                child: Semantics(
                  link: true,
                  child: Pressable(
                    onPressed: () => openLink(src.url),
                    radius: 4,
                    child: Text(
                      src.title.isEmpty ? src.url : src.title,
                      style: s.answer.copyWith(
                        color: c.accent,
                        decoration: TextDecoration.underline,
                      ),
                    ),
                  ),
                ),
              ),
              if (src.publishedDate case final date?)
                Text('· $date', style: muted),
            ],
          ),
      ],
    );
  }
}

/// "› Pensamiento · N palabras", plegable por mensaje. Plegado por defecto;
/// mientras llega sin texto, abierto con las últimas líneas, y se pliega solo
/// con el primer texto sin que salte el scroll.
class ThoughtView extends StatefulWidget {
  final Entry entry;
  const ThoughtView(this.entry, {super.key});

  @override
  State<ThoughtView> createState() => _ThoughtViewState();
}

class _ThoughtViewState extends State<ThoughtView>
    with SingleTickerProviderStateMixin {
  /// null: sigue la regla automática; si no, lo que eligió el usuario.
  bool? _userOpen;
  final _content = GlobalKey();
  late final _fold = AnimationController(vsync: this, value: _open ? 1 : 0)
    ..addListener(_keepAnswerInPlace);
  bool _autoFolding = false;
  double _lastFold = 1;

  static bool _live(Entry e) => e.inProgress && e.text.isEmpty;
  bool get _open => _userOpen ?? _live(widget.entry);

  @override
  void didUpdateWidget(ThoughtView old) {
    super.didUpdateWidget(old);
    if (_userOpen == null && _live(old.entry) != _live(widget.entry)) {
      _animate(auto: true);
    }
  }

  void _toggle() {
    setState(() => _userOpen = !_open);
    _animate(auto: false);
  }

  void _animate({required bool auto}) {
    final motion = Motion.of(context);
    _autoFolding = auto && !_open;
    _lastFold = _fold.value;
    _fold.duration = auto ? motion.thinkingAutoFold : motion.thinkingFold;
    _open ? _fold.forward() : _fold.reverse();
  }

  /// Al plegarse solo, sube la lista lo que se encoge el bloque.
  void _keepAnswerInPlace() {
    if (!_autoFolding) return;
    final height = _content.currentContext?.size?.height ?? 0;
    final delta = height * (_lastFold - _fold.value);
    _lastFold = _fold.value;
    if (delta > 0) ScrollAnchor.maybeOf(context)?.keepBelow(context, delta);
    if (_fold.isDismissed) _autoFolding = false;
  }

  @override
  void dispose() {
    _fold.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    // También mientras se pliega solo, para que no crezca antes de plegarse.
    final live = (_live(widget.entry) && _userOpen == null) || _autoFolding;
    final text = MarkdownView(widget.entry.reasoning, thought: true);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Transform.translate(
          offset: const Offset(-4, 0),
          child: Semantics(
            expanded: _open,
            child: Pressable(
              onPressed: _toggle,
              radius: 8,
              minHeight: 32,
              hoverColor: c.field,
              padding: const EdgeInsets.only(left: 4, right: 8),
              child: Row(
                mainAxisSize: MainAxisSize.min,
                spacing: 6,
                children: [
                  RotationTransition(
                    turns: Tween(begin: 0.0, end: 0.25).animate(_fold),
                    child: Icon(
                      Icons.chevron_right_rounded,
                      size: 16,
                      color: c.textMuted,
                    ),
                  ),
                  Text(
                    thoughtSummary(widget.entry.reasoning),
                    style: s.thoughtSummary,
                  ),
                ],
              ),
            ),
          ),
        ),
        SizeTransition(
          sizeFactor: _fold,
          alignment: AlignmentDirectional.topStart,
          child: Padding(
            key: _content,
            padding: const EdgeInsets.only(top: 6, left: 22),
            child: live
                // Las últimas ~6 líneas mientras llega.
                ? ConstrainedBox(
                    constraints: BoxConstraints(
                      maxHeight: s.thought.fontSize! * s.thought.height! * 6,
                    ),
                    child: ClipRect(
                      child: OverflowBox(
                        alignment: Alignment.bottomLeft,
                        maxHeight: double.infinity,
                        fit: OverflowBoxFit.deferToChild,
                        child: text,
                      ),
                    ),
                  )
                : text,
          ),
        ),
      ],
    );
  }
}
