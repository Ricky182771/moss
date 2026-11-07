import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/state/app_state.dart';
import 'package:moss_gui/strings.dart';
import 'package:moss_gui/widgets/markdown_view.dart';

import 'fake_server.dart';
import 'harness.dart';

void main() {
  testWidgets('Reintentar arranca otro moss-rpc y reabre la conversación', (
    tester,
  ) async {
    final chat = snapshot(
      id: 'abc',
      title: 'Selection sort',
      entries: [
        entry('user', text: 'hola'),
        entry('assistant', text: 'Respuesta guardada'),
      ],
    );
    final second = FakeServer(
      handlers: {
        'conversation.open': (_) => chat,
        'conversations.list': (_) => {'items': <Object>[]},
      },
    );
    final (_, first) = await startApp(
      tester,
      conversation: chat,
      reconnect: () async => second.client(),
    );
    first.crash();
    await tester.pump();
    expect(find.text(Strings.rpcDisconnected), findsOneWidget);

    await tester.tap(find.text(Strings.retry));
    await tester.pump();
    await tester.pump();
    final open = second.requests.firstWhere(
      (r) => r['method'] == 'conversation.open',
    );
    expect(open['params'], {'id': 'abc'});
    expect(second.methods.first, 'initialize');
    expect(find.text(Strings.rpcDisconnected), findsNothing);
    expect(find.text('Respuesta guardada'), findsOneWidget);
  });

  test(
    'hueco de rev: conversation.get y luego lo que llegó mientras',
    () async {
      var gets = 0;
      final server = FakeServer(
        handlers: {
          'conversations.list': (_) => {'items': <Object>[]},
          'conversation.get': (_) => ++gets == 1
              ? snapshot(
                  rev: 2,
                  entries: [
                    entry('user', text: 'hola'),
                    entry('assistant', text: 'Hola', inProgress: true),
                  ],
                )
              : snapshot(
                  rev: 5,
                  entries: [
                    entry('user', text: 'hola'),
                    entry('assistant', text: 'Hola mundo', inProgress: true),
                  ],
                ),
        },
      );
      final client = server.client();
      await client.initialize();
      final app = AppState(client);
      await app.load('c1');

      // Falta el rev 3: hay que volver a pedir la conversación.
      server.notify('conversation.entry_delta', {
        'conversation': 'c1',
        'rev': 4,
        'index': 1,
        'text': ' mundo',
      });
      // Llega mientras se espera conversation.get: se aplica después.
      server.notify('conversation.entry_delta', {
        'conversation': 'c1',
        'rev': 6,
        'index': 1,
        'text': ' y más',
      });
      await pumpEventQueue();

      expect(gets, 2);
      expect(app.conversation!.rev, 6);
      expect(app.conversation!.entries.last.text, 'Hola mundo y más');
    },
  );

  testWidgets('restore_text vuelve a la caja si está vacía', (tester) async {
    final (_, server) = await startApp(tester);
    server.notify('chat.finished', {
      'conversation': 'c1',
      'rev': 1,
      'outcome': 'error',
      'restore_text': 'mi pregunta',
    });
    await tester.pump();
    expect(_input(tester).text, 'mi pregunta');

    // Si ya se escribió otra cosa, no se pisa.
    await tester.enterText(find.byType(TextField), 'otra cosa');
    server.notify('chat.finished', {
      'conversation': 'c1',
      'rev': 2,
      'outcome': 'cancelled',
      'restore_text': 'mi pregunta',
    });
    await tester.pump();
    expect(_input(tester).text, 'otra cosa');
  });

  test('openLink abre solo http y https', () async {
    final opened = <Uri>[];
    Future<bool> launch(Uri u) async {
      opened.add(u);
      return true;
    }

    for (final href in [
      'https://en.cppreference.com/w/cpp',
      'http://localhost:8765/',
      'file:///etc/passwd',
      'javascript:alert(1)',
      'mailto:a@b.c',
      'no es una url',
      null,
    ]) {
      await openLink(href, launch: launch);
    }
    expect(opened.map((u) => '$u'), [
      'https://en.cppreference.com/w/cpp',
      'http://localhost:8765/',
    ]);
  });
}

TextEditingController _input(WidgetTester tester) =>
    tester.widget<TextField>(find.byType(TextField)).controller!;
