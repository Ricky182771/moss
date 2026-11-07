import 'package:flutter/material.dart';

import '../theme/colors.dart';
import '../theme/motion.dart';
import '../theme/theme.dart';

/// Opacidad que se repite: "Pensando…" pulsa (1.2 s) y el cursor parpadea
/// (1 s). Con Reducir animaciones queda fijo.
class _Repeating extends StatefulWidget {
  final Duration Function(Motion) period;
  final bool blink;
  final Widget child;
  const _Repeating({
    required this.period,
    required this.blink,
    required this.child,
  });

  @override
  State<_Repeating> createState() => _RepeatingState();
}

class _RepeatingState extends State<_Repeating>
    with SingleTickerProviderStateMixin {
  late final _c = AnimationController(vsync: this, value: 1);

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final motion = Motion.of(context);
    if (motion.reduced) {
      _c.value = 1;
    } else {
      _c.repeat(reverse: !widget.blink, period: widget.period(motion));
    }
  }

  @override
  void dispose() {
    _c.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
    animation: _c,
    builder: (context, child) => Opacity(
      // El cursor parpadea encendido/apagado; "Pensando…" va de 0.4 a 1.
      opacity: widget.blink ? (_c.value < 0.5 ? 1 : 0) : 0.4 + 0.6 * _c.value,
      child: child,
    ),
    child: widget.child,
  );
}

/// "Pensando…", que pulsa suave.
class Pulse extends StatelessWidget {
  final Widget child;
  const Pulse({super.key, required this.child});

  @override
  Widget build(BuildContext context) =>
      _Repeating(period: (m) => m.thinkingPulse, blink: false, child: child);
}

/// El cursor verde de bloque al final de la respuesta que llega.
class StreamCursor extends StatelessWidget {
  const StreamCursor({super.key});

  @override
  Widget build(BuildContext context) {
    final em = context.styles.answer.fontSize!;
    return ExcludeSemantics(
      child: _Repeating(
        period: (m) => m.cursorBlink,
        blink: true,
        child: Container(
          width: 0.55 * em,
          height: 1.1 * em,
          margin: const EdgeInsets.only(left: 2),
          decoration: BoxDecoration(
            color: context.colors.accent,
            borderRadius: BorderRadius.circular(2),
          ),
        ),
      ),
    );
  }
}
