import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/state/app_state.dart';
import 'package:moss_gui/strings.dart';

import 'fake_server.dart';
import 'harness.dart';

final _summaries = [
  summary('a', 'Abierto'),
  summary('b', 'Otro'),
  summary('c', '', group: 'Sin fecha', readable: false, error: 'Dañado'),
];

Finder _dots(String title) => find.bySemanticsLabel(Strings.moreOptions(title));

Future<void> _rightClick(WidgetTester tester, String title) async {
  await tester.tap(find.text(title).first, buttons: kSecondaryButton);
  await tester.pumpAndSettle();
}

Map<String, dynamic> _last(FakeServer server, String method) =>
    server.requests.lastWhere((r) => r['method'] == method)['params']
        as Map<String, dynamic>;

Future<(AppState, FakeServer)> _start(
  WidgetTester tester, {
  Map<String, Object? Function(Map<String, dynamic>)> handlers = const {},
}) => startApp(
  tester,
  conversation: snapshot(id: 'a', title: 'Abierto'),
  summaries: _summaries,
  handlers: {
    'conversation.rename': (p) => {'title': p['title']},
    'conversation.export': (_) => {
      'path': '/tmp/x.md',
      'message': 'Conversación exportada a ~/x.md',
    },
    'conversation.delete': (_) => {'replaced_by': null},
    ...handlers,
  },
);

void main() {
  testWidgets('⋯ siempre en el abierto; en los demás con hover o foco', (
    tester,
  ) async {
    await _start(tester);
    expect(_dots('Abierto'), findsOneWidget);
    expect(_dots('Otro'), findsNothing);
    expect(_dots('c'), findsNothing);

    final mouse = await tester.createGesture(kind: PointerDeviceKind.mouse);
    await mouse.addPointer(location: tester.getCenter(find.text('Otro')));
    await tester.pump();
    expect(_dots('Otro'), findsOneWidget);
    await mouse.moveTo(Offset.zero);
    await tester.pump();
    expect(_dots('Otro'), findsNothing);

    Focus.of(tester.element(find.text('Otro'))).requestFocus();
    await tester.pump();
    expect(_dots('Otro'), findsOneWidget);
  });

  testWidgets('clic derecho y Shift+F10 abren el menú; no en ilegibles', (
    tester,
  ) async {
    await _start(tester);
    await _rightClick(tester, 'Otro');
    expect(find.text(Strings.rename), findsOneWidget);
    expect(find.text(Strings.delete), findsOneWidget);
    // Esc cierra y el foco vuelve al ⋯.
    await tester.sendKeyEvent(LogicalKeyboardKey.escape);
    await tester.pumpAndSettle();
    expect(find.text(Strings.rename), findsNothing);

    Focus.of(tester.element(find.text('Otro'))).requestFocus();
    await tester.pump();
    await tester.sendKeyDownEvent(LogicalKeyboardKey.shiftLeft);
    await tester.sendKeyEvent(LogicalKeyboardKey.f10);
    await tester.sendKeyUpEvent(LogicalKeyboardKey.shiftLeft);
    await tester.pumpAndSettle();
    expect(find.text(Strings.rename), findsOneWidget);
    await tester.sendKeyEvent(LogicalKeyboardKey.escape);
    await tester.pumpAndSettle();

    // La tecla de menú.
    Focus.of(tester.element(find.text('Otro'))).requestFocus();
    await tester.pump();
    await tester.sendKeyEvent(LogicalKeyboardKey.contextMenu);
    await tester.pumpAndSettle();
    expect(find.text(Strings.rename), findsOneWidget);
    await tester.sendKeyEvent(LogicalKeyboardKey.escape);
    await tester.pumpAndSettle();

    await _rightClick(tester, 'c');
    expect(find.text(Strings.rename), findsNothing);
  });

  testWidgets('Renombrar: Enter guarda, Esc cancela, el error debajo', (
    tester,
  ) async {
    var fail = true;
    final (_, server) = await _start(
      tester,
      handlers: {
        'conversation.rename': (p) => fail
            ? throw const FakeError(-32005, 'ValidationFailed', {
                'message': 'Escribe un título.',
              })
            : {'title': p['title']},
      },
    );
    // Esc cancela sin pedir nada.
    await _rightClick(tester, 'Otro');
    await tester.tap(find.text(Strings.rename));
    await tester.pumpAndSettle();
    final field = find.widgetWithText(TextField, 'Otro');
    expect(field, findsOneWidget);
    final editable = tester.widget<TextField>(field).controller!;
    expect(
      editable.selection,
      const TextSelection(baseOffset: 0, extentOffset: 4),
    );
    await tester.sendKeyEvent(LogicalKeyboardKey.escape);
    await tester.pumpAndSettle();
    expect(find.widgetWithText(TextField, 'Otro'), findsNothing);
    expect(server.methods, isNot(contains('conversation.rename')));
    // El foco vuelve a la fila: su ⋯ se ve.
    expect(_dots('Otro'), findsOneWidget);

    // El error del servidor, debajo del campo.
    await _rightClick(tester, 'Otro');
    await tester.tap(find.text(Strings.rename));
    await tester.pumpAndSettle();
    await tester.enterText(find.widgetWithText(TextField, 'Otro'), ' ');
    await tester.sendKeyEvent(LogicalKeyboardKey.enter);
    await tester.pumpAndSettle();
    expect(find.text('Escribe un título.'), findsOneWidget);

    // Enter guarda.
    fail = false;
    await tester.enterText(find.byType(TextField).first, 'Nuevo título');
    await tester.sendKeyEvent(LogicalKeyboardKey.enter);
    await tester.pumpAndSettle();
    expect(_last(server, 'conversation.rename'), {
      'id': 'b',
      'title': 'Nuevo título',
    });
    expect(find.text('Escribe un título.'), findsNothing);
  });

  testWidgets('Exportar: con id y aviso corto', (tester) async {
    final (_, server) = await _start(tester);
    await _rightClick(tester, 'Otro');
    // El menú va antes que el encabezado en el árbol (OverlayPortal).
    await tester.tap(find.text(Strings.export).first);
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 200));
    expect(_last(server, 'conversation.export'), {'id': 'b'});
    expect(find.text('Conversación exportada a ~/x.md'), findsOneWidget);
  });

  testWidgets('Borrar: confirmación, Busy en la ventana y replaced_by', (
    tester,
  ) async {
    var busy = true;
    final (app, server) = await _start(
      tester,
      handlers: {
        'conversation.delete': (_) => busy
            ? throw const FakeError(-32002, 'Busy', {
                'message': 'Espera la respuesta o cancélala con Esc.',
              })
            : {'replaced_by': snapshot(handle: 'c9')},
      },
    );
    await tester.tap(_dots('Abierto'));
    await tester.pumpAndSettle();
    await tester.tap(find.text(Strings.delete));
    await tester.pumpAndSettle();
    expect(find.text(Strings.deleteQuestion('Abierto')), findsOneWidget);
    expect(find.text(Strings.cannotUndo), findsOneWidget);

    await tester.tap(find.text(Strings.confirmDelete));
    await tester.pumpAndSettle();
    expect(
      find.text('Espera la respuesta o cancélala con Esc.'),
      findsOneWidget,
    );
    expect(find.text(Strings.deleteQuestion('Abierto')), findsOneWidget);

    busy = false;
    await tester.tap(find.text(Strings.confirmDelete));
    await tester.pumpAndSettle();
    expect(_last(server, 'conversation.delete'), {'id': 'a'});
    expect(find.text(Strings.deleteQuestion('Abierto')), findsNothing);
    expect(app.conversation!.conversation, 'c9');
  });

  testWidgets('Cancelar no borra', (tester) async {
    final (_, server) = await _start(tester);
    await _rightClick(tester, 'Otro');
    await tester.tap(find.text(Strings.delete));
    await tester.pumpAndSettle();
    await tester.tap(find.text(Strings.cancel));
    await tester.pumpAndSettle();
    expect(find.text(Strings.deleteQuestion('Otro')), findsNothing);
    expect(server.methods, isNot(contains('conversation.delete')));
  });
}
