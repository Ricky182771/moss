import 'package:flutter/material.dart';

import '../strings.dart';
import '../theme/colors.dart';
import '../theme/theme.dart';
import '../widgets/logo.dart';

/// `moss-rpc` no está, no arrancó o es de otra versión.
class RpcErrorScreen extends StatelessWidget {
  final String title;
  final String? detail;

  /// La ruta donde se buscó `moss-rpc`.
  final String path;
  final List<String> stderr;

  const RpcErrorScreen({
    super.key,
    required this.title,
    required this.path,
    this.detail,
    this.stderr = const [],
  });

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final muted = s.answer.copyWith(color: c.textMuted);
    return Material(
      color: c.background,
      child: Center(
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(48),
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 640),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              spacing: 16,
              children: [
                const MossLogo(size: 64),
                Semantics(header: true, child: Text(title, style: s.title)),
                if (detail != null) SelectableText(detail!, style: muted),
                Text.rich(
                  TextSpan(
                    text: '${Strings.rpcSearchedAt}: ',
                    children: [TextSpan(text: path, style: s.code)],
                  ),
                  style: muted,
                ),
                if (stderr.isNotEmpty) ...[
                  Text(Strings.rpcStderr, style: s.thoughtSummary),
                  Container(
                    constraints: const BoxConstraints(maxHeight: 280),
                    decoration: BoxDecoration(
                      color: c.field,
                      borderRadius: BorderRadius.circular(12),
                      border: Border.all(color: c.border),
                    ),
                    child: SingleChildScrollView(
                      // Las últimas líneas abajo, a la vista.
                      reverse: true,
                      padding: const EdgeInsets.symmetric(
                        horizontal: 16,
                        vertical: 14,
                      ),
                      child: SelectableText(stderr.join('\n'), style: s.code),
                    ),
                  ),
                ],
              ],
            ),
          ),
        ),
      ),
    );
  }
}
