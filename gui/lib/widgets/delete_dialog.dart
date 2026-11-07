import 'package:flutter/material.dart';

import '../strings.dart';
import '../theme/colors.dart';
import '../theme/motion.dart';
import '../theme/theme.dart';
import 'controls.dart';

/// "¿Borrar «título»? No se puede deshacer." con Cancelar y Borrar. [delete]
/// devuelve el error del servidor (Busy…), que se queda en la ventana, o null
/// si se borró. Esc o Cancelar cierran sin borrar.
Future<void> confirmDelete(
  BuildContext context, {
  required String title,
  required Future<String?> Function() delete,
}) {
  final motion = Motion.of(context);
  return showGeneralDialog(
    context: context,
    barrierDismissible: true,
    barrierLabel: Strings.cancel,
    barrierColor: context.colors.scrim,
    transitionDuration: motion.menuOpen,
    transitionBuilder: (context, anim, _, child) {
      final t = CurvedAnimation(parent: anim, curve: Motion.enter);
      return FadeTransition(
        opacity: t,
        child: ScaleTransition(
          scale: Tween(begin: 0.98, end: 1.0).animate(t),
          child: child,
        ),
      );
    },
    pageBuilder: (context, _, _) => _DeleteDialog(title: title, delete: delete),
  );
}

class _DeleteDialog extends StatefulWidget {
  final String title;
  final Future<String?> Function() delete;
  const _DeleteDialog({required this.title, required this.delete});

  @override
  State<_DeleteDialog> createState() => _DeleteDialogState();
}

class _DeleteDialogState extends State<_DeleteDialog> {
  String? _error;
  bool _busy = false;

  /// El foco empieza en Cancelar: Enter no borra sin querer.
  final _cancel = FocusNode(debugLabel: Strings.cancel);

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => _cancel.requestFocus());
  }

  @override
  void dispose() {
    _cancel.dispose();
    super.dispose();
  }

  Future<void> _delete() async {
    setState(() {
      _busy = true;
      _error = null;
    });
    final error = await widget.delete();
    if (!mounted) return;
    if (error == null) {
      Navigator.of(context).pop();
    } else {
      setState(() {
        _busy = false;
        _error = error;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final question = Strings.deleteQuestion(widget.title);
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 420),
          child: Material(
            color: c.sidebar,
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(16),
              side: BorderSide(color: c.border),
            ),
            child: Semantics(
              scopesRoute: true,
              namesRoute: true,
              label: question,
              explicitChildNodes: true,
              child: Padding(
                padding: const EdgeInsets.all(24),
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  spacing: 8,
                  children: [
                    Text(question, style: s.title),
                    Text(
                      Strings.cannotUndo,
                      style: s.row.copyWith(color: c.textMuted),
                    ),
                    AnimatedSwitcher(
                      duration: Motion.of(context).fieldError,
                      switchInCurve: Motion.enter,
                      switchOutCurve: Motion.enter,
                      child: _error == null
                          ? const SizedBox.shrink()
                          : Semantics(
                              key: ValueKey(_error),
                              liveRegion: true,
                              child: Text(
                                _error!,
                                style: s.help.copyWith(color: c.error),
                              ),
                            ),
                    ),
                    const SizedBox(height: 8),
                    Row(
                      mainAxisAlignment: MainAxisAlignment.end,
                      spacing: 8,
                      children: [
                        Pressable(
                          focusNode: _cancel,
                          onPressed: () => Navigator.of(context).pop(),
                          minHeight: 40,
                          padding: const EdgeInsets.symmetric(horizontal: 16),
                          child: Center(
                            widthFactor: 1,
                            child: Text(Strings.cancel, style: s.nav),
                          ),
                        ),
                        Pressable(
                          onPressed: _busy ? null : _delete,
                          minHeight: 40,
                          padding: const EdgeInsets.symmetric(horizontal: 16),
                          color: c.error,
                          hoverColor: c.error,
                          child: Center(
                            widthFactor: 1,
                            child: Text(
                              Strings.confirmDelete,
                              style: s.nav.copyWith(color: c.background),
                            ),
                          ),
                        ),
                      ],
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
