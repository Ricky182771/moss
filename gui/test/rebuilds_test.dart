import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/widgets/entry_view.dart';
import 'package:moss_gui/widgets/input_box.dart';
import 'package:moss_gui/widgets/markdown_view.dart';
import 'package:moss_gui/widgets/sidebar.dart';

import 'fake_server.dart';
import 'harness.dart';

/// Cuenta cuántas veces se reconstruye cada tipo de widget mientras corre
/// [body] (`debugOnRebuildDirtyWidget`, solo en debug).
Future<Map<Type, int>> countBuilds(Future<void> Function() body) async {
  final counts = <Type, int>{};
  debugOnRebuildDirtyWidget = (e, _) =>
      counts.update(e.widget.runtimeType, (n) => n + 1, ifAbsent: () => 1);
  try {
    await body();
  } finally {
    debugOnRebuildDirtyWidget = null;
  }
  return counts;
}

void main() {
  testWidgets('un trozo reconstruye solo la última entrada', (tester) async {
    final (_, server) = await startApp(
      tester,
      summaries: [summary('a', 'Otro')],
      conversation: snapshot(
        rev: 4,
        phase: 'streaming',
        entries: [
          entry('user', text: 'uno'),
          entry('assistant', text: 'Respuesta **uno**', reasoning: 'pensé'),
          entry('user', text: 'dos'),
          entry('assistant', text: 'Va', reasoning: 'pienso', inProgress: true),
        ],
      ),
    );
    final counts = await countBuilds(() async {
      server.notify('conversation.entry_delta', {
        'conversation': 'c1',
        'rev': 5,
        'index': 3,
        'text': ' llegando',
      });
      await tester.pump();
    });
    expect(counts[EntryView], 1);
    // El de la respuesta que llega; los dos Pensamientos están plegados y no
    // arman su Markdown.
    expect(counts[MarkdownView], 1);
    expect(counts[Sidebar], isNull);
    expect(counts[InputBox], isNull);
  });

  testWidgets('el Pensamiento plegado no arma su Markdown', (tester) async {
    await startApp(
      tester,
      conversation: snapshot(
        rev: 2,
        entries: [
          entry('user', text: 'hola'),
          entry(
            'assistant',
            text: 'Hola',
            reasoning: 'pienso **mucho**',
            reasoningWords: 2,
          ),
        ],
      ),
    );
    expect(find.byType(MarkdownView), findsOneWidget);
    await tester.tap(find.text('Pensamiento · 2 palabras'));
    await tester.pumpAndSettle();
    expect(find.byType(MarkdownView), findsNWidgets(2));
    await tester.tap(find.text('Pensamiento · 2 palabras'));
    await tester.pumpAndSettle();
    expect(find.byType(MarkdownView), findsOneWidget);
  });

  testWidgets('la fase cambia Enviar por Detener sin rearmar la caja', (
    tester,
  ) async {
    final (_, server) = await startApp(
      tester,
      conversation: snapshot(rev: 1, entries: [entry('user', text: 'hola')]),
    );
    final counts = await countBuilds(() async {
      server.notify('conversation.phase', {
        'conversation': 'c1',
        'rev': 2,
        'phase': 'thinking',
      });
      await tester.pump();
    });
    // El botón de Enviar pasa a Detener: la caja misma no se arma de nuevo.
    expect(counts[InputBox], isNull);
    expect(find.bySemanticsLabel('Detener'), findsOneWidget);
  });
}
