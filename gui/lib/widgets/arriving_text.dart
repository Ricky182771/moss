import 'package:flutter/rendering.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';

import '../state/arriving.dart';
import '../theme/colors.dart';
import '../theme/motion.dart';

/// Hace aparecer, con opacidad 0 → 1 en [Motion.word], el texto que se
/// agregó a [child] (el Markdown de la respuesta que llega). Cada cambio de
/// [text] es un grupo: lo que llegó junto aparece junto.
///
/// No toca el Markdown ni lo reconstruye por cuadro: después de pintar a
/// [child], tapa el texto nuevo con el color de lo que tiene detrás, cada vez
/// más transparente (solo se repinta). El texto nuevo se encuentra por su
/// lugar en el texto ya dibujado (los RenderParagraph en orden), no en el
/// Markdown.
class ArrivingText extends StatefulWidget {
  /// El texto que se muestra (para saber cuándo llegó algo y cuántas
  /// palabras trae).
  final String text;
  final Widget child;

  const ArrivingText({super.key, required this.text, required this.child});

  /// Cuántos grupos se están animando en el [ArrivingText] de [element].
  @visibleForTesting
  static int debugGroups(Element element) =>
      ((element as StatefulElement).state as _ArrivingTextState)
          ._render
          ?.debugGroups ??
      0;

  @override
  State<ArrivingText> createState() => _ArrivingTextState();
}

class _ArrivingTextState extends State<ArrivingText>
    with SingleTickerProviderStateMixin {
  final _key = GlobalKey();
  late final _ticker = createTicker(_tick);

  _RenderArriving? get _render =>
      _key.currentContext?.findRenderObject() as _RenderArriving?;

  @override
  void didUpdateWidget(ArrivingText old) {
    super.didUpdateWidget(old);
    final t = widget.text;
    if (t.length > old.text.length && t.startsWith(old.text)) {
      _render?.arrived(countWords(t.substring(old.text.length)));
      if (!_ticker.isActive) _ticker.start();
    }
  }

  void _tick(Duration _) {
    final r = _render;
    if (r == null || !r.animating) {
      _ticker.stop();
    } else {
      r.markNeedsPaint();
    }
  }

  @override
  void dispose() {
    _ticker.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => _Arriving(
    key: _key,
    duration: Motion.of(context).word,
    background: context.colors.background,
    child: widget.child,
  );
}

class _Arriving extends SingleChildRenderObjectWidget {
  final Duration duration;
  final Color background;

  const _Arriving({
    super.key,
    required this.duration,
    required this.background,
    required super.child,
  });

  @override
  _RenderArriving createRenderObject(BuildContext context) =>
      _RenderArriving(duration, background);

  @override
  void updateRenderObject(BuildContext context, _RenderArriving r) => r
    ..duration = duration
    ..background = background;
}

/// Un grupo de texto que llegó junto: `[start, end)` en el texto dibujado.
class _Group {
  final int start;
  final int end;
  final int words;
  final Duration at;
  _Group(this.start, this.end, this.words, this.at);
}

class _RenderArriving extends RenderProxyBox {
  _RenderArriving(this.duration, this.background);

  Duration duration;
  Color background;

  /// Cuánto texto había dibujado la última vez; null antes del primer
  /// dibujo (lo que ya estaba no se anima).
  int? _known;

  /// Palabras del trozo que llegó y todavía no se dibuja.
  int? _pending;
  final _groups = <_Group>[];

  bool get animating => _groups.isNotEmpty || _pending != null;

  @visibleForTesting
  int get debugGroups => _groups.length;

  void arrived(int words) {
    _pending = (_pending ?? 0) + words;
    markNeedsPaint();
  }

  /// Los párrafos dibujados, en orden, con dónde empieza cada uno.
  List<(RenderParagraph, int)> _paragraphs() {
    final out = <(RenderParagraph, int)>[];
    var at = 0;
    void visit(RenderObject o) {
      if (o is RenderParagraph) {
        out.add((o, at));
        at += o.text.toPlainText(includePlaceholders: false).length;
        return;
      }
      o.visitChildren(visit);
    }

    if (child case final c?) visit(c);
    _length = at;
    return out;
  }

  int _length = 0;

  @override
  void paint(PaintingContext context, Offset offset) {
    super.paint(context, offset);
    final now = SchedulerBinding.instance.currentFrameTimeStamp;
    final paragraphs = _paragraphs();
    final length = _length;
    final known = _known;
    _known = length;
    if (known == null || length < known) {
      // El primero, o el Markdown se reacomodó: nada que animar.
      _groups.clear();
      _pending = null;
      return;
    }
    if (length > known) {
      final words = _pending ?? 1;
      _pending = null;
      _groups.add(_Group(known, length, words, now));
      // Como mucho ~40 palabras animándose: lo más viejo termina ya, y una
      // ráfaga más grande aparece directo.
      var total = _groups.fold(0, (n, g) => n + g.words);
      while (_groups.isNotEmpty && total > kMaxAnimatedWords) {
        total -= _groups.removeAt(0).words;
      }
    }
    _groups.removeWhere((g) => now - g.at >= duration);
    if (_groups.isEmpty || duration == Duration.zero) return;

    final canvas = context.canvas;
    for (final g in _groups) {
      final t = (now - g.at).inMicroseconds / duration.inMicroseconds;
      final cover = 1 - Motion.enter.transform(t.clamp(0, 1));
      for (final (p, start) in paragraphs) {
        final len = p.text.toPlainText(includePlaceholders: false).length;
        final from = (g.start - start).clamp(0, len);
        final to = (g.end - start).clamp(0, len);
        if (from >= to) continue;
        final (color, clip) = _behind(p);
        final paint = Paint()..color = color.withValues(alpha: cover);
        final transform = p.getTransformTo(this);
        for (final box in p.getBoxesForSelection(
          TextSelection(baseOffset: from, extentOffset: to),
        )) {
          var rect = MatrixUtils.transformRect(transform, box.toRect());
          if (clip != null) rect = rect.intersect(clip);
          if (rect.isEmpty) continue;
          canvas.drawRect(rect.shift(offset), paint);
        }
      }
    }
  }

  /// El color de lo que hay detrás de [p] (el fondo del bloque de código que
  /// lo contiene, o el del chat) y, si es un bloque, su rectángulo para no
  /// tapar fuera de él (el código se desplaza de lado).
  (Color, Rect?) _behind(RenderParagraph p) {
    for (RenderObject? o = p.parent; o != null && o != this; o = o.parent) {
      if (o is! RenderDecoratedBox) continue;
      final decoration = o.decoration;
      if (decoration is BoxDecoration && decoration.color != null) {
        final box = MatrixUtils.transformRect(
          o.getTransformTo(this),
          Offset.zero & o.size,
        );
        return (decoration.color!, box);
      }
    }
    return (background, null);
  }
}
