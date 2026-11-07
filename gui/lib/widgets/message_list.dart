import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../models/conversation.dart';
import '../strings.dart';
import '../theme/colors.dart';
import '../theme/motion.dart';
import '../theme/theme.dart';
import 'entry_view.dart';
import 'indicators.dart';

/// Lo usa el Pensamiento que se pliega solo para que la respuesta no salte.
class ScrollAnchor extends InheritedWidget {
  final void Function(BuildContext source, double delta) keepBelow;

  const ScrollAnchor({
    super.key,
    required this.keepBelow,
    required super.child,
  });

  static ScrollAnchor? maybeOf(BuildContext context) =>
      context.getInheritedWidgetOfExactType<ScrollAnchor>();

  @override
  bool updateShouldNotify(ScrollAnchor old) => false;
}

/// Los mensajes en una columna de 760 px como máximo, en su propia zona
/// desplazable. Se queda pegada abajo mientras llega la respuesta solo si el
/// usuario ya estaba abajo.
class MessageList extends StatefulWidget {
  final ConversationSnapshot conversation;
  const MessageList(this.conversation, {super.key});

  @override
  State<MessageList> createState() => _MessageListState();
}

class _MessageListState extends State<MessageList> {
  final _scroll = ScrollController();

  /// El usuario está abajo (o acaba de enviar). Solo lo cambia el usuario.
  bool _pinned = true;
  bool _programmatic = false;

  /// La vista de cada entrada, por índice. `applyEvent` deja idénticas las
  /// entradas que no cambian; con la misma instancia del widget, Flutter no
  /// las reconstruye y un trozo nuevo reconstruye solo la última.
  final _views = <(Entry, Widget)>[];

  /// Las entradas desde aquí llegaron con la lista a la vista: tu mensaje
  /// aparece al enviarlo; lo que ya estaba al abrir, no. Un chat sin guardar
  /// (sin `id`) se acaba de empezar: su primer mensaje también aparece.
  late int _firstNew = _startOf(widget.conversation);

  static int _startOf(ConversationSnapshot c) =>
      c.id == null ? 0 : c.entries.length;

  Widget _view(int i, Entry e) {
    if (i < _views.length && identical(_views[i].$1, e)) return _views[i].$2;
    final key = ValueKey('${widget.conversation.conversation}/$i');
    final w = i >= _firstNew && e.kind == EntryKind.user
        ? _AppearUp(key: key, child: EntryView(e))
        : EntryView(e, key: key);
    if (i < _views.length) {
      _views[i] = (e, w);
    } else {
      _views.add((e, w));
    }
    return w;
  }

  @override
  void initState() {
    super.initState();
    _stickAfterFrame();
  }

  @override
  void didUpdateWidget(MessageList old) {
    super.didUpdateWidget(old);
    final now = widget.conversation;
    final sentOrSwitched =
        now.conversation != old.conversation.conversation ||
        (now.entries.length > old.conversation.entries.length &&
            now.entries.last.kind == EntryKind.user);
    if (sentOrSwitched) _pinned = true;
    if (now.conversation != old.conversation.conversation) {
      _views.clear();
      _firstNew = _startOf(now);
    } else if (now.entries.length < _firstNew) {
      _firstNew = now.entries.length; // Un resync la acortó.
    }
    if (_views.length > now.entries.length) {
      _views.removeRange(now.entries.length, _views.length);
    }
    _stickAfterFrame();
  }

  void _stickAfterFrame() {
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (_pinned && _scroll.hasClients) {
        _jump(_scroll.position.maxScrollExtent);
      }
    });
  }

  void _jump(double to) {
    _programmatic = true;
    _scroll.jumpTo(to);
    _programmatic = false;
  }

  void _keepBelow(BuildContext source, double delta) {
    if (_pinned || !_scroll.hasClients) return; // Abajo ya se queda abajo.
    final view = context.findRenderObject() as RenderBox?;
    final box = source.findRenderObject() as RenderBox?;
    if (view == null || box == null || !box.attached) return;
    final bottom = box
        .localToGlobal(Offset(0, box.size.height), ancestor: view)
        .dy;
    // Solo si lo de abajo del bloque (la respuesta) está a la vista o arriba.
    if (bottom < view.size.height) {
      _jump(math.max(0, _scroll.offset - delta));
    }
  }

  @override
  void dispose() {
    _scroll.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final conv = widget.conversation;
    final entries = conv.entries;
    final last = entries.lastOrNull;
    final thoughtArriving =
        last != null &&
        last.kind == EntryKind.assistant &&
        last.inProgress &&
        last.reasoning.isNotEmpty;
    final s = context.styles;
    final muted = s.answer.copyWith(color: context.colors.textMuted);
    final Widget? phase = switch (conv.phase) {
      Phase.searching => Text(Strings.searching, style: muted),
      Phase.thinking when !thoughtArriving => Pulse(
        child: Text(Strings.thinking, style: muted),
      ),
      _ => null,
    };

    final list = _list(context, phase);
    // Si la zona cambia de alto (la caja crece a dos filas), abajo se queda
    // abajo.
    return LayoutBuilder(
      builder: (context, box) {
        if (box.maxHeight != _height) {
          _height = box.maxHeight;
          _stickAfterFrame();
        }
        return list;
      },
    );
  }

  double? _height;

  Widget _list(BuildContext context, Widget? phase) {
    final conv = widget.conversation;
    final entries = conv.entries;
    return ScrollAnchor(
      keepBelow: _keepBelow,
      child: NotificationListener<ScrollUpdateNotification>(
        onNotification: (n) {
          if (!_programmatic && n.depth == 0) {
            _pinned = n.metrics.extentAfter <= 24;
          }
          return false;
        },
        child: SelectionArea(
          // La barra de desplazamiento la pone el ScrollBehavior de escritorio.
          child: SingleChildScrollView(
            controller: _scroll,
            padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 32),
            // shortcut: arma todas las entradas; con chats muy largos, cambiar
            // a una lista perezosa (y recalcular el pegado abajo).
            child: Center(
              child: ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 760),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  spacing: 28,
                  children: [
                    for (final (i, e) in entries.indexed) _view(i, e),
                    if (phase != null)
                      AssistantRow(
                        child: Semantics(liveRegion: true, child: phase),
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
}

/// Tu mensaje al enviarlo: opacidad + sube 6 px (160 ms).
class _AppearUp extends StatelessWidget {
  final Widget child;
  const _AppearUp({super.key, required this.child});

  @override
  Widget build(BuildContext context) => TweenAnimationBuilder<double>(
    tween: Tween(begin: 0, end: 1),
    duration: Motion.of(context).messageIn,
    curve: Motion.enter,
    child: child,
    builder: (context, t, child) => Opacity(
      opacity: t,
      child: Transform.translate(offset: Offset(0, 6 * (1 - t)), child: child),
    ),
  );
}
