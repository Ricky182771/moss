import 'dart:async';

import 'package:flutter/material.dart';

import '../state/app_state.dart';
import '../theme/colors.dart';
import '../theme/motion.dart';
import '../theme/theme.dart';

/// Aviso corto abajo al centro: opacidad + sube 6 px y se va solo a los 4 s.
/// No mueve nada.
class ToastHost extends StatefulWidget {
  final ValueNotifier<Toast?> toast;
  const ToastHost(this.toast, {super.key});

  @override
  State<ToastHost> createState() => _ToastHostState();
}

class _ToastHostState extends State<ToastHost>
    with SingleTickerProviderStateMixin {
  late final _anim = AnimationController(vsync: this);
  Timer? _timer;
  String _text = '';

  @override
  void initState() {
    super.initState();
    widget.toast.addListener(_show);
  }

  void _show() {
    final t = widget.toast.value;
    if (t == null) return;
    setState(() => _text = t.text);
    _anim.duration = Motion.of(context).toast;
    _anim.forward(from: 0);
    _timer?.cancel();
    _timer = Timer(Motion.toastVisible, () {
      if (mounted) _anim.reverse();
    });
  }

  @override
  void dispose() {
    widget.toast.removeListener(_show);
    _timer?.cancel();
    _anim.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    return IgnorePointer(
      child: AnimatedBuilder(
        animation: _anim,
        builder: (context, child) => _anim.isDismissed
            ? const SizedBox.shrink()
            : Opacity(
                opacity: Motion.enter.transform(_anim.value),
                child: Transform.translate(
                  offset: Offset(0, 6 * (1 - _anim.value)),
                  child: child,
                ),
              ),
        child: Semantics(
          liveRegion: true,
          child: Container(
            constraints: const BoxConstraints(maxWidth: 560),
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
            decoration: BoxDecoration(
              color: c.text,
              borderRadius: BorderRadius.circular(10),
            ),
            child: Text(
              _text,
              style: context.styles.toast,
              textAlign: TextAlign.center,
            ),
          ),
        ),
      ),
    );
  }
}
