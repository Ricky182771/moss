import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_markdown_plus/flutter_markdown_plus.dart';
import 'package:markdown/markdown.dart' as md;
import 'package:url_launcher/url_launcher.dart';

import '../strings.dart';
import '../theme/colors.dart';
import '../theme/motion.dart';
import '../theme/theme.dart';
import 'controls.dart';
import 'indicators.dart';

/// Marca, de uso privado de Unicode, donde va el cursor de la respuesta que
/// llega. Nunca se dibuja: la reemplaza [StreamCursor].
const _cursorMark = '\uE000';

/// Abre en el navegador solo enlaces `http` y `https`.
Future<void> openLink(String? href) async {
  final uri = Uri.tryParse(href ?? '');
  if (uri == null || !(uri.isScheme('http') || uri.isScheme('https'))) return;
  await launchUrl(uri);
}

/// Markdown de una respuesta o de su pensamiento. El texto se selecciona con
/// el `SelectionArea` de la lista de mensajes.
class MarkdownView extends StatelessWidget {
  final String text;

  /// Termina con el cursor verde.
  final bool streaming;

  /// Estilo del pensamiento (14, tenue) en vez del de la respuesta.
  final bool thought;

  const MarkdownView(
    this.text, {
    super.key,
    this.streaming = false,
    this.thought = false,
  });

  @override
  Widget build(BuildContext context) {
    final s = context.styles;
    final c = context.colors;
    final base = thought ? s.thought : s.answer;
    final bold = base.copyWith(fontWeight: FontWeight.w600);
    final heading = s.title.copyWith(color: base.color);
    return MarkdownBody(
      data: streaming ? '${text.trimRight()}$_cursorMark' : text,
      onTapLink: (_, href, _) => openLink(href),
      // Nunca cargar imágenes (red o disco) por lo que escriba el modelo o una
      // página: solo su texto alternativo, como la TUI.
      imageBuilder: (_, _, alt) => Text(alt ?? '', style: base),
      inlineSyntaxes: [_CursorSyntax()],
      builders: {'pre': _CodeBuilder(), 'cursor': _CursorBuilder()},
      styleSheet: MarkdownStyleSheet(
        p: base,
        a: base.copyWith(color: c.accent, decoration: TextDecoration.underline),
        strong: const TextStyle(fontWeight: FontWeight.w600),
        em: const TextStyle(fontStyle: FontStyle.italic),
        del: const TextStyle(decoration: TextDecoration.lineThrough),
        code: s.inlineCode.copyWith(backgroundColor: c.field),
        h1: heading,
        h2: heading,
        h3: bold,
        h4: bold,
        h5: bold,
        h6: bold,
        listBullet: base,
        tableHead: bold,
        tableBody: base,
        tableBorder: TableBorder.all(color: c.border),
        blockquote: base.copyWith(color: c.textMuted),
        blockquoteDecoration: BoxDecoration(
          border: Border(left: BorderSide(color: c.border, width: 3)),
        ),
        blockquotePadding: const EdgeInsets.only(left: 12),
        horizontalRuleDecoration: BoxDecoration(
          border: Border(top: BorderSide(color: c.border)),
        ),
        codeblockDecoration: const BoxDecoration(),
        blockSpacing: thought ? 8 : 14,
      ),
    );
  }
}

class _CursorSyntax extends md.InlineSyntax {
  _CursorSyntax() : super(_cursorMark);

  @override
  bool onMatch(md.InlineParser parser, Match match) {
    parser.addNode(md.Element.empty('cursor'));
    return true;
  }
}

/// `Text.rich` para que quede dentro del párrafo y no en una fila aparte.
class _CursorBuilder extends MarkdownElementBuilder {
  @override
  Widget visitElementAfterWithContext(
    BuildContext context,
    md.Element element,
    TextStyle? preferredStyle,
    TextStyle? parentStyle,
  ) => const Text.rich(
    WidgetSpan(alignment: PlaceholderAlignment.middle, child: StreamCursor()),
  );
}

class _CodeBuilder extends MarkdownElementBuilder {
  @override
  bool isBlockElement() => true;

  @override
  Widget visitElementAfterWithContext(
    BuildContext context,
    md.Element element,
    TextStyle? preferredStyle,
    TextStyle? parentStyle,
  ) {
    final code = element.children?.whereType<md.Element>().firstOrNull;
    final language = (code?.attributes['class'] ?? '').replaceFirst(
      'language-',
      '',
    );
    final raw = element.textContent;
    final text = raw.replaceAll(_cursorMark, '');
    return CodeBlock(
      language: language,
      code: text.endsWith('\n') ? text.substring(0, text.length - 1) : text,
      cursor: raw.contains(_cursorMark),
    );
  }
}

/// Bloque de código: el lenguaje y Copiar, que copia el texto tal como se ve.
class CodeBlock extends StatefulWidget {
  final String language;
  final String code;
  final bool cursor;

  const CodeBlock({
    super.key,
    required this.language,
    required this.code,
    this.cursor = false,
  });

  @override
  State<CodeBlock> createState() => _CodeBlockState();
}

class _CodeBlockState extends State<CodeBlock> {
  bool _copied = false;
  Timer? _timer;

  Future<void> _copy() async {
    await Clipboard.setData(ClipboardData(text: widget.code));
    _timer?.cancel();
    setState(() => _copied = true);
    _timer = Timer(Motion.copiedVisible, () {
      if (mounted) setState(() => _copied = false);
    });
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    final line = BorderSide(color: c.border);
    return Container(
      clipBehavior: Clip.antiAlias,
      decoration: BoxDecoration(
        color: c.field,
        borderRadius: BorderRadius.circular(12),
        border: Border.fromBorderSide(line),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Container(
            padding: const EdgeInsets.fromLTRB(14, 4, 6, 4),
            decoration: BoxDecoration(border: Border(bottom: line)),
            child: Row(
              children: [
                Expanded(child: Text(widget.language, style: s.codeLabel)),
                Pressable(
                  onPressed: _copy,
                  radius: 8,
                  minHeight: 36,
                  padding: const EdgeInsets.symmetric(horizontal: 10),
                  child: Center(
                    widthFactor: 1,
                    child: AnimatedSwitcher(
                      duration: Motion.of(context).copySwap,
                      child: _copied
                          ? Text(
                              Strings.copied,
                              key: const ValueKey(true),
                              style: s.smallButton,
                            )
                          : Row(
                              key: const ValueKey(false),
                              mainAxisSize: MainAxisSize.min,
                              spacing: 6,
                              children: [
                                Icon(
                                  Icons.content_copy_rounded,
                                  size: 14,
                                  color: c.textMuted,
                                ),
                                Text(Strings.copy, style: s.smallButton),
                              ],
                            ),
                    ),
                  ),
                ),
              ],
            ),
          ),
          SingleChildScrollView(
            scrollDirection: Axis.horizontal,
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
            child: Text.rich(
              TextSpan(
                text: widget.code,
                children: [
                  if (widget.cursor)
                    const WidgetSpan(
                      alignment: PlaceholderAlignment.middle,
                      child: StreamCursor(),
                    ),
                ],
              ),
              style: s.code,
            ),
          ),
        ],
      ),
    );
  }
}
