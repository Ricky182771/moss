import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/screens/settings_screen.dart';
import 'package:moss_gui/strings.dart';
import 'package:moss_gui/widgets/controls.dart';
import 'package:moss_gui/widgets/settings_form.dart';

import 'fake_server.dart';
import 'harness.dart';

const _key = 'nvapi-secreta-1234567890';

Future<void> _open(WidgetTester tester) async {
  await tester.sendKeyEvent(LogicalKeyboardKey.f2);
  await tester.pumpAndSettle();
}

/// Lleva [f] a la vista (el panel se desplaza) y lo toca.
Future<void> _tap(WidgetTester tester, Finder f) async {
  await tester.ensureVisible(f);
  await tester.pumpAndSettle();
  await tester.tap(f);
}

Finder _row(String text) => find.widgetWithText(Pressable, text).first;

Finder get _keyField => find.descendant(
  of: find.byType(SecretField),
  matching: find.byType(EditableText),
);

Finder _field(String label) => find.descendant(
  of: find.widgetWithText(LabeledField, label),
  matching: find.byType(EditableText),
);

Map<String, dynamic>? _last(FakeServer server, String method) =>
    server.requests.lastWhere(
          (r) => r['method'] == method,
          orElse: () => {},
        )['params']
        as Map<String, dynamic>?;

/// Ninguna key en el árbol: ni en un texto ni en un campo.
void _expectNoKey(WidgetTester tester) {
  expect(find.textContaining(_key), findsNothing);
  for (final e in tester.widgetList<EditableText>(find.byType(EditableText))) {
    expect(e.controller.text, isNot(contains(_key)));
  }
}

void main() {
  testWidgets('dos grupos; un «sin key» se puede elegir para ponerle key', (
    tester,
  ) async {
    await startApp(tester);
    await _open(tester);
    expect(find.text(Strings.fromTable.toUpperCase()), findsOneWidget);
    expect(find.text(Strings.yourEndpoints.toUpperCase()), findsOneWidget);
    expect(find.text('sin key'), findsOneWidget);

    await _tap(tester, _row('OmniRoute'));
    await tester.pumpAndSettle();
    expect(find.text('custom:omniroute'), findsOneWidget);
    expect(find.text(Strings.endpointName), findsOneWidget);
    expect(find.text(Strings.baseUrl), findsOneWidget);
    expect(
      find.bySemanticsLabel(Strings.withUnsaved(Strings.providerMenu)),
      findsOneWidget,
    );

    // Uno de la tabla: solo la key.
    await _tap(tester, _row('Google Gemini'));
    await tester.pumpAndSettle();
    expect(find.text(Strings.endpointName), findsNothing);
    expect(find.byType(SecretField), findsOneWidget);
  });

  testWidgets('cada proveedor con su key_status, el que manda el servidor', (
    tester,
  ) async {
    await startApp(tester);
    await _open(tester);
    expect(find.textContaining('guardada: …wxyz'), findsOneWidget);
    expect(find.text('key guardada'), findsNothing);

    await _tap(tester, _row('Google Gemini'));
    await tester.pumpAndSettle();
    final field = tester.widget<SecretField>(find.byType(SecretField));
    expect(field.status, 'guardada: …wxyz');

    await _tap(tester, _row('OmniRoute'));
    await tester.pumpAndSettle();
    expect(tester.widget<SecretField>(find.byType(SecretField)).status, '');
  });

  testWidgets('la key va enmascarada; ver se apaga al salir del campo', (
    tester,
  ) async {
    await startApp(
      tester,
      handlers: {
        'settings.provider.get': (_) =>
            providerSettings(keyStatus: 'guardada: …abcd'),
      },
    );
    await _open(tester);
    expect(find.text('guardada: …abcd'), findsOneWidget);
    await tester.enterText(_keyField, _key);
    await tester.pump();
    expect(tester.widget<EditableText>(_keyField).obscureText, isTrue);

    await tester.tap(find.bySemanticsLabel(Strings.showKey));
    await tester.pump();
    expect(tester.widget<EditableText>(_keyField).obscureText, isFalse);

    // Tab lleva al botón de ver: se sigue viendo. Otro Tab sale: se tapa.
    await tester.sendKeyEvent(LogicalKeyboardKey.tab);
    await tester.pump();
    expect(tester.widget<EditableText>(_keyField).obscureText, isFalse);
    await tester.sendKeyEvent(LogicalKeyboardKey.tab);
    await tester.pump();
    expect(tester.widget<EditableText>(_keyField).obscureText, isTrue);
  });

  testWidgets('la key a la vista no se copia ni se corta', (tester) async {
    final copied = <Object?>[];
    tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
      SystemChannels.platform,
      (call) async {
        if (call.method == 'Clipboard.setData') copied.add(call.arguments);
        return null;
      },
    );
    addTearDown(
      () => tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
        SystemChannels.platform,
        null,
      ),
    );
    await startApp(tester);
    await _open(tester);
    await tester.enterText(_keyField, _key);
    await tester.tap(find.bySemanticsLabel(Strings.showKey));
    await tester.pump();
    await tester.tap(_keyField);
    await tester.pump();
    for (final k in [LogicalKeyboardKey.keyA, LogicalKeyboardKey.keyC]) {
      await tester.sendKeyDownEvent(LogicalKeyboardKey.controlLeft);
      await tester.sendKeyEvent(k);
      await tester.sendKeyUpEvent(LogicalKeyboardKey.controlLeft);
      await tester.pump();
    }
    await tester.sendKeyDownEvent(LogicalKeyboardKey.controlLeft);
    await tester.sendKeyEvent(LogicalKeyboardKey.keyX);
    await tester.sendKeyUpEvent(LogicalKeyboardKey.controlLeft);
    await tester.pump();
    expect(copied, isEmpty);
    expect(tester.widget<EditableText>(_keyField).controller.text, _key);
  });

  testWidgets('el check va con retraso al escribir, no por tecla', (
    tester,
  ) async {
    final (_, server) = await startApp(tester);
    await _open(tester);
    int checks() =>
        server.methods.where((m) => m == 'settings.provider.check').length;
    final before = checks();
    await tester.enterText(_field(Strings.modelByHand), 'a');
    await tester.pump(const Duration(milliseconds: 100));
    await tester.enterText(_field(Strings.modelByHand), 'ab');
    await tester.pump(const Duration(milliseconds: 100));
    expect(checks(), before);
    await tester.pump(const Duration(milliseconds: 450));
    expect(checks(), before + 1);
  });

  testWidgets('guardar una key: va en el borrador y luego no queda en el '
      'árbol', (tester) async {
    final (_, server) = await startApp(
      tester,
      handlers: {
        'settings.provider.save': (p) => {'ready': true, 'provider': 'nvidia'},
      },
    );
    await _open(tester);
    await tester.enterText(_keyField, _key);
    await tester.pumpAndSettle();
    await tester.tap(find.text(Strings.save));
    await tester.pumpAndSettle();

    final draft = _last(server, 'settings.provider.save')!['draft'] as Map;
    expect(draft['api_key'], _key);
    expect(draft['provider'], 'nvidia');
    _expectNoKey(tester);
    expect(
      find.bySemanticsLabel(Strings.withUnsaved(Strings.providerMenu)),
      findsNothing,
    );
  });

  testWidgets('agregar endpoint: campos vacíos, foco en Nombre, provider '
      '«custom:»', (tester) async {
    final (_, server) = await startApp(
      tester,
      handlers: {
        'settings.provider.save': (p) => {
          'ready': true,
          'provider': 'custom:lab',
        },
      },
    );
    await _open(tester);
    await _tap(tester, find.text(Strings.addEndpoint));
    await tester.pumpAndSettle();
    expect(find.text(Strings.newEndpoint), findsWidgets);
    expect(
      Focus.of(tester.element(_field(Strings.endpointName))).hasFocus,
      isTrue,
    );

    await tester.enterText(_field(Strings.endpointName), 'Lab');
    await tester.enterText(_field(Strings.baseUrl), 'http://127.0.0.1:8080/v1');
    await tester.enterText(_field(Strings.modelByHand), 'qwen');
    await tester.pumpAndSettle();
    await tester.tap(find.text(Strings.save));
    await tester.pumpAndSettle();
    final draft = _last(server, 'settings.provider.save')!['draft'] as Map;
    expect(draft, {
      'provider': 'custom:',
      'name': 'Lab',
      'base_url': 'http://127.0.0.1:8080/v1',
      'model': 'qwen',
    });
  });

  testWidgets('editar el nombre de un endpoint no manda key', (tester) async {
    final (_, server) = await startApp(
      tester,
      handlers: {
        'settings.provider.get': (_) =>
            providerSettings(provider: 'custom:omniroute'),
        'settings.provider.save': (p) => {
          'ready': true,
          'provider': 'custom:omniroute',
        },
      },
    );
    await _open(tester);
    await tester.enterText(_field(Strings.endpointName), 'Omni');
    await tester.pumpAndSettle();
    await tester.tap(find.text(Strings.save));
    await tester.pumpAndSettle();
    final draft = _last(server, 'settings.provider.save')!['draft'] as Map;
    expect(draft['name'], 'Omni');
    expect(draft.containsKey('api_key'), isFalse);
  });

  testWidgets('borrar endpoint: atenuado en uso; si no, pregunta y se va '
      'al momento', (tester) async {
    var withLab = true;
    final (_, server) = await startApp(
      tester,
      handlers: {
        'settings.provider.get': (_) =>
            providerSettings(provider: 'custom:omniroute', withLab: withLab),
        'settings.endpoint.delete': (p) {
          withLab = false;
          return <String, Object?>{};
        },
      },
    );
    await _open(tester);
    expect(find.text(Strings.endpointInUse), findsOneWidget);
    await _tap(tester, find.text(Strings.deleteEndpoint));
    await tester.pumpAndSettle();
    expect(
      find.text(Strings.deleteEndpointQuestion('OmniRoute')),
      findsNothing,
    );

    await _tap(tester, _row('Servidor del lab'));
    await tester.pumpAndSettle();
    await _tap(tester, find.text(Strings.deleteEndpoint));
    await tester.pumpAndSettle();
    expect(
      find.text(Strings.deleteEndpointQuestion('Servidor del lab')),
      findsOneWidget,
    );
    await tester.tap(find.text(Strings.confirmDelete));
    await tester.pumpAndSettle();
    expect(_last(server, 'settings.endpoint.delete'), {'id': 'custom:lab'});
    expect(find.text('Servidor del lab'), findsNothing);
    // Regresa al guardado y sin aviso de "cambió afuera".
    expect(find.text(Strings.endpointInUse), findsOneWidget);
    server.notify('settings.changed', {'section': 'provider'});
    await tester.pumpAndSettle();
    expect(find.text(Strings.changedOutside), findsNothing);
  });

  testWidgets('borrar endpoint con error: se queda en la ventana', (
    tester,
  ) async {
    await startApp(
      tester,
      handlers: {
        'settings.provider.get': (_) => providerSettings(withLab: true),
        'settings.endpoint.delete': (_) => throw const FakeError(
          -32005,
          'Error',
          {'message': 'Ese endpoint ya no existe.'},
        ),
      },
    );
    await _open(tester);
    await _tap(tester, _row('Servidor del lab'));
    await tester.pumpAndSettle();
    await _tap(tester, find.text(Strings.deleteEndpoint));
    await tester.pumpAndSettle();
    await tester.tap(find.text(Strings.confirmDelete));
    await tester.pumpAndSettle();
    expect(find.text('Ese endpoint ya no existe.'), findsOneWidget);
  });

  testWidgets('modelos: «Cargando…» tras 300 ms y \$/cancel al cambiar', (
    tester,
  ) async {
    final (_, server) = await startApp(
      tester,
      handlers: {'models.list': (_) => noReply},
    );
    // Sin pumpAndSettle: el indicador gira mientras carga.
    await tester.sendKeyEvent(LogicalKeyboardKey.f2);
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 200));
    expect(find.text(Strings.loadingModels), findsNothing);
    await tester.pump(const Duration(milliseconds: 150));
    expect(find.text(Strings.loadingModels), findsOneWidget);

    await tester.tap(_row('Google Gemini'));
    await tester.pump();
    await tester.pump();
    expect(server.methods, contains(r'$/cancel'));
  });

  testWidgets('otro proveedor que no puede pedir modelos: \$/cancel y sin '
      'la lista anterior', (tester) async {
    var canRequest = true;
    final (_, server) = await startApp(
      tester,
      handlers: {
        'models.list': (_) => noReply,
        'settings.provider.check': (_) =>
            providerCheck(canRequestModels: canRequest),
      },
    );
    await tester.sendKeyEvent(LogicalKeyboardKey.f2);
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400));
    final pending = server.requests.lastWhere(
      (r) => r['method'] == 'models.list',
    )['id'];
    canRequest = false;
    await tester.tap(_row('Google Gemini'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400));
    expect(
      server.requests.where((r) => r['method'] == r'$/cancel').last['params'],
      {'id': pending},
    );
    expect(find.text(Strings.loadingModels), findsNothing);
    expect(
      server.methods.where((m) => m == 'models.list').length,
      1,
      reason: 'sin can_request_models no se piden',
    );
  });

  testWidgets('modelos con error: su mensaje y escribir a mano', (
    tester,
  ) async {
    final (_, server) = await startApp(
      tester,
      handlers: {
        'models.list': (_) => throw const FakeError(-32005, 'Error', {
          'message': '[error de red] no se pudo conectar',
        }),
        'settings.provider.save': (_) => {'ready': true, 'provider': 'nvidia'},
      },
    );
    await _open(tester);
    expect(find.text('[error de red] no se pudo conectar'), findsOneWidget);
    await tester.enterText(_field(Strings.modelByHand), 'a-mano');
    await tester.pumpAndSettle();
    await tester.tap(find.text(Strings.save));
    await tester.pumpAndSettle();
    final draft = _last(server, 'settings.provider.save')!['draft'] as Map;
    expect(draft['model'], 'a-mano');
  });

  testWidgets('elegir un modelo de la lista y Actualizar', (tester) async {
    final (_, server) = await startApp(tester);
    await _open(tester);
    expect(find.text(Strings.modelsCount(2)), findsOneWidget);
    await _tap(tester, _row('meta/llama-3.3-70b'));
    await tester.pumpAndSettle();
    expect(
      tester.widget<EditableText>(_field(Strings.modelByHand)).controller.text,
      'meta/llama-3.3-70b',
    );
    final before = server.methods.where((m) => m == 'models.list').length;
    await _tap(tester, find.text(Strings.refresh));
    await tester.pumpAndSettle();
    expect(server.methods.where((m) => m == 'models.list').length, before + 1);
  });

  testWidgets('fijados por CHAT_*: deshabilitados con la razón', (
    tester,
  ) async {
    await startApp(
      tester,
      handlers: {
        'settings.provider.get': (_) => providerSettings(
          provider: 'custom:omniroute',
          baseUrlLocked: true,
          modelLocked: true,
          apiKeyLocked: true,
        ),
      },
    );
    await _open(tester);
    expect(find.text(Strings.lockedBy('CHAT_BASE_URL')), findsWidgets);
    expect(find.text(Strings.lockedBy('CHAT_MODEL')), findsOneWidget);
    expect(find.text(Strings.lockedBy('CHAT_API_KEY')), findsOneWidget);
    expect(tester.widget<EditableText>(_keyField).readOnly, isTrue);
    expect(
      tester
          .widget<TextField>(
            find.ancestor(
              of: _field(Strings.modelByHand),
              matching: find.byType(TextField),
            ),
          )
          .enabled,
      isFalse,
    );
  });

  group('el error de check va junto a su campo', () {
    const wrong = 'Mensaje del servidor.';
    Future<void> openWith(WidgetTester tester, String? field) async {
      await startApp(
        tester,
        handlers: {
          'settings.provider.check': (_) =>
              providerCheck(error: wrong, errorField: field),
        },
      );
      await _open(tester);
      await _tap(tester, _row('OmniRoute'));
      await tester.pumpAndSettle();
    }

    Finder inside(Type type, String label) => find.descendant(
      of: type == SecretField
          ? find.byType(SecretField)
          : find.widgetWithText(LabeledField, label),
      matching: find.text(wrong),
    );

    for (final (field, type, label) in const [
      ('name', LabeledField, Strings.endpointName),
      ('base_url', LabeledField, Strings.baseUrl),
      ('api_key', SecretField, Strings.apiKey),
      ('model', LabeledField, Strings.modelByHand),
    ]) {
      testWidgets(field, (tester) async {
        await openWith(tester, field);
        expect(find.text(wrong), findsOneWidget);
        expect(inside(type, label), findsOneWidget);
      });
    }

    testWidgets('sin campo: al final de la tarjeta', (tester) async {
      await openWith(tester, null);
      expect(find.text(wrong), findsOneWidget);
      expect(
        find.descendant(
          of: find.byType(LabeledField),
          matching: find.text(wrong),
        ),
        findsNothing,
      );
    });

    testWidgets('de la URL en uno de la tabla, que no tiene ese campo', (
      tester,
    ) async {
      await startApp(
        tester,
        handlers: {
          'settings.provider.check': (_) =>
              providerCheck(error: wrong, errorField: 'base_url'),
        },
      );
      await _open(tester);
      await _tap(tester, _row('Google Gemini'));
      await tester.pumpAndSettle();
      expect(find.text(wrong), findsOneWidget);
    });
  });

  testWidgets('Guardar con un error en otra sección: va ahí y no guarda nada', (
    tester,
  ) async {
    var check = providerCheck();
    final (_, server) = await startApp(
      tester,
      handlers: {
        'settings.provider.check': (_) => check,
        'settings.gui.set': (_) => <String, Object?>{},
      },
    );
    await _open(tester);
    await _tap(tester, _row('Google Gemini'));
    await tester.pumpAndSettle();
    // Gemini sin modelo elegido: el servidor dice que falta.
    check = providerCheck(error: 'Elige un modelo de la lista o escríbelo.');
    await tester.tap(find.text(Strings.colorsTitle).first);
    await tester.pumpAndSettle();
    await _tap(tester, find.text(Strings.themeLight));
    await tester.pump();

    await tester.tap(find.text(Strings.save));
    await tester.pumpAndSettle();
    expect(find.text(Strings.providerTitle), findsOneWidget);
    expect(
      find.text('Elige un modelo de la lista o escríbelo.'),
      findsOneWidget,
    );
    expect(server.methods, isNot(contains('settings.gui.set')));
    expect(server.methods, isNot(contains('settings.provider.save')));
  });

  testWidgets('Guardar con Busy: el mensaje en la barra y lo demás sin '
      'guardar', (tester) async {
    final (_, server) = await startApp(
      tester,
      handlers: {
        'settings.provider.save': (_) => throw const FakeError(-32002, 'Busy', {
          'message': 'Espera la respuesta o cancélala con Esc.',
        }),
        'settings.gui.set': (_) => <String, Object?>{},
      },
    );
    await _open(tester);
    await _tap(tester, _row('meta/llama-3.3-70b'));
    await tester.pumpAndSettle();
    await tester.tap(find.text(Strings.colorsTitle).first);
    await tester.pumpAndSettle();
    await _tap(tester, find.text(Strings.themeLight));
    await tester.pump();

    await tester.tap(find.text(Strings.save));
    await tester.pumpAndSettle();
    expect(
      find.text('Espera la respuesta o cancélala con Esc.'),
      findsOneWidget,
    );
    // Proveedor va antes que Colores: se detuvo ahí.
    expect(find.text(Strings.providerTitle), findsOneWidget);
    expect(server.methods, isNot(contains('settings.gui.set')));
    expect(
      find.bySemanticsLabel(Strings.withUnsaved(Strings.colorsTitle)),
      findsOneWidget,
    );
  });

  testWidgets('al guardar, el aviso del servidor como aviso corto', (
    tester,
  ) async {
    FakeServer? srv;
    var saved = false;
    final (_, server) = await startApp(
      tester,
      handlers: {
        'settings.provider.save': (_) {
          saved = true;
          return {'ready': true, 'provider': 'nvidia'};
        },
        // Los avisos llegan antes de la respuesta de app.status.
        'app.status': (_) {
          if (!saved) return appStatus();
          saved = false;
          srv!.notify('conversation.entry_added', {
            'conversation': 'c1',
            'rev': 1,
            'index': 0,
            'entry': entry('notice', text: 'Ahora se usa meta/llama de NIM.'),
          });
          return appStatus();
        },
      },
    );
    srv = server;
    await _open(tester);
    await _tap(tester, _row('meta/llama-3.3-70b'));
    await tester.pumpAndSettle();
    await tester.tap(find.text(Strings.save));
    // Se queda quieto con el aviso a la vista (se va a los 4 s, por timer).
    await tester.pumpAndSettle();
    expect(
      find.descendant(
        of: find.byType(SettingsScreen),
        matching: find.text('Ahora se usa meta/llama de NIM.'),
      ),
      findsOneWidget,
    );
    await tester.pumpAndSettle(const Duration(seconds: 5));
  });

  testWidgets('settings.changed con cambios: el aviso y no se pisan', (
    tester,
  ) async {
    final (_, server) = await startApp(tester);
    await _open(tester);
    await _tap(tester, _row('Google Gemini'));
    await tester.pumpAndSettle();
    server.notify('settings.changed', {'section': 'provider'});
    await tester.pumpAndSettle();
    expect(find.text(Strings.changedOutside), findsOneWidget);
    expect(find.text('gemini'), findsOneWidget);
  });
}
