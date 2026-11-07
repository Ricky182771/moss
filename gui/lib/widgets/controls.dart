import 'dart:async';

import 'package:flutter/material.dart';

import '../theme/colors.dart';
import '../theme/motion.dart';
import '../theme/theme.dart';

/// Botón sin fondo: el acento suave al pasar el mouse y el anillo de 2 px en
/// el acento con el foco del teclado.
class Pressable extends StatefulWidget {
  final VoidCallback? onPressed;
  final Widget child;

  /// Para los botones de solo ícono.
  final String? label;
  final Color? color;
  final Color? hoverColor;
  final double radius;
  final EdgeInsetsGeometry padding;
  final double minHeight;
  final bool selected;
  final FocusNode? focusNode;

  const Pressable({
    super.key,
    required this.onPressed,
    required this.child,
    this.label,
    this.color,
    this.hoverColor,
    this.radius = 10,
    this.padding = EdgeInsets.zero,
    this.minHeight = 0,
    this.selected = false,
    this.focusNode,
  });

  @override
  State<Pressable> createState() => _PressableState();
}

class _PressableState extends State<Pressable> {
  bool _hover = false;
  bool _focus = false;

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final enabled = widget.onPressed != null;
    final radius = BorderRadius.circular(widget.radius);
    return Semantics(
      button: true,
      enabled: enabled,
      selected: widget.selected,
      label: widget.label,
      child: FocusableActionDetector(
        enabled: enabled,
        focusNode: widget.focusNode,
        mouseCursor: enabled ? SystemMouseCursors.click : MouseCursor.defer,
        onShowHoverHighlight: (v) => setState(() => _hover = v),
        onShowFocusHighlight: (v) => setState(() => _focus = v),
        actions: {
          ActivateIntent: CallbackAction<ActivateIntent>(
            onInvoke: (_) => widget.onPressed?.call(),
          ),
        },
        child: GestureDetector(
          onTap: widget.onPressed,
          behavior: HitTestBehavior.opaque,
          child: AnimatedContainer(
            duration: Motion.of(context).hover,
            curve: Motion.enter,
            constraints: BoxConstraints(minHeight: widget.minHeight),
            padding: widget.padding,
            decoration: BoxDecoration(
              color: _hover && enabled
                  ? widget.hoverColor ?? c.accentSoft
                  : widget.color ?? Colors.transparent,
              borderRadius: radius,
            ),
            foregroundDecoration: _focus
                ? BoxDecoration(
                    border: Border.all(color: c.accent, width: 2),
                    borderRadius: radius,
                  )
                : null,
            child: widget.child,
          ),
        ),
      ),
    );
  }
}

/// Etiqueta corta de un botón de ícono: aparece tras ~400 ms de hover, o al
/// instante con el foco del teclado (opacidad + sube 4 px).
class HoverLabel extends StatefulWidget {
  final String label;
  final Widget child;

  /// Abajo del botón, para los del borde de arriba de la ventana.
  final bool below;

  const HoverLabel({
    super.key,
    required this.label,
    required this.child,
    this.below = false,
  });

  @override
  State<HoverLabel> createState() => _HoverLabelState();
}

class _HoverLabelState extends State<HoverLabel>
    with SingleTickerProviderStateMixin {
  final _portal = OverlayPortalController();
  final _link = LayerLink();
  late final _anim = AnimationController(vsync: this)
    ..addStatusListener((s) {
      if (s == AnimationStatus.dismissed) _portal.hide();
    });
  Timer? _timer;

  void _show() {
    _timer?.cancel();
    if (!mounted) return;
    _portal.show();
    _anim.duration = Motion.of(context).tooltip;
    _anim.forward();
  }

  void _hide() {
    _timer?.cancel();
    _anim.duration = Motion.of(context).tooltip;
    _anim.reverse();
  }

  @override
  void dispose() {
    _timer?.cancel();
    _anim.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final below = widget.below;
    return CompositedTransformTarget(
      link: _link,
      child: OverlayPortal(
        controller: _portal,
        overlayChildBuilder: (context) => Align(
          alignment: Alignment.topLeft,
          child: CompositedTransformFollower(
            link: _link,
            targetAnchor: below ? Alignment.bottomCenter : Alignment.topCenter,
            followerAnchor: below
                ? Alignment.topCenter
                : Alignment.bottomCenter,
            offset: Offset(0, below ? 8 : -8),
            child: IgnorePointer(
              child: ExcludeSemantics(
                child: AnimatedBuilder(
                  animation: _anim,
                  builder: (context, child) => Opacity(
                    opacity: Motion.enter.transform(_anim.value),
                    child: Transform.translate(
                      offset: Offset(
                        0,
                        4 * (1 - Motion.enter.transform(_anim.value)),
                      ),
                      child: child,
                    ),
                  ),
                  child: DecoratedBox(
                    decoration: BoxDecoration(
                      color: context.colors.text,
                      borderRadius: BorderRadius.circular(6),
                    ),
                    child: Padding(
                      padding: const EdgeInsets.symmetric(
                        horizontal: 8,
                        vertical: 4,
                      ),
                      child: Text(widget.label, style: context.styles.tooltip),
                    ),
                  ),
                ),
              ),
            ),
          ),
        ),
        child: MouseRegion(
          onEnter: (_) {
            _timer?.cancel();
            _timer = Timer(Motion.tooltipDelay, _show);
          },
          onExit: (_) => _hide(),
          child: Focus(
            canRequestFocus: false,
            skipTraversal: true,
            onFocusChange: (focused) {
              final keyboard =
                  FocusManager.instance.highlightMode ==
                  FocusHighlightMode.traditional;
              focused && keyboard ? _show() : _hide();
            },
            child: widget.child,
          ),
        ),
      ),
    );
  }
}

/// Botón de solo ícono de 44 px, con su etiqueta y su `Semantics`.
class IconAction extends StatelessWidget {
  final Widget icon;
  final String label;
  final VoidCallback? onPressed;
  final bool labelBelow;
  final FocusNode? focusNode;

  /// Fondo fijo (el engranaje con Opciones abierta).
  final Color? color;
  final double size;

  const IconAction({
    super.key,
    required this.icon,
    required this.label,
    required this.onPressed,
    this.labelBelow = false,
    this.focusNode,
    this.color,
    this.size = 44,
  });

  @override
  Widget build(BuildContext context) {
    return HoverLabel(
      label: label,
      below: labelBelow,
      child: Pressable(
        onPressed: onPressed,
        label: label,
        focusNode: focusNode,
        color: color,
        radius: size < 44 ? size / 2 : 10,
        child: SizedBox.square(
          dimension: size,
          child: Center(child: ExcludeSemantics(child: icon)),
        ),
      ),
    );
  }
}

/// Ícono de la barra lateral de las maquetas: un marco con una columna a la
/// izquierda. Material no tiene uno parecido.
class SidebarIcon extends StatelessWidget {
  final Color color;
  const SidebarIcon({super.key, required this.color});

  @override
  Widget build(BuildContext context) =>
      CustomPaint(size: const Size.square(20), painter: _SidebarPainter(color));
}

class _SidebarPainter extends CustomPainter {
  final Color color;
  _SidebarPainter(this.color);

  @override
  void paint(Canvas canvas, Size size) {
    canvas.scale(size.width / 24);
    final p = Paint()
      ..color = color
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2
      ..strokeJoin = StrokeJoin.round;
    canvas.drawRRect(
      RRect.fromRectAndRadius(
        const Rect.fromLTWH(3, 4, 18, 16),
        const Radius.circular(3),
      ),
      p,
    );
    canvas.drawLine(const Offset(9, 4), const Offset(9, 20), p);
  }

  @override
  bool shouldRepaint(_SidebarPainter old) => old.color != color;
}
