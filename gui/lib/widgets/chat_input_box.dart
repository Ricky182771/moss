import 'package:flutter/material.dart';

import '../strings.dart';
import '../theme/colors.dart';

/// Caja de entrada de texto según DISENO.md
class ChatInputBox extends StatefulWidget {
  final bool busy;
  final VoidCallback? onSend;
  final VoidCallback? onStop;
  final ValueChanged<String>? onChanged;

  const ChatInputBox({
    super.key,
    required this.busy,
    this.onSend,
    this.onStop,
    this.onChanged,
  });

  @override
  State<ChatInputBox> createState() => _ChatInputBoxState();
}

class _ChatInputBoxState extends State<ChatInputBox> {
  final TextEditingController _controller = TextEditingController();
  bool _isEmpty = true;

  @override
  void initState() {
    super.initState();
    _controller.addListener(_onTextChanged);
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  void _onTextChanged() {
    final isEmpty = _controller.text.trim().isEmpty;
    if (isEmpty != _isEmpty) {
      setState(() {
        _isEmpty = isEmpty;
      });
    }
    widget.onChanged?.call(_controller.text);
  }

  void _handleSend() {
    if (_isEmpty || widget.busy) return;
    widget.onSend?.call();
    _controller.clear();
  }

  @override
  Widget build(BuildContext context) {
    final colors = context.colors;

    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: colors.background,
        border: Border(top: BorderSide(color: colors.border)),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.end,
        children: [
          Expanded(
            child: Container(
              constraints: const BoxConstraints(minHeight: 44, maxHeight: 200),
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
              decoration: BoxDecoration(
                color: colors.field,
                borderRadius: BorderRadius.circular(8),
                border: Border.all(color: colors.border),
              ),
              child: TextField(
                controller: _controller,
                maxLines: null,
                textInputAction: TextInputAction.newline,
                style: Theme.of(context).textTheme.bodyMedium,
                decoration: InputDecoration(
                  hintText: Strings.placeholderMessage,
                  hintStyle: Theme.of(context).textTheme.bodyMedium
                      ?.copyWith(color: colors.textMuted),
                  border: InputBorder.none,
                  isDense: true,
                  contentPadding: EdgeInsets.zero,
                ),
                onSubmitted: (_) => _handleSend(),
              ),
            ),
          ),
          const SizedBox(width: 12),
          // Botón Enviar/Detener
          if (widget.busy)
            IconButton(
              icon: const Icon(Icons.stop),
              tooltip: Strings.buttonStop,
              onPressed: widget.onStop,
              style: IconButton.styleFrom(
                backgroundColor: colors.error,
                foregroundColor: colors.textOnAccent,
                minimumSize: const Size(44, 44),
              ),
            )
          else
            IconButton(
              icon: const Icon(Icons.send),
              tooltip: Strings.buttonSend,
              onPressed: _isEmpty ? null : _handleSend,
              style: IconButton.styleFrom(
                backgroundColor: _isEmpty
                    ? colors.fieldDisabled
                    : colors.accent,
                foregroundColor: colors.textOnAccent,
                minimumSize: const Size(44, 44),
                disabledBackgroundColor: colors.fieldDisabled,
              ),
            ),
        ],
      ),
    );
  }
}
