import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../theme/colors.dart';
import '../theme/motion.dart';

/// Ventana chica que flota anclada a [link] (Opciones, menú `⋯`, menú de
/// `/`): opacidad + escala 0.98 → 1 desde [followerAnchor], 140 ms al abrir y
/// 100 al cerrar. El que la contiene decide con [open] si se ve.
class Popover extends StatefulWidget {
  final bool open;
  final LayerLink link;
  final Alignment targetAnchor;
  final Alignment followerAnchor;
  final Offset offset;

  /// Esc o un clic afuera. null: no se cierra sola (el menú de `/` lo
  /// maneja la caja) y deja pasar los clics.
  final VoidCallback? onDismiss;

  /// A dónde vuelve el foco al cerrarse.
  final FocusNode? returnFocus;
  final WidgetBuilder builder;
  final Widget child;

  const Popover({
    super.key,
    required this.open,
    required this.link,
    required this.builder,
    required this.child,
    this.targetAnchor = Alignment.topLeft,
    this.followerAnchor = Alignment.bottomLeft,
    this.offset = Offset.zero,
    this.onDismiss,
    this.returnFocus,
  });

  @override
  State<Popover> createState() => _PopoverState();
}

class _PopoverState extends State<Popover> with SingleTickerProviderStateMixin {
  final _portal = OverlayPortalController();
  late final _anim = AnimationController(vsync: this);
  late final _curved = CurvedAnimation(parent: _anim, curve: Motion.enter);

  @override
  void initState() {
    super.initState();
    if (widget.open) _showAfterFrame();
  }

  // OverlayPortalController.show no se puede llamar durante el build.
  void _showAfterFrame() =>
      WidgetsBinding.instance.addPostFrameCallback((_) => _show());

  @override
  void didUpdateWidget(Popover old) {
    super.didUpdateWidget(old);
    if (widget.open != old.open) widget.open ? _showAfterFrame() : _hide();
  }

  void _show() {
    if (!mounted || !widget.open) return;
    final motion = Motion.of(context);
    _anim.duration = motion.menuOpen;
    _anim.reverseDuration = motion.menuClose;
    _portal.show();
    _anim.forward();
  }

  Future<void> _hide() async {
    await _anim.reverse().orCancel.catchError((_) {});
    if (!mounted || widget.open) return;
    // Si el foco sigue adentro (Esc, clic afuera), vuelve a donde estaba; si
    // ya se fue a otro lado (el campo de Renombrar), se queda ahí.
    final inside = _scope.hasFocus;
    _portal.hide();
    if (inside) widget.returnFocus?.requestFocus();
  }

  final _scope = FocusScopeNode(debugLabel: 'Popover');

  @override
  void dispose() {
    _scope.dispose();
    _curved.dispose();
    _anim.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return OverlayPortal(
      controller: _portal,
      overlayChildBuilder: (context) {
        final dismiss = widget.onDismiss;
        Widget popup = FadeTransition(
          opacity: _curved,
          child: ScaleTransition(
            scale: Tween(begin: 0.98, end: 1.0).animate(_curved),
            alignment: widget.followerAnchor,
            child: widget.builder(context),
          ),
        );
        if (dismiss != null) {
          popup = CallbackShortcuts(
            bindings: {
              const SingleActivator(LogicalKeyboardKey.escape): dismiss,
            },
            child: FocusScope(node: _scope, child: popup),
          );
        }
        return Stack(
          children: [
            if (dismiss != null)
              Positioned.fill(
                child: GestureDetector(
                  behavior: HitTestBehavior.opaque,
                  onTap: dismiss,
                  onSecondaryTap: dismiss,
                ),
              ),
            Align(
              alignment: Alignment.topLeft,
              child: CompositedTransformFollower(
                link: widget.link,
                showWhenUnlinked: false,
                targetAnchor: widget.targetAnchor,
                followerAnchor: widget.followerAnchor,
                offset: widget.offset,
                child: IgnorePointer(ignoring: !widget.open, child: popup),
              ),
            ),
          ],
        );
      },
      child: CompositedTransformTarget(link: widget.link, child: widget.child),
    );
  }
}

/// El marco de una ventana flotante: fondo, borde, esquinas y sombra.
class PopoverFrame extends StatelessWidget {
  final double width;
  final EdgeInsetsGeometry padding;
  final double radius;
  final Color? color;
  final Widget child;

  const PopoverFrame({
    super.key,
    required this.width,
    required this.child,
    this.padding = const EdgeInsets.all(6),
    this.radius = 12,
    this.color,
  });

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    return Material(
      type: MaterialType.transparency,
      child: Container(
        width: width,
        padding: padding,
        decoration: BoxDecoration(
          color: color ?? c.field,
          borderRadius: BorderRadius.circular(radius),
          border: Border.all(color: c.border),
          boxShadow: [
            BoxShadow(
              color: c.shadow,
              offset: const Offset(0, 12),
              blurRadius: 32,
            ),
          ],
        ),
        child: child,
      ),
    );
  }
}
