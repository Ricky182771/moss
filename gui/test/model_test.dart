import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/strings.dart';
import 'package:moss_gui/widgets/controls.dart';
import 'package:moss_gui/widgets/markdown_view.dart';
import 'package:moss_gui/widgets/settings_form.dart';

import 'fake_server.dart';
import 'harness.dart';

Future<void> _openModel(WidgetTester tester) async {
  await tester.sendKeyEvent(LogicalKeyboardKey.f2);
  await tester.pumpAndSettle();
  await tester.tap(find.text(Strings.modelTitle).first);
  await tester.pumpAndSettle();
}

Future<void> _tap(WidgetTester tester, Finder f) async {
  await tester.ensureVisible(f);
  await tester.pumpAndSettle();
  await tester.tap(f);
  await tester.pumpAndSettle();
}

/// El campo con [label] arriba (los numéricos y el JSON).
Finder _field(String label) =>
    find.byWidgetPredicate((w) => w is FieldBox && w.label == label);

Finder _editable(String label) =>
    find.descendant(of: _field(label), matching: find.byType(EditableText));

Map<String, dynamic> _last(FakeServer server, String method) =>
    server.requests.lastWhere((r) => r['method'] == method)['params']
        as Map<String, dynamic>;

void main() {
  testWidgets('el título lleva el modelo y el proveedor', (tester) async {
    await startApp(tester);
    await _openModel(tester);
    expect(
      find.text(Strings.modelTitleFor('z-ai/glm5', 'NVIDIA NIM')),
      findsOneWidget,
    );
    expect(find.text(Strings.reasoningHelp('toggle')!), findsOneWidget);
    expect(
      find.text(Strings.range('0', '2', exclusive: false)),
      findsOneWidget,
    );
    expect(find.text(Strings.range('0', '1', exclusive: true)), findsOneWidget);
    expect(
      find.text(Strings.range('1', '1 000 000', exclusive: false)),
      findsOneWidget,
    );
  });

  testWidgets('instrucciones: los tres modos, contador y Restaurar', (
    tester,
  ) async {
    final (_, server) = await startApp(
      tester,
      handlers: {
        'settings.system_prompt.save': (_) => {'mode': 'custom'},
      },
    );
    await _openModel(tester);
    expect(find.text(Strings.promptText), findsNothing);

    await _tap(tester, find.text(Strings.promptCustom));
    // Empieza con el predeterminado para editarlo; el contador, del servidor.
    expect(
      tester
          .widget<EditableText>(_editable(Strings.promptText))
          .controller
          .text,
      kDefaultPrompt,
    );
    expect(
      find.text(Strings.bytesCounter('${kDefaultPrompt.length}', '8000')),
      findsOneWidget,
    );
    await tester.enterText(_editable(Strings.promptText), 'Eres un tutor.');
    await tester.pumpAndSettle(const Duration(milliseconds: 500));
    expect(find.text(Strings.bytesCounter('14', '8000')), findsOneWidget);

    await _tap(tester, find.text(Strings.promptNone));
    await tester.tap(find.text(Strings.save));
    await tester.pumpAndSettle();
    expect(_last(server, 'settings.system_prompt.save'), {'text': ''});
  });

  testWidgets('Restaurar predeterminadas manda default_text', (tester) async {
    final (_, server) = await startApp(
      tester,
      handlers: {
        'settings.system_prompt.get': (_) =>
            systemPrompt(mode: 'custom', text: 'Eres un tutor.'),
        'settings.system_prompt.save': (_) => {'mode': 'default'},
      },
    );
    await _openModel(tester);
    await _tap(tester, find.text(Strings.restorePrompt));
    await tester.tap(find.text(Strings.save));
    await tester.pumpAndSettle();
    expect(_last(server, 'settings.system_prompt.save'), {
      'text': kDefaultPrompt,
    });
  });

  testWidgets('field_errors junto al campo y no se guarda', (tester) async {
    final (_, server) = await startApp(
      tester,
      handlers: {
        'settings.model_params.check': (p) => {
          'field_errors': (p['params'] as Map)['top_k'] == 2000
              ? {'top_k': 'Debe ser un número entero entre 1 y 1000.'}
              : <String, String>{},
          'extra_error': null,
          'preview': null,
        },
      },
    );
    await _openModel(tester);
    await tester.ensureVisible(_field('Top K'));
    await tester.enterText(_editable('Top K'), '2000');
    await tester.pumpAndSettle(const Duration(milliseconds: 500));
    expect(
      find.text('Debe ser un número entero entre 1 y 1000.'),
      findsOneWidget,
    );
    await tester.tap(find.text(Strings.save));
    await tester.pumpAndSettle();
    expect(server.methods, isNot(contains('settings.model_params.save')));
    expect(
      find.text('Debe ser un número entero entre 1 y 1000.'),
      findsOneWidget,
    );
  });

  testWidgets('lo que no es número se marca sin mandarlo', (tester) async {
    final (_, server) = await startApp(tester);
    await _openModel(tester);
    final before = server.methods
        .where((m) => m == 'settings.model_params.check')
        .length;
    await tester.ensureVisible(_field('Semilla'));
    await tester.enterText(_editable('Semilla'), 'abc');
    await tester.pumpAndSettle(const Duration(milliseconds: 500));
    expect(find.text(Strings.notANumber), findsOneWidget);
    expect(
      server.methods.where((m) => m == 'settings.model_params.check').length,
      before,
    );
  });

  testWidgets('"0.60" es lo guardado (0.6): sin cambios tras guardar', (
    tester,
  ) async {
    final (_, server) = await startApp(
      tester,
      handlers: {'settings.model_params.save': (_) => <String, Object?>{}},
    );
    await _openModel(tester);
    await tester.ensureVisible(_field('Temperatura'));
    await tester.enterText(_editable('Temperatura'), '0.60');
    await tester.pumpAndSettle(const Duration(milliseconds: 500));
    expect(
      find.bySemanticsLabel(Strings.withUnsaved(Strings.modelTitle)),
      findsNothing,
    );
    await tester.enterText(_editable('Temperatura'), '0,7');
    await tester.pumpAndSettle(const Duration(milliseconds: 500));
    await tester.tap(find.text(Strings.save));
    await tester.pumpAndSettle();
    final params = _last(server, 'settings.model_params.save')['params'] as Map;
    expect(params['temperature'], 0.7);
    expect(
      find.bySemanticsLabel(Strings.withUnsaved(Strings.modelTitle)),
      findsNothing,
    );
  });

  testWidgets('un entero con decimales se marca y no se guarda', (
    tester,
  ) async {
    final (_, server) = await startApp(tester);
    await _openModel(tester);
    await tester.ensureVisible(_field('Top K'));
    await tester.enterText(_editable('Top K'), '2.5');
    await tester.pumpAndSettle(const Duration(milliseconds: 500));
    expect(find.text(Strings.notAnInteger), findsOneWidget);
    await tester.tap(find.text(Strings.save));
    await tester.pumpAndSettle();
    expect(server.methods, isNot(contains('settings.model_params.save')));
  });

  testWidgets('Avanzado: extra_error, vista previa y Copiar', (tester) async {
    await startApp(
      tester,
      handlers: {
        'settings.model_params.check': (p) =>
            (p['extra'] as String).contains('{') &&
                !(p['extra'] as String).contains('}')
            ? {
                'field_errors': <String, String>{},
                'extra_error':
                    'El JSON avanzado no es válido (línea 1, columna 2).',
                'preview': null,
              }
            : {
                'field_errors': <String, String>{},
                'extra_error': null,
                'preview': '{\n  "model": "z-ai/glm5",\n  "x": 1\n}',
              },
      },
    );
    await _openModel(tester);
    expect(find.text(Strings.extraJson), findsNothing); // Cerrado.
    await _tap(tester, find.text(Strings.advanced));
    expect(find.textContaining('"temperature": 0.6'), findsOneWidget);

    await tester.ensureVisible(_field(Strings.extraJson));
    await tester.enterText(_editable(Strings.extraJson), '{');
    await tester.pumpAndSettle(const Duration(milliseconds: 500));
    expect(
      find.text('El JSON avanzado no es válido (línea 1, columna 2).'),
      findsOneWidget,
    );
    await tester.enterText(_editable(Strings.extraJson), '{"x": 1}');
    await tester.pumpAndSettle(const Duration(milliseconds: 500));
    expect(
      find.descendant(
        of: find.byType(CodeBlock),
        matching: find.textContaining('"x": 1'),
      ),
      findsOneWidget,
    );
    expect(find.widgetWithText(Pressable, Strings.copy), findsOneWidget);
  });

  testWidgets('Restaurar todo: todo en null y extra vacío', (tester) async {
    final (_, server) = await startApp(
      tester,
      handlers: {
        'settings.model_params.get': (_) => modelParams(
          params: const {'temperature': 0.6, 'reasoning': 'high'},
          extra: '{"a": 1}',
        ),
        'settings.model_params.save': (_) => <String, Object?>{},
      },
    );
    await _openModel(tester);
    await _tap(tester, find.text(Strings.restoreAll));
    await tester.tap(find.text(Strings.save));
    await tester.pumpAndSettle();
    final sent = _last(server, 'settings.model_params.save');
    expect(sent['extra'], '');
    expect((sent['params'] as Map).values.every((v) => v == null), isTrue);
  });

  testWidgets('razonamiento Alto se manda como high', (tester) async {
    final (_, server) = await startApp(
      tester,
      handlers: {'settings.model_params.save': (_) => <String, Object?>{}},
    );
    await _openModel(tester);
    await _tap(tester, find.text(Strings.reasoningHigh));
    await tester.tap(find.text(Strings.save));
    await tester.pumpAndSettle();
    final params = _last(server, 'settings.model_params.save')['params'] as Map;
    expect(params['reasoning'], 'high');
    expect(params['temperature'], 0.6);
  });

  testWidgets('sin modelo: el texto del servidor y el botón a Proveedor', (
    tester,
  ) async {
    const text =
        'Elige un modelo en Proveedor de IA (o con /model) para ajustarlo.';
    await startApp(
      tester,
      handlers: {
        'settings.model_params.get': (_) =>
            throw const FakeError(-32004, 'Not configured', {'message': text}),
      },
    );
    await _openModel(tester);
    expect(find.text(text), findsOneWidget);
    expect(find.text(Strings.systemPrompt), findsOneWidget);
    expect(find.text(Strings.restoreAll), findsNothing);
    await _tap(tester, find.text(Strings.goToProvider));
    expect(find.text(Strings.providerTitle), findsOneWidget);
    expect(find.byType(SecretField), findsOneWidget);
  });
}
