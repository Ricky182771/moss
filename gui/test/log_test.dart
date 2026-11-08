import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/strings.dart';
import 'package:moss_gui/theme/colors.dart';

import 'fake_server.dart';
import 'harness.dart';

Future<FakeServer> _openLog(
  WidgetTester tester,
  Object? Function(Map<String, dynamic>) log, {
  bool settle = true,
  DateTime? now,
}) async {
  final (_, server) = await startApp(
    tester,
    addons: ['telegram'],
    now: now,
    handlers: {...telegramHandlers(), 'service.log': log},
  );
  await tester.sendKeyEvent(LogicalKeyboardKey.f2);
  await tester.pumpAndSettle();
  await tester.tap(find.text(Strings.telegramMenu));
  await tester.pumpAndSettle();
  await tester.tap(find.text(Strings.viewLog));
  if (settle) await tester.pumpAndSettle();
  return server;
}

MossColors _colors(WidgetTester tester) =>
    Theme.of(tester.element(find.text(Strings.logTitle)))
        .extension<MossColors>()!;

void main() {
  testWidgets('abre abajo, con separadores y los errores marcados', (
    tester,
  ) async {
    final server = await _openLog(tester, (_) => logResult(errors: [58]));
    expect(server.lastParams('service.log'), {'addon': 'telegram'});
    expect(find.text(Strings.logTitle), findsOneWidget);
    expect(find.text(Strings.logSubtitle), findsOneWidget);
    // Abajo: la más reciente se ve; la más vieja no.
    expect(find.text('mensaje de hoy 29').hitTestable(), findsOneWidget);
    expect(find.text('mensaje de ayer 0').hitTestable(), findsNothing);
    // La hora y el mensaje, sin la fecha ni el equipo.
    expect(find.text('09:29:00'), findsOneWidget);
    expect(find.textContaining('moss-prueba'), findsNothing);
    expect(find.text(Strings.yesterday.toUpperCase()), findsOneWidget);
    expect(find.text(Strings.today.toUpperCase()), findsOneWidget);

    // El error: texto en el color de error y la barra a la izquierda.
    final c = _colors(tester);
    final error = tester.widget<Text>(find.text('mensaje de hoy 28'));
    expect(error.style?.color, c.error);
    final normal = tester.widget<Text>(find.text('mensaje de hoy 27'));
    expect(normal.style?.color, isNot(c.error));
    final bar = tester
        .widgetList<Container>(
          find.ancestor(
            of: find.text('mensaje de hoy 28'),
            matching: find.byType(Container),
          ),
        )
        .first;
    expect(
      ((bar.decoration as BoxDecoration).border as Border).left.color,
      c.error,
    );
    expect(find.text(Strings.newestBelow), findsOneWidget);
    expect(find.text(Strings.linesTruncated), findsNothing);
  });

  testWidgets('Hoy y Ayer con el reloj de la app, también a medianoche', (
    tester,
  ) async {
    await _openLog(
      tester,
      (_) => logResult(
        lines: [
          '2026-11-07T23:59:59-0600 pc moss-telegram[7]: antes',
          '2026-11-08T00:00:01-0600 pc moss-telegram[7]: después',
        ],
      ),
      now: DateTime(2026, 11, 8, 0, 0, 5),
    );
    expect(find.text(Strings.yesterday.toUpperCase()), findsOneWidget);
    expect(find.text(Strings.today.toUpperCase()), findsOneWidget);
  });

  testWidgets('una línea sin formato sale completa', (tester) async {
    await _openLog(
      tester,
      (_) => logResult(
        lines: ['-- Boot 3f2a --', journalLine(0, '09:00:00', 'a')],
      ),
    );
    expect(find.text('-- Boot 3f2a --'), findsOneWidget);
  });

  testWidgets('truncated: el aviso al pie', (tester) async {
    await _openLog(tester, (_) => logResult(truncated: true));
    expect(find.text(Strings.linesTruncated), findsOneWidget);
  });

  testWidgets('sin líneas: el notice del servidor, sin Copiar', (tester) async {
    const notice = 'El bot todavía no ha escrito nada en el registro.';
    await _openLog(tester, (_) => logResult(lines: [], notice: notice));
    expect(find.text(notice), findsOneWidget);
    final copy = tester.widget<Semantics>(
      find.ancestor(
        of: find.text(Strings.copy),
        matching: find.byWidgetPredicate(
          (w) => w is Semantics && w.properties.label == Strings.copy,
        ),
      ),
    );
    expect(copy.properties.enabled, isFalse);
  });

  testWidgets(r'cargando: el aviso tras 300 ms y $/cancel al cerrar', (
    tester,
  ) async {
    final server = await _openLog(tester, (_) => noReply, settle: false);
    await tester.pump(const Duration(milliseconds: 100));
    expect(find.text(Strings.readingLog), findsNothing);
    await tester.pump(const Duration(milliseconds: 300));
    expect(find.text(Strings.readingLog), findsOneWidget);
    final id = server.lastId('service.log');
    await tester.sendKeyEvent(LogicalKeyboardKey.escape);
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 500));
    expect(server.lastParams(r'$/cancel'), {'id': id});
    expect(find.text(Strings.logTitle), findsNothing);
  });

  testWidgets('un error, en la ventana con su texto', (tester) async {
    await _openLog(
      tester,
      (_) => throw const FakeError(-32006, 'Unsupported', {
        'message': 'No hay systemd de usuario en esta sesión.',
      }),
    );
    expect(
      find.text('No hay systemd de usuario en esta sesión.'),
      findsWidgets,
    );
    expect(find.text(Strings.logTitle), findsOneWidget);
  });

  testWidgets('Copiar: todas las líneas completas, tal como llegan', (
    tester,
  ) async {
    String? copied;
    tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
      SystemChannels.platform,
      (call) async {
        if (call.method == 'Clipboard.setData') {
          copied = (call.arguments as Map)['text'] as String;
        }
        return null;
      },
    );
    addTearDown(
      () => tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
        SystemChannels.platform,
        null,
      ),
    );
    final lines = manyLines();
    await _openLog(tester, (_) => logResult(lines: lines));
    await tester.tap(find.text(Strings.copy));
    await tester.pump();
    expect(copied, lines.join('\n'));
    await tester.pump(const Duration(milliseconds: 200));
    expect(find.text(Strings.copied), findsOneWidget);
    await tester.pumpAndSettle(const Duration(seconds: 3));
  });

  testWidgets('Actualizar: lo nuevo y otra vez abajo', (tester) async {
    var lines = manyLines();
    final server = await _openLog(tester, (_) => logResult(lines: lines));
    // Arriba de todo, para ver que regresa abajo.
    await tester.drag(find.text('mensaje de hoy 29'), const Offset(0, 3000));
    await tester.pumpAndSettle();
    expect(find.text('mensaje de hoy 29').hitTestable(), findsNothing);
    lines = [...lines, journalLine(0, '10:00:00', 'la nueva')];
    await tester.tap(find.text(Strings.refreshLog));
    await tester.pumpAndSettle();
    expect(server.methods.where((m) => m == 'service.log'), hasLength(2));
    expect(find.text('la nueva').hitTestable(), findsOneWidget);
  });

  testWidgets('Esc cierra y el foco regresa a Ver registro', (tester) async {
    await _openLog(tester, (_) => logResult());
    await tester.sendKeyEvent(LogicalKeyboardKey.escape);
    await tester.pumpAndSettle();
    expect(find.text(Strings.logTitle), findsNothing);
    expect(FocusManager.instance.primaryFocus?.debugLabel, 'Ver registro');
    // Configuración sigue abierta: el Esc fue de la ventana.
    expect(find.text(Strings.telegramTitle), findsWidgets);
  });
}
