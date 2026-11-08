import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../rpc/client.dart';
import '../state/log_lines.dart';
import '../state/log_state.dart';
import '../strings.dart';
import '../theme/colors.dart';
import '../theme/motion.dart';
import '../theme/theme.dart';
import 'controls.dart';
import 'indicators.dart';
import 'settings_form.dart';

/// Ver registro (DISENO.md, maqueta `Config-registro`): ventana sobre
/// Configuración con el fondo oscurecido; aparece y sube 8 px (180 ms), como
/// Buscar chats. Al cerrarse (Esc, Cerrar o el clic afuera), el foco regresa
/// a [returnFocus] (Ver registro).
Future<void> showLogDialog(
  BuildContext context, {
  required RpcClient rpc,
  FocusNode? returnFocus,
  required DateTime Function() now,
}) async {
  final motion = Motion.of(context);
  await showGeneralDialog<void>(
    context: context,
    barrierDismissible: true,
    barrierLabel: Strings.closeLog,
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
    pageBuilder: (context, _, _) => LogDialog(rpc: rpc, now: now),
  );
  if (returnFocus?.context != null) returnFocus!.requestFocus();
}

class LogDialog extends StatefulWidget {
  final RpcClient rpc;
  final DateTime Function() now;
  const LogDialog({super.key, required this.rpc, required this.now});

  @override
  State<LogDialog> createState() => _LogDialogState();
}

class _LogDialogState extends State<LogDialog> {
  late final _state = LogState(widget.rpc)..addListener(_changed);
  final _scroll = ScrollController();
  int _shown = 0;
  bool _copied = false;
  Timer? _copiedTimer;

  @override
  void initState() {
    super.initState();
    _state.load();
  }

  /// Con cada respuesta, abajo (la lista va al revés: 0 es abajo).
  void _changed() {
    if (_state.generation != _shown) {
      _shown = _state.generation;
      if (_scroll.hasClients) _scroll.jumpTo(0);
    }
    setState(() {});
  }

  /// Todas las líneas completas, tal como llegaron.
  Future<void> _copy() async {
    await Clipboard.setData(ClipboardData(text: _state.lines.join('\n')));
    _copiedTimer?.cancel();
    if (!mounted) return;
    setState(() => _copied = true);
    _copiedTimer = Timer(Motion.copiedVisible, () {
      if (mounted) setState(() => _copied = false);
    });
  }

  @override
  void dispose() {
    _copiedTimer?.cancel();
    _state.dispose(); // `$/cancel` si seguía leyendo.
    _scroll.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final line = BorderSide(color: c.border);
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(48),
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 920, maxHeight: 720),
          child: SizedBox.expand(
            child: Material(
              color: c.sidebar,
              clipBehavior: Clip.antiAlias,
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(16),
                side: line,
              ),
              child: Semantics(
                scopesRoute: true,
                namesRoute: true,
                label: Strings.logTitle,
                explicitChildNodes: true,
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    _header(context),
                    Expanded(
                      child: ColoredBox(
                        color: c.background,
                        child: _body(context),
                      ),
                    ),
                    Container(
                      padding: const EdgeInsets.symmetric(
                        horizontal: 22,
                        vertical: 12,
                      ),
                      decoration: BoxDecoration(border: Border(top: line)),
                      child: Wrap(
                        alignment: WrapAlignment.spaceBetween,
                        spacing: 12,
                        runSpacing: 4,
                        children: [
                          Text(Strings.newestBelow, style: s.note),
                          if (_state.truncated)
                            Text(Strings.linesTruncated, style: s.note),
                        ],
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }

  Widget _header(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    return Container(
      padding: const EdgeInsets.fromLTRB(22, 14, 14, 14),
      decoration: BoxDecoration(
        border: Border(bottom: BorderSide(color: c.border)),
      ),
      child: Row(
        spacing: 12,
        children: [
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              spacing: 2,
              children: [
                Semantics(
                  header: true,
                  child: Text(Strings.logTitle, style: s.cardTitle),
                ),
                Text(Strings.logSubtitle, style: s.kbd),
              ],
            ),
          ),
          SecondaryButton(
            label: Strings.refreshLog,
            icon: Icons.refresh_rounded,
            bordered: false,
            onPressed: _state.load,
            waiting: _state.loading,
            slow: _state.slow,
          ),
          Semantics(
            label: _copied ? Strings.copied : Strings.copy,
            excludeSemantics: true,
            button: true,
            enabled: _state.canCopy,
            child: Pressable(
              onPressed: _state.canCopy ? _copy : null,
              minHeight: 44,
              padding: const EdgeInsets.symmetric(horizontal: 16),
              child: AnimatedSwitcher(
                duration: Motion.of(context).copySwap,
                switchInCurve: Motion.enter,
                switchOutCurve: Motion.enter,
                child: _copied
                    ? Text(
                        Strings.copied,
                        key: const ValueKey(true),
                        style: s.row,
                      )
                    : Row(
                        key: const ValueKey(false),
                        mainAxisSize: MainAxisSize.min,
                        spacing: 8,
                        children: [
                          Icon(
                            Icons.content_copy_rounded,
                            size: 16,
                            color: _state.canCopy ? c.text : c.textMuted,
                          ),
                          Text(
                            Strings.copy,
                            style: _state.canCopy
                                ? s.row
                                : s.row.copyWith(color: c.textMuted),
                          ),
                        ],
                      ),
              ),
            ),
          ),
          IconAction(
            label: Strings.closeLog,
            size: 40,
            onPressed: () => Navigator.of(context).pop(),
            icon: Icon(Icons.close_rounded, size: 18, color: c.textMuted),
          ),
        ],
      ),
    );
  }

  Widget _body(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final st = _state;
    Widget message(Widget child) => Padding(
      padding: const EdgeInsets.symmetric(horizontal: 22, vertical: 16),
      child: Align(alignment: Alignment.topLeft, child: child),
    );
    if (st.slow) {
      return message(
        Semantics(
          liveRegion: true,
          child: Row(
            mainAxisSize: MainAxisSize.min,
            spacing: 8,
            children: [
              const Spinner(),
              Text(Strings.readingLog, style: s.note),
            ],
          ),
        ),
      );
    }
    if (st.error case final error?) {
      return message(
        Semantics(
          liveRegion: true,
          child: SelectableText(error, style: s.note.copyWith(color: c.error)),
        ),
      );
    }
    if (st.lines.isEmpty) {
      return st.loading
          ? const SizedBox.shrink()
          : message(Text(st.notice, style: s.note));
    }
    final items = logItems(st.lines, st.errors, widget.now());
    return Semantics(
      label: Strings.logLines,
      container: true,
      child: Focus(
        autofocus: true,
        child: SelectionArea(
          child: Scrollbar(
            controller: _scroll,
            // Al revés (abajo al abrir: 0 es la más reciente), pero con pocas
            // líneas empiezan arriba, como en la maqueta.
            child: LayoutBuilder(
              builder: (context, box) => SingleChildScrollView(
                controller: _scroll,
                reverse: true,
                child: ConstrainedBox(
                  constraints: BoxConstraints(minHeight: box.maxHeight),
                  child: Padding(
                    padding: const EdgeInsets.only(top: 6, bottom: 12),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        for (final item in items)
                          switch (item) {
                            LogDay(:final label) => Padding(
                              padding: const EdgeInsets.fromLTRB(12, 10, 12, 4),
                              child: Text(label.toUpperCase(), style: s.group),
                            ),
                            LogEntry(:final line) => _LogRow(line),
                          },
                      ],
                    ),
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

/// Una línea: la hora en su columna de 72 px y el mensaje. Los errores, con
/// la barra roja a la izquierda y el texto en el color de error (no solo el
/// color).
class _LogRow extends StatelessWidget {
  final LogLine line;
  const _LogRow(this.line);

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final text = line.error ? s.preview.copyWith(color: c.error) : s.preview;
    final time = line.error ? text : s.preview.copyWith(color: c.textMuted);
    return Container(
      padding: const EdgeInsets.fromLTRB(10, 1, 12, 1),
      decoration: BoxDecoration(
        border: Border(
          left: BorderSide(
            color: line.error ? c.error : c.background,
            width: 2,
          ),
        ),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        spacing: 12,
        children: [
          if (line.time case final t?)
            SizedBox(width: 72, child: Text(t, style: time)),
          Expanded(child: Text(line.message, style: text)),
        ],
      ),
    );
  }
}
