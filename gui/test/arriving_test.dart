import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/state/arriving.dart';
import 'package:moss_gui/widgets/arriving_text.dart';

import 'fake_server.dart';
import 'harness.dart';

Map<String, Object?> _streaming({String text = '', String reasoning = ''}) =>
    snapshot(
      rev: 2,
      phase: reasoning.isNotEmpty && text.isEmpty ? 'thinking' : 'streaming',
      entries: [
        entry('user', text: 'hola'),
        entry('assistant', text: text, reasoning: reasoning, inProgress: true),
      ],
    );

var _rev = 2;

Future<void> _delta(
  WidgetTester tester,
  FakeServer server,
  String text, {
  bool reasoning = false,
}) async {
  server.notify(
    reasoning ? 'conversation.reasoning_delta' : 'conversation.entry_delta',
    {'conversation': 'c1', 'rev': ++_rev, 'index': 1, 'text': text},
  );
  await tester.pump();
  await tester.pump();
}

int _groups(WidgetTester tester) =>
    ArrivingText.debugGroups(tester.element(find.byType(ArrivingText).first));

Finder _has(String s) => find.textContaining(s, findRichText: true);

void main() {
  setUp(() => _rev = 2);

  group('shownLength', () {
    test('hasta la última palabra completa', () {
      expect(shownLength('Hola mun'), 5);
      expect(shownLength('Hola mundo '), 11);
      expect(shownLength('Hola mundo  \n'), 13); // Espacios al final.
      expect(shownLength('Canción de cu'), 11);
      expect(shownLength('día'), 0);
      expect(shownLength(''), 0);
      expect(shownLength('uno\ndo'), 4);
    });

    test('emojis: no se parten', () {
      expect(shownLength('Hola 👋'), 5);
      expect(shownLength('Hola 👋 '), 'Hola 👋 '.length);
      expect(shownLength('🎵🎵 a'), '🎵🎵 '.length);
    });

    test('un trozo que corta una palabra a la mitad', () {
      const first = 'La respuesta lle';
      const both = '${first}ga completa ';
      expect(first.substring(0, shownLength(first)), 'La respuesta ');
      expect(both.substring(0, shownLength(both)), both);
    });

    test('bloques de código: por línea', () {
      const open = 'Mira:\n\n```dart\nint x = 1;\nint y';
      expect(
        open.substring(0, shownLength(open)),
        'Mira:\n\n```dart\nint x = 1;\n',
      );
      const fence = 'Mira:\n\n```pyt';
      expect(fence.substring(0, shownLength(fence)), 'Mira:\n\n');
      const closed = '```\nx\n```\nY ahora pal';
      expect(closed.substring(0, shownLength(closed)), '```\nx\n```\nY ahora ');
    });

    test('palabras', () {
      expect(countWords('  uno dos\ntres  '), 3);
      expect(countWords(''), 0);
    });
  });

  testWidgets('lo que llega aparece con su animación; la palabra a medias no', (
    tester,
  ) async {
    final (_, server) = await startApp(
      tester,
      conversation: _streaming(text: 'Hola '),
    );
    expect(_groups(tester), 0); // Lo que ya estaba no se anima.

    await _delta(tester, server, 'mundo bon');
    expect(_has('mundo'), findsOneWidget);
    expect(_has('bon'), findsNothing);
    expect(_groups(tester), 1);
    await tester.pump(const Duration(milliseconds: 150));
    await tester.pump();
    expect(_groups(tester), 0);

    await _delta(tester, server, 'ito ');
    expect(_has('bonito'), findsOneWidget);
    expect(_groups(tester), 1);
  });

  testWidgets('una ráfaga de más de 40 palabras aparece directo', (
    tester,
  ) async {
    final (_, server) = await startApp(
      tester,
      conversation: _streaming(text: 'Hola '),
    );
    await _delta(tester, server, '${List.filled(41, 'palabra').join(' ')} ');
    expect(_groups(tester), 0);
  });

  testWidgets('el Pensamiento mientras llega, igual', (tester) async {
    final (_, server) = await startApp(
      tester,
      conversation: _streaming(reasoning: 'Estoy '),
    );
    await _delta(tester, server, 'pensando mu', reasoning: true);
    expect(_has('pensando'), findsOneWidget);
    expect(_groups(tester), 1);
  });

  testWidgets('al reabrir o terminar: completo y sin animación', (
    tester,
  ) async {
    await startApp(
      tester,
      conversation: snapshot(
        rev: 2,
        entries: [
          entry('user', text: 'hola'),
          entry('assistant', text: 'Respuesta guardada sin espa'),
        ],
      ),
    );
    expect(find.byType(ArrivingText), findsNothing);
    expect(_has('Respuesta guardada sin espa'), findsOneWidget);
  });

  testWidgets('con disableAnimations: apagado y todo a la vista', (
    tester,
  ) async {
    final (_, server) = await startApp(
      tester,
      conversation: _streaming(text: 'Hola '),
      disableAnimations: true,
    );
    await _delta(tester, server, 'mundo bon');
    expect(find.byType(ArrivingText), findsNothing);
    expect(_has('mundo bon'), findsOneWidget);
  });

  testWidgets('tu mensaje aparece al enviarlo; los de antes, no', (
    tester,
  ) async {
    final (_, server) = await startApp(
      tester,
      conversation: snapshot(rev: 0, entries: [entry('user', text: 'antes')]),
    );
    double opacity(String text) => tester
        .widget<Opacity>(
          find.ancestor(of: find.text(text), matching: find.byType(Opacity)),
        )
        .opacity;
    expect(
      find.ancestor(of: find.text('antes'), matching: find.byType(Opacity)),
      findsNothing,
    );
    server.notify('conversation.entry_added', {
      'conversation': 'c1',
      'rev': 1,
      'index': 1,
      'entry': entry('user', text: 'nuevo'),
    });
    await tester.pump();
    await tester.pump();
    expect(opacity('nuevo'), lessThan(1));
    await tester.pump(const Duration(milliseconds: 200));
    expect(opacity('nuevo'), 1);
  });
}
