import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/strings.dart';
import 'package:moss_gui/theme/motion.dart';
import 'package:moss_gui/theme/theme.dart';
import 'package:moss_gui/widgets/entry_view.dart';
import 'package:moss_gui/widgets/markdown_view.dart';
import 'package:moss_gui/widgets/message_list.dart';
import 'package:moss_gui/widgets/sidebar.dart';

import 'fake_server.dart';
import 'harness.dart';

void main() {
  group('barra lateral', () {
    testWidgets('grupos, globo, ilegible y abierto', (tester) async {
      final (_, server) = await startApp(
        tester,
        conversation: snapshot(id: 'a', title: 'Abierto'),
        summaries: [
          summary('a', 'Abierto', group: 'Hoy'),
          summary('b', 'Con búsqueda', group: 'Hoy', usedSearch: true),
          summary(
            'c',
            '',
            group: 'Sin fecha',
            readable: false,
            error: 'No se pudo leer.',
          ),
        ],
      );
      expect(find.text('HOY'), findsOneWidget);
      expect(find.text('SIN FECHA'), findsOneWidget);
      expect(
        find.bySemanticsLabel(RegExp(Strings.withWebSearch)),
        findsOneWidget,
      );
      // El abierto, resaltado.
      expect(
        tester.getSemantics(find.text('Abierto').first),
        isSemantics(isSelected: true, isButton: true),
      );
      // El ilegible: atenuado, con su error en la etiqueta y sin abrirse.
      expect(find.bySemanticsLabel(RegExp('No se pudo leer')), findsOneWidget);
      await tester.tap(find.text('c'));
      await tester.pump();
      expect(server.methods, isNot(contains('conversation.open')));
    });

    testWidgets('abrir un chat', (tester) async {
      final (app, server) = await startApp(
        tester,
        summaries: [summary('b', 'Otro')],
        handlers: {
          'conversation.open': (p) =>
              snapshot(handle: 'c2', id: p['id'], title: 'Otro'),
        },
      );
      await tester.tap(find.text('Otro'));
      await tester.pump();
      expect(server.methods, contains('conversation.open'));
      expect(app.conversation!.conversation, 'c2');
    });
  });

  group('cada tipo de entrada', () {
    testWidgets('user, assistant, error, notice, sources y marcas', (
      tester,
    ) async {
      await startApp(
        tester,
        conversation: snapshot(
          rev: 6,
          entries: [
            entry('user', text: 'Pregunta'),
            entry('assistant', text: 'Respuesta **fuerte**', note: 'Nota'),
            entry('error', text: '[error de red] Sin conexión'),
            entry('notice', text: 'Un aviso'),
            entry(
              'sources',
              sources: [
                {
                  'n': 1,
                  'title': 'Kernel',
                  'url': 'https://kernel.org',
                  'published_date': '2026-10-20',
                },
              ],
            ),
            entry('assistant', text: 'Corta', cancelled: true),
            entry('assistant', text: 'Larga', incomplete: true),
          ],
        ),
      );
      expect(find.text('Pregunta'), findsOneWidget);
      expect(find.textContaining('Respuesta'), findsOneWidget);
      expect(find.text('Nota'), findsOneWidget);
      expect(find.text('[error de red] Sin conexión'), findsOneWidget);
      expect(find.byIcon(Icons.error_outline_rounded), findsOneWidget);
      expect(find.text('Un aviso'), findsOneWidget);
      expect(find.text(Strings.sources), findsOneWidget);
      expect(find.text('1.'), findsOneWidget);
      expect(find.text('Kernel'), findsOneWidget);
      expect(find.text('· 2026-10-20'), findsOneWidget);
      expect(find.text(Strings.cancelled), findsOneWidget);
      expect(find.text(Strings.incomplete), findsOneWidget);
    });

    testWidgets('bloque de código con Copiar', (tester) async {
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
      await startApp(
        tester,
        conversation: snapshot(
          rev: 1,
          entries: [entry('assistant', text: '```cpp\nint x = 1;\n```')],
        ),
      );
      expect(find.text('cpp'), findsOneWidget);
      await tester.tap(find.text(Strings.copy));
      await tester.pump();
      expect(copied, 'int x = 1;');
      expect(find.text(Strings.copied), findsOneWidget);
      await tester.pump(Motion.copiedVisible);
      await tester.pumpAndSettle();
      expect(find.text(Strings.copy), findsOneWidget);
    });
  });

  group('Pensamiento', () {
    testWidgets('por mensaje y plegado por defecto', (tester) async {
      await startApp(
        tester,
        conversation: snapshot(
          rev: 2,
          entries: [
            entry('assistant', text: 'Uno', reasoning: 'pienso uno'),
            entry('assistant', text: 'Dos', reasoning: 'pienso dos'),
          ],
        ),
      );
      final thoughts = find.byType(ThoughtView);
      expect(thoughts, findsNWidgets(2));
      expect(_folds(tester), [0, 0]);
      await tester.tap(find.text('Pensamiento · 2 palabras').first);
      await tester.pumpAndSettle();
      expect(_folds(tester), [1, 0]);
    });

    testWidgets('abierto mientras llega y se pliega con el texto', (
      tester,
    ) async {
      final (_, server) = await startApp(
        tester,
        conversation: snapshot(
          rev: 2,
          phase: 'thinking',
          entries: [
            entry('user', text: 'hola'),
            entry('assistant', reasoning: 'Estoy pensando', inProgress: true),
          ],
        ),
      );
      expect(_folds(tester), [1]);
      // "Pensando…" no sale: ya se ve el pensamiento llegando.
      expect(find.text(Strings.thinking), findsNothing);
      server.notify('conversation.entry_delta', {
        'conversation': 'c1',
        'rev': 3,
        'index': 1,
        'text': 'Hola',
      });
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 300));
      expect(_folds(tester), [0]);
    });
  });

  group('pegado abajo', () {
    testWidgets('sigue la respuesta si ya estaba abajo', (tester) async {
      final (_, server) = await startApp(
        tester,
        conversation: longChat(phase: 'streaming', inProgress: true),
      );
      final pos = _scroll(tester);
      expect(pos.pixels, pos.maxScrollExtent);
      server.notify('conversation.entry_delta', {
        'conversation': 'c1',
        'rev': 3,
        'index': 1,
        'text': '\n\nMás texto.\n\nY más.\n\nY más.',
      });
      await tester.pump();
      await tester.pump();
      expect(pos.pixels, pos.maxScrollExtent);
    });

    testWidgets('no baja si el usuario subió', (tester) async {
      final (_, server) = await startApp(
        tester,
        conversation: longChat(phase: 'streaming', inProgress: true),
      );
      final pos = _scroll(tester);
      await tester.drag(find.byType(MessageList), const Offset(0, 400));
      // Sin pumpAndSettle: el cursor parpadea mientras llega.
      await tester.pump();
      await tester.pump(const Duration(seconds: 1));
      final before = pos.pixels;
      expect(before, lessThan(pos.maxScrollExtent));
      server.notify('conversation.entry_delta', {
        'conversation': 'c1',
        'rev': 3,
        'index': 1,
        'text': '\n\nMás texto.\n\nY más.',
      });
      await tester.pump();
      await tester.pump();
      expect(pos.pixels, before);
    });
  });

  group('caja del mensaje', () {
    testWidgets('Enter envía, Shift+Enter hace una nueva línea', (
      tester,
    ) async {
      final (_, server) = await startApp(tester);
      await tester.enterText(find.byType(TextField), 'hola');
      await tester.sendKeyDownEvent(LogicalKeyboardKey.shiftLeft);
      await tester.sendKeyEvent(LogicalKeyboardKey.enter);
      await tester.sendKeyUpEvent(LogicalKeyboardKey.shiftLeft);
      await tester.pump();
      expect(server.methods, isNot(contains('chat.send')));
      await tester.sendKeyEvent(LogicalKeyboardKey.enter);
      await tester.pump();
      final send = server.requests.firstWhere(
        (r) => r['method'] == 'chat.send',
      );
      expect(send['params'], {'conversation': 'c1', 'text': 'hola'});
      expect(_input(tester).text, isEmpty);
    });

    testWidgets('Enter mantenido envía una sola vez', (tester) async {
      final (_, server) = await startApp(tester);
      await tester.enterText(find.byType(TextField), 'hola');
      await tester.sendKeyDownEvent(LogicalKeyboardKey.enter);
      // Atendida aquí: si siguiera de largo, GTK metería un salto de línea.
      expect(await tester.sendKeyRepeatEvent(LogicalKeyboardKey.enter), true);
      expect(await tester.sendKeyRepeatEvent(LogicalKeyboardKey.enter), true);
      await tester.sendKeyUpEvent(LogicalKeyboardKey.enter);
      await tester.pump();
      expect(server.methods.where((m) => m == 'chat.send'), hasLength(1));
      expect(_input(tester).text, isEmpty);
    });

    testWidgets('/comandos van por commands.run; status es un aviso', (
      tester,
    ) async {
      final (_, server) = await startApp(
        tester,
        handlers: {
          'commands.run': (_) => {
            'kind': 'status',
            'message': 'Uso: /copiar [número de bloque]',
            'clear': false,
          },
        },
      );
      await tester.enterText(find.byType(TextField), '/copiar x');
      await tester.sendKeyEvent(LogicalKeyboardKey.enter);
      await tester.pump();
      expect(server.methods, contains('commands.run'));
      expect(find.text('Uso: /copiar [número de bloque]'), findsOneWidget);
      expect(_input(tester).text, '/copiar x');
      await tester.pump(Motion.toastVisible);
      await tester.pumpAndSettle();
    });

    testWidgets('Busy va junto a la caja, en el lugar de la ayuda', (
      tester,
    ) async {
      await startApp(
        tester,
        handlers: {
          'chat.send': (_) => throw const FakeError(-32002, 'Busy', {
            'message': 'Espera la respuesta o cancélala con Esc.',
          }),
        },
      );
      final boxTop = tester.getTopLeft(find.byType(TextField));
      await tester.enterText(find.byType(TextField), 'hola');
      await tester.sendKeyEvent(LogicalKeyboardKey.enter);
      await tester.pumpAndSettle();
      expect(
        find.text('Espera la respuesta o cancélala con Esc.'),
        findsOneWidget,
      );
      expect(find.text(Strings.help), findsNothing);
      expect(tester.getTopLeft(find.byType(TextField)), boxTop);
    });

    testWidgets('Detener y Esc con una respuesta en curso', (tester) async {
      final (_, server) = await startApp(
        tester,
        conversation: longChat(phase: 'streaming', inProgress: true),
      );
      expect(find.bySemanticsLabel(Strings.send), findsNothing);
      await tester.tap(find.bySemanticsLabel(Strings.stop));
      await tester.pump();
      expect(server.methods.where((m) => m == 'chat.cancel'), hasLength(1));
      await tester.sendKeyEvent(LogicalKeyboardKey.escape);
      await tester.pump();
      expect(server.methods.where((m) => m == 'chat.cancel'), hasLength(2));
    });

    testWidgets('Enviar deshabilitado sin texto', (tester) async {
      await startApp(tester);
      expect(
        tester.getSemantics(find.bySemanticsLabel(Strings.send)),
        isSemantics(isButton: true, isEnabled: false, hasEnabledState: true),
      );
    });
  });

  testWidgets('viñetas a 4 px y bloques a 14 px, como la maqueta', (
    tester,
  ) async {
    await tester.pumpWidget(
      MaterialApp(
        theme: MossTheme.dark(),
        home: const Material(
          child: MarkdownView('Antes\n\n- uno\n- dos\n\nDespués'),
        ),
      ),
    );
    Rect box(String t) => tester.getRect(find.text(t, findRichText: true).last);
    expect(box('dos').top - box('uno').bottom, 4);
    expect(box('uno').top - box('Antes').bottom, 14);
    expect(box('Después').top - box('dos').bottom, 14);
  });

  testWidgets('las imágenes del Markdown no se cargan: solo su alt', (
    tester,
  ) async {
    await startApp(
      tester,
      conversation: snapshot(
        rev: 1,
        entries: [
          entry('assistant', text: '![un gato](https://ejemplo.com/x.png)'),
        ],
      ),
    );
    expect(find.byType(Image), findsNothing);
    expect(find.text('un gato'), findsOneWidget);
  });

  testWidgets('el error de Exportar es un aviso corto, no junto a la caja', (
    tester,
  ) async {
    await startApp(
      tester,
      handlers: {
        'conversation.export': (_) => throw const FakeError(
          -32005,
          'ValidationFailed',
          {'message': 'Todavía no hay respuestas que exportar.'},
        ),
      },
    );
    await tester.tap(find.text(Strings.export));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 200));
    expect(
      find.text('Todavía no hay respuestas que exportar.'),
      findsOneWidget,
    );
    expect(find.text(Strings.help), findsOneWidget);
    await tester.pump(Motion.toastVisible);
    await tester.pumpAndSettle();
  });

  testWidgets('chat vacío: logo y "¿En qué te ayudo?"', (tester) async {
    await startApp(tester);
    expect(find.text(Strings.emptyPrompt), findsOneWidget);
    expect(find.text(Strings.newChat), findsWidgets);
  });

  testWidgets('Exportar muestra el aviso corto', (tester) async {
    await startApp(
      tester,
      handlers: {
        'conversation.export': (_) => {
          'path': '/tmp/x.md',
          'message': 'Conversación exportada a ~/x.md',
        },
      },
    );
    await tester.tap(find.text(Strings.export));
    await tester.pump();
    await tester.pump(Motion.toastVisible ~/ 2);
    expect(find.text('Conversación exportada a ~/x.md'), findsOneWidget);
    await tester.pump(Motion.toastVisible);
    await tester.pumpAndSettle();
    expect(find.text('Conversación exportada a ~/x.md'), findsNothing);
  });

  testWidgets('Ctrl+N pide un chat nuevo', (tester) async {
    final (_, server) = await startApp(
      tester,
      handlers: {'conversation.new': (_) => snapshot(handle: 'c2')},
    );
    await tester.sendKeyDownEvent(LogicalKeyboardKey.controlLeft);
    await tester.sendKeyEvent(LogicalKeyboardKey.keyN);
    await tester.sendKeyUpEvent(LogicalKeyboardKey.controlLeft);
    await tester.pump();
    expect(server.methods, contains('conversation.new'));
  });

  group('ventana angosta', () {
    testWidgets('la barra se oculta sola y Ctrl+B la abre encima', (
      tester,
    ) async {
      await startApp(
        tester,
        size: const Size(800, 600),
        summaries: [summary('a', 'Un chat')],
      );
      expect(find.byType(Sidebar), findsNothing);
      final inputLeft = tester.getTopLeft(find.byType(TextField)).dx;
      await tester.sendKeyDownEvent(LogicalKeyboardKey.controlLeft);
      await tester.sendKeyEvent(LogicalKeyboardKey.keyB);
      await tester.sendKeyUpEvent(LogicalKeyboardKey.controlLeft);
      await tester.pumpAndSettle();
      expect(find.text('Un chat'), findsOneWidget);
      // Encima: el chat no se movió.
      expect(tester.getTopLeft(find.byType(TextField)).dx, inputLeft);
    });

    testWidgets('ancha: Ctrl+B oculta la barra', (tester) async {
      await startApp(tester, summaries: [summary('a', 'Un chat')]);
      expect(find.text('Un chat'), findsOneWidget);
      await tester.sendKeyDownEvent(LogicalKeyboardKey.controlLeft);
      await tester.sendKeyEvent(LogicalKeyboardKey.keyB);
      await tester.sendKeyUpEvent(LogicalKeyboardKey.controlLeft);
      await tester.pumpAndSettle();
      expect(find.text('Un chat'), findsNothing);
    });
  });

  group('disableAnimations', () {
    testWidgets('todas las duraciones a 0 ms', (tester) async {
      late Motion m;
      await tester.pumpWidget(
        MediaQuery(
          data: const MediaQueryData(disableAnimations: true),
          child: Builder(
            builder: (context) {
              m = Motion.of(context);
              return const SizedBox();
            },
          ),
        ),
      );
      expect(m.reduced, isTrue);
      expect(
        {
          m.hover,
          m.tooltip,
          m.menuOpen,
          m.menuClose,
          m.searchDialog,
          m.screenFade,
          m.sidebar,
          m.inputGrow,
          m.messageIn,
          m.word,
          m.thinkingFold,
          m.thinkingAutoFold,
          m.copySwap,
          m.fieldError,
          m.toast,
          m.cursorBlink,
          m.thinkingPulse,
        },
        {Duration.zero},
      );
    });

    testWidgets('la barra y el cursor no animan', (tester) async {
      await startApp(
        tester,
        disableAnimations: true,
        conversation: longChat(phase: 'streaming', inProgress: true),
        summaries: [summary('a', 'Un chat')],
      );
      // Se asienta: el cursor de la respuesta no parpadea.
      await tester.pumpAndSettle();
      await tester.sendKeyDownEvent(LogicalKeyboardKey.controlLeft);
      await tester.sendKeyEvent(LogicalKeyboardKey.keyB);
      await tester.sendKeyUpEvent(LogicalKeyboardKey.controlLeft);
      await tester.pump();
      expect(find.text('Un chat'), findsNothing);
      // Ni el cursor que parpadea ni nada más queda animando.
      expect(tester.hasRunningAnimations, isFalse);
    });
  });
}

/// Cuánto está abierto cada Pensamiento (0 plegado, 1 abierto).
List<double> _folds(WidgetTester tester) => [
  for (final t in tester.widgetList<SizeTransition>(
    find.descendant(
      of: find.byType(ThoughtView),
      matching: find.byType(SizeTransition),
    ),
  ))
    t.sizeFactor.value,
];

ScrollPosition _scroll(WidgetTester tester) => tester
    .state<ScrollableState>(
      find
          .ancestor(
            of: find.byType(EntryView).first,
            matching: find.byType(Scrollable),
          )
          .first,
    )
    .position;

TextEditingController _input(WidgetTester tester) =>
    tester.widget<TextField>(find.byType(TextField)).controller!;
