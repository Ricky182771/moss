import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/models/settings.dart';
import 'package:moss_gui/state/slash_menu.dart';
import 'package:moss_gui/strings.dart';

import 'fake_server.dart';
import 'harness.dart';

Map<String, Object?> _cmd(
  String name, {
  String arguments = '',
  bool needsArgument = false,
  String note = '',
}) => {
  'name': name,
  'arguments': arguments,
  'description': 'Descripción de $name',
  'needs_argument': needsArgument,
  'available': note.isEmpty,
  'note': note,
};

final _commands = {
  'commands': [
    _cmd('/buscar', arguments: '<consulta>', needsArgument: true),
    _cmd('/copiar', arguments: '[n]', note: 'no hay bloques de código'),
    _cmd('/exportar'),
    _cmd('/model', arguments: '[proveedor] [modelo]'),
  ],
};

Future<FakeServer> _start(WidgetTester tester) async {
  final (_, server) = await startApp(
    tester,
    handlers: {
      'commands.list': (_) => _commands,
      'commands.run': (_) => {'kind': 'export', 'clear': true},
    },
  );
  return server;
}

Future<void> _type(WidgetTester tester, String text) async {
  await tester.enterText(find.byType(TextField).first, text);
  for (var i = 0; i < 3; i++) {
    await tester.pump();
  }
  await tester.pump(const Duration(milliseconds: 200));
}

void main() {
  group('reglas del menú', () {
    test('se ve mientras se escribe el nombre', () {
      expect(typingCommand('/'), isTrue);
      expect(typingCommand('/bus'), isTrue);
      expect(typingCommand('/buscar x'), isFalse);
      expect(typingCommand(' /buscar'), isFalse);
      expect(typingCommand('/a\nb'), isFalse);
      expect(typingCommand('hola'), isFalse);
    });

    test('filtro por prefijo sin mayúsculas y lo que completa', () {
      final all = [
        for (final c in _commands['commands']!)
          CommandInfo.fromJson(c as Map<String, dynamic>),
      ];
      expect(filterCommands(all, '/').length, 4);
      expect(filterCommands(all, '/B').map((c) => c.name), ['/buscar']);
      expect(filterCommands(all, '/e').map((c) => c.name), ['/exportar']);
      expect(filterCommands(all, '/zz'), isEmpty);
      expect(completeCommand(all[0]), '/buscar ');
      expect(completeCommand(all[2]), '/exportar');
    });
  });

  testWidgets('se pide una vez, filtra y atenúa los no disponibles', (
    tester,
  ) async {
    final server = await _start(tester);
    await _type(tester, '/');
    expect(find.text('/buscar <consulta>'), findsOneWidget);
    expect(find.text('/exportar'), findsOneWidget);
    expect(find.text('no hay bloques de código'), findsOneWidget);
    expect(
      tester.getSemantics(find.text('/copiar [n]')),
      isSemantics(isButton: true, hasEnabledState: true, isEnabled: false),
    );
    await _type(tester, '/e');
    await _type(tester, '/ex');
    expect(find.text('/buscar <consulta>'), findsNothing);
    expect(find.text('/exportar'), findsOneWidget);
    expect(server.methods.where((m) => m == 'commands.list'), hasLength(1));

    await _type(tester, '/zz');
    expect(find.text(Strings.noCommands), findsOneWidget);
  });

  testWidgets('↓ salta el no disponible; Tab completa sin enviar', (
    tester,
  ) async {
    final server = await _start(tester);
    await _type(tester, '/');
    // /buscar → (salta /copiar) → /exportar → /model
    await tester.sendKeyEvent(LogicalKeyboardKey.arrowDown);
    await tester.sendKeyEvent(LogicalKeyboardKey.arrowDown);
    await tester.sendKeyEvent(LogicalKeyboardKey.tab);
    await tester.pumpAndSettle();
    final box = tester.widget<TextField>(find.byType(TextField).first);
    expect(box.controller!.text, '/model ');
    expect(find.text('/exportar'), findsNothing); // Se cerró.
    expect(server.methods, isNot(contains('commands.run')));
  });

  testWidgets('Enter completa /buscar y manda /exportar', (tester) async {
    final server = await _start(tester);
    await _type(tester, '/bu');
    await tester.sendKeyEvent(LogicalKeyboardKey.enter);
    await tester.pumpAndSettle();
    final box = tester.widget<TextField>(find.byType(TextField).first);
    expect(box.controller!.text, '/buscar ');
    expect(server.methods, isNot(contains('commands.run')));

    await _type(tester, '/exp');
    await tester.sendKeyEvent(LogicalKeyboardKey.enter);
    await tester.pumpAndSettle();
    final run = server.requests.lastWhere((r) => r['method'] == 'commands.run');
    expect(run['params'], {'conversation': 'c1', 'text': '/exportar'});
  });

  testWidgets('Esc cierra el menú sin borrar el texto', (tester) async {
    await _start(tester);
    await _type(tester, '/ex');
    expect(find.text('/exportar'), findsOneWidget);
    await tester.sendKeyEvent(LogicalKeyboardKey.escape);
    await tester.pumpAndSettle();
    expect(find.text('/exportar'), findsNothing);
    final box = tester.widget<TextField>(find.byType(TextField).first);
    expect(box.controller!.text, '/ex');
  });
}
