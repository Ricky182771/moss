import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/strings.dart';
import 'package:moss_gui/widgets/settings_form.dart';

import 'fake_server.dart';
import 'harness.dart';

const _key = 'tvly-secreta-0987654321';

Future<void> _openSearch(WidgetTester tester) async {
  await tester.sendKeyEvent(LogicalKeyboardKey.f2);
  await tester.pumpAndSettle();
  await tester.tap(find.text(Strings.webSearch).first);
  await tester.pumpAndSettle();
}

Finder get _keyField => find.descendant(
  of: find.byType(SecretField),
  matching: find.byType(EditableText),
);

void main() {
  testWidgets('el estado, la key enmascarada y guardar', (tester) async {
    var configured = false;
    final (app, server) = await startApp(
      tester,
      handlers: {
        'settings.search.get': (_) => {
          'configured': configured,
          'locked': false,
          'key_status': configured ? 'guardada: …4321' : 'sin configurar',
        },
        'settings.search.save': (p) {
          configured = true;
          return {'saved': true};
        },
      },
    );
    await _openSearch(tester);
    expect(find.text(Strings.searchNotConfigured), findsOneWidget);
    await tester.enterText(_keyField, _key);
    await tester.pump();
    expect(tester.widget<EditableText>(_keyField).obscureText, isTrue);

    await tester.tap(find.text(Strings.save));
    await tester.pumpAndSettle();
    expect(
      server.requests.lastWhere(
        (r) => r['method'] == 'settings.search.save',
      )['params'],
      {'api_key': _key},
    );
    // El servidor avisa y la sección se vuelve a pedir.
    server.notify('settings.changed', {'section': 'search'});
    await tester.pumpAndSettle();
    expect(find.text(Strings.searchConfigured), findsOneWidget);
    expect(find.text('guardada: …4321'), findsOneWidget);
    expect(find.textContaining(_key), findsNothing);
    for (final e in tester.widgetList<EditableText>(
      find.byType(EditableText),
    )) {
      expect(e.controller.text, isNot(contains(_key)));
    }
    expect(app.searchOn.value, isFalse); // Solo se habilita, no se enciende.
  });

  testWidgets('el error del servidor va junto al campo', (tester) async {
    await startApp(
      tester,
      handlers: {
        'settings.search.save': (_) => throw const FakeError(-32005, 'Error', {
          'message':
              'Eso parece el nombre de una variable o una key de '
              'búsqueda incompleta; pega la key completa.',
        }),
      },
    );
    await _openSearch(tester);
    await tester.enterText(_keyField, 'TAVILY');
    await tester.pump();
    await tester.tap(find.text(Strings.save));
    await tester.pumpAndSettle();
    expect(find.textContaining('pega la key completa'), findsOneWidget);
    expect(
      find.bySemanticsLabel(Strings.withUnsaved(Strings.webSearch)),
      findsOneWidget,
    );
  });

  testWidgets('con CHAT_SEARCH_API_KEY: el aviso y el campo deshabilitado', (
    tester,
  ) async {
    await startApp(
      tester,
      handlers: {
        'settings.search.get': (_) => {
          'configured': true,
          'locked': true,
          'key_status': '(definido por CHAT_SEARCH_API_KEY) …abcd',
        },
      },
    );
    await _openSearch(tester);
    expect(find.text(Strings.lockedBy('CHAT_SEARCH_API_KEY')), findsOneWidget);
    expect(
      find.text('(definido por CHAT_SEARCH_API_KEY) …abcd'),
      findsOneWidget,
    );
    expect(tester.widget<EditableText>(_keyField).readOnly, isTrue);
  });
}
