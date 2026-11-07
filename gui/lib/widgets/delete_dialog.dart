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
}) => confirm(
  context,
  question: Strings.deleteQuestion(title),
  detail: Strings.cannotUndo,
  confirmLabel: Strings.confirmDelete,
  action: delete,
);

/// Una pregunta s/n (las del servidor con `-32008`, "¿Reiniciar ahora?"):
/// true con Sí, false con No y null con Esc o el clic afuera, que cancelan
/// (como `yes_no_answer` en la TUI). El foco empieza en No, la opción segura.
Future<bool?> askYesNo(
  BuildContext context, {
  required String question,
  String? detail,
}) async {
  final motion = Motion.of(context);
  return showGeneralDialog<bool>(
    context: context,
    barrierDismissible: true,
    barrierLabel: Strings.cancel,
    barrierColor: context.colors.scrim,
    transitionDuration: motion.menuOpen,
    transitionBuilder: _transition,
    pageBuilder: (context, _, _) => _ConfirmDialog(
      // Las del servidor son las de la TUI: aquí responden los botones.
      question: question.endsWith(' (s/n)')
          ? question.substring(0, question.length - ' (s/n)'.length)
          : question,
      detail: detail,
      cancelLabel: Strings.no,
      confirmLabel: Strings.yes,
      cancelResult: false,
      action: () async => null,
    ),
  );
}

Widget _transition(
  BuildContext context,
  Animation<double> anim,
  Animation<double> _,
  Widget child,
) {
  final t = CurvedAnimation(parent: anim, curve: Motion.enter);
  return FadeTransition(
    opacity: t,
    child: ScaleTransition(
      scale: Tween(begin: 0.98, end: 1.0).animate(t),
      child: child,
    ),
  );
}

/// Pregunta con [cancelLabel] y [confirmLabel] (en el color de error). El
/// foco empieza en cancelar. [action] devuelve el error del servidor, que se
/// queda en la ventana, o null para cerrarla. Devuelve si se confirmó.
Future<bool> confirm(
  BuildContext context, {
  required String question,
  String? detail,
  String cancelLabel = Strings.cancel,
  required String confirmLabel,
  required Future<String?> Function() action,
}) async {
  final motion = Motion.of(context);
  final done = await showGeneralDialog<bool>(
    context: context,
    barrierDismissible: true,
    barrierLabel: Strings.cancel,
    barrierColor: context.colors.scrim,
    transitionDuration: motion.menuOpen,
    transitionBuilder: _transition,
    pageBuilder: (context, _, _) => _ConfirmDialog(
      question: question,
      detail: detail,
      cancelLabel: cancelLabel,
      confirmLabel: confirmLabel,
      action: action,
    ),
  );
  return done ?? false;
}

class _ConfirmDialog extends StatefulWidget {
  final String question;
  final String? detail;
  final String cancelLabel;
  final String confirmLabel;
  final Future<String?> Function() action;

  /// Lo que devuelve el botón de cancelar (null: igual que Esc).
  final bool? cancelResult;
  const _ConfirmDialog({
    required this.question,
    required this.detail,
    required this.cancelLabel,
    required this.confirmLabel,
    required this.action,
    this.cancelResult,
  });

  @override
  State<_ConfirmDialog> createState() => _ConfirmDialogState();
}

class _ConfirmDialogState extends State<_ConfirmDialog> {
  String? _error;
  bool _busy = false;

  /// El foco empieza en cancelar: Enter no borra sin querer.
  final _cancel = FocusNode();

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

  Future<void> _confirm() async {
    setState(() {
      _busy = true;
      _error = null;
    });
    final error = await widget.action();
    if (!mounted) return;
    if (error == null) {
      Navigator.of(context).pop(true);
    } else {
      setState(() {
        _busy = false;
        _error = error;
      });
      _cancel.requestFocus(); // Borrar estuvo deshabilitado: perdió el foco.
    }
  }

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final question = widget.question;
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
                    if (widget.detail case final detail?)
                      Text(detail, style: s.row.copyWith(color: c.textMuted)),
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
                    // Con el texto grande, los botones bajan a otra fila.
                    Wrap(
                      alignment: WrapAlignment.end,
                      spacing: 8,
                      runSpacing: 8,
                      children: [
                        Pressable(
                          focusNode: _cancel,
                          onPressed: () =>
                              Navigator.of(context).pop(widget.cancelResult),
                          minHeight: 40,
                          padding: const EdgeInsets.symmetric(horizontal: 16),
                          child: Center(
                            widthFactor: 1,
                            child: Text(widget.cancelLabel, style: s.nav),
                          ),
                        ),
                        Pressable(
                          onPressed: _busy ? null : _confirm,
                          minHeight: 40,
                          padding: const EdgeInsets.symmetric(horizontal: 16),
                          color: c.error,
                          hoverColor: c.error,
                          child: Center(
                            widthFactor: 1,
                            child: Text(
                              widget.confirmLabel,
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
