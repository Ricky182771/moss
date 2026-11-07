import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/state/title_search.dart';
import 'package:moss_gui/strings.dart';
import 'package:moss_gui/widgets/controls.dart';
import 'package:moss_gui/widgets/search_dialog.dart';

import 'fake_server.dart';
import 'harness.dart';

final _summaries = [
  summary('a', 'Canción de cuna', group: 'Hoy', usedSearch: true),
  summary('b', 'Selection sort en C++', group: 'Hoy'),
  summary('c', 'Otra canción', group: 'Ayer'),
  summary('d', '', group: 'Sin fecha', readable: false, error: 'Dañado'),
];

Future<void> _open(WidgetTester tester) async {
  await tester.sendKeyDownEvent(LogicalKeyboardKey.controlLeft);
  await tester.sendKeyEvent(LogicalKeyboardKey.keyK);
  await tester.sendKeyUpEvent(LogicalKeyboardKey.controlLeft);
  await tester.pumpAndSettle();
}

Finder _inDialog(Finder f) =>
    find.descendant(of: find.byType(SearchDialog), matching: f);

Finder _query() => find.descendant(
  of: find.byType(SearchDialog),
  matching: find.byType(TextField),
);

void main() {
  group('findInTitle', () {
    test('sin mayúsculas ni acentos', () {
      expect(findInTitle('Canción de cuna', 'cancion'), (0, 7));
      expect(findInTitle('Canción de cuna', 'CANCIÓN'), (0, 7));
      expect(findInTitle('Otra canción', 'cion'), (8, 12));
      expect(findInTitle('Pingüino', 'guino'), (3, 8));
      expect(findInTitle('Año nuevo', 'ano'), (0, 3));
      expect(findInTitle('İstanbul', 'istanbul'), (0, 8));
    });

    test('acento escrito aparte (marca combinante)', () {
      const title = 'Canción'; // "Canción" con U+0301
      expect(findInTitle(title, 'canción'), (0, 8));
      expect(findInTitle(title, 'cancio'), (0, 7)); // La marca va con la o.
    });

    test('emojis, espacios y sin coincidencia', () {
      expect(findInTitle('🎵 Canción', 'cancion'), (3, 10));
      expect(findInTitle('Canción', '  cancion  '), (0, 7));
      expect(findInTitle('Canción', 'cuna'), isNull);
      expect(findInTitle('Canción', ''), (0, 0));
    });
  });

  testWidgets('Ctrl+K: por título, resaltado, globo y sin ilegibles', (
    tester,
  ) async {
    await startApp(tester, summaries: _summaries);
    await _open(tester);
    expect(find.byType(SearchDialog), findsOneWidget);
    // Sin escribir: todos los legibles, por grupo.
    expect(_inDialog(find.text('HOY')), findsOneWidget);
    expect(_inDialog(find.text('AYER')), findsOneWidget);
    // El ilegible ("d", sin título) no sale: solo tres filas.
    expect(_inDialog(find.byType(Pressable)), findsNWidgets(3));
    expect(
      find.descendant(
        of: find.byType(SearchDialog),
        matching: find.text('d', findRichText: true),
      ),
      findsNothing,
    );

    await tester.enterText(_query(), 'cancion');
    await tester.pump();
    final hit = _inDialog(find.text('Canción de cuna', findRichText: true));
    expect(hit, findsOneWidget);
    expect(
      _inDialog(find.text('Otra canción', findRichText: true)),
      findsOneWidget,
    );
    expect(
      _inDialog(find.text('Selection sort en C++', findRichText: true)),
      findsNothing,
    );
    // Lo que coincide, en negrita.
    final bold = <String>[];
    tester.widget<RichText>(hit).text.visitChildren((s) {
      if (s is TextSpan && s.style?.fontWeight == FontWeight.w600) {
        bold.add(s.text!);
      }
      return true;
    });
    expect(bold, ['Canción']);
    expect(
      find.descendant(
        of: find.byType(SearchDialog),
        matching: find.byIcon(Icons.language_rounded),
      ),
      findsOneWidget,
    );
  });

  testWidgets('↓ y Enter abren; el foco vuelve a la caja', (tester) async {
    final (_, server) = await startApp(
      tester,
      summaries: _summaries,
      handlers: {
        'conversation.open': (p) =>
            snapshot(handle: 'c2', id: p['id'] as String, title: 'x'),
      },
    );
    expect(FocusManager.instance.primaryFocus?.debugLabel, 'Mensaje');
    await _open(tester);
    // "Selection" también tiene "cion": aquí, solo las dos canciones.
    await tester.enterText(_query(), 'cancion');
    await tester.sendKeyEvent(LogicalKeyboardKey.arrowDown);
    await tester.sendKeyEvent(LogicalKeyboardKey.enter);
    await tester.pumpAndSettle();
    expect(find.byType(SearchDialog), findsNothing);
    final open = server.requests.lastWhere(
      (r) => r['method'] == 'conversation.open',
    );
    expect(open['params'], {'id': 'c'});
    expect(FocusManager.instance.primaryFocus?.debugLabel, 'Mensaje');
  });

  testWidgets('Esc cierra y el foco vuelve a donde estaba', (tester) async {
    await startApp(tester, summaries: _summaries);
    await _open(tester);
    await tester.sendKeyEvent(LogicalKeyboardKey.escape);
    await tester.pumpAndSettle();
    expect(find.byType(SearchDialog), findsNothing);
    expect(FocusManager.instance.primaryFocus?.debugLabel, 'Mensaje');
  });

  testWidgets('sin resultados: el texto tenue', (tester) async {
    await startApp(tester, summaries: _summaries);
    await tester.tap(find.text(Strings.searchChats));
    await tester.pumpAndSettle();
    await tester.enterText(_query(), 'zzz');
    await tester.pump();
    expect(find.text(Strings.noChatsFound), findsOneWidget);
  });
}
