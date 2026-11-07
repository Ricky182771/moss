import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/screens/settings_screen.dart';
import 'package:moss_gui/strings.dart';
import 'package:moss_gui/widgets/settings_form.dart';

import 'fake_server.dart';
import 'harness.dart';

const _token = '123456789:AAH-token-secreto-de-prueba';
const _modelKey = 'nvapi-key-secreta-0987654321';

Future<void> _openTelegram(WidgetTester tester) async {
  await tester.sendKeyEvent(LogicalKeyboardKey.f2);
  await tester.pumpAndSettle();
  await tester.tap(find.text(Strings.telegramMenu));
  await tester.pumpAndSettle();
}

/// La caja de texto del campo con la etiqueta [label] (de `labels`).
Finder _field(String label) => find.descendant(
  of: find.byWidgetPredicate(
    (w) =>
        (w is LabeledField && w.label == label) ||
        (w is SecretField && w.label == label),
  ),
  matching: find.byType(EditableText),
);

Future<void> _type(WidgetTester tester, String label, String text) async {
  await tester.ensureVisible(_field(label));
  await tester.enterText(_field(label), text);
  await tester.pump();
}

Future<void> _tapText(WidgetTester tester, String text) async {
  await tester.ensureVisible(find.text(text).last);
  await tester.tap(find.text(text).last);
}

Future<void> _save(WidgetTester tester) async {
  await tester.tap(find.text(Strings.save));
  await tester.pumpAndSettle();
}

/// El texto de la pregunta, ya en la ventana.
Future<void> _answer(
  WidgetTester tester,
  String question,
  String button,
) async {
  expect(find.text(question), findsOneWidget);
  await tester.tap(find.text(button).last);
  await tester.pumpAndSettle();
}

void main() {
  testWidgets('en Linux: grupos, etiquetas del servidor y el estado al pie', (
    tester,
  ) async {
    var summary = 'Servicio: activo · habilitado';
    final (_, server) = await startApp(
      tester,
      addons: ['telegram'],
      handlers: {
        ...telegramHandlers(invited: ['555123987']),
        'service.status': (_) => serviceStatus(summary: summary),
      },
    );
    await _openTelegram(tester);
    for (final title in [
      Strings.botSection,
      Strings.accessSection,
      Strings.limitsSection,
      Strings.invitedTitle(1),
      'Mensajes por día',
      'Key del modelo',
      '/usr/local/bin/moss-telegram',
      '555123987',
    ]) {
      expect(find.text(title), findsOneWidget, reason: title);
    }
    expect(find.text(Strings.serviceFoot(summary)), findsOneWidget);
    expect(find.text(summary), findsOneWidget);
    expect(find.text(Strings.telegramBarHint), findsOneWidget);
    expect(server.methods.where((m) => m == 'service.status'), hasLength(1));

    // service.changed: se vuelve a pedir el estado, una vez.
    summary = 'Servicio: detenido · habilitado';
    server.notify('service.changed', {'addon': 'telegram'});
    await tester.pumpAndSettle();
    expect(find.text(Strings.serviceFoot(summary)), findsOneWidget);
    expect(server.methods.where((m) => m == 'service.status'), hasLength(2));
  });

  testWidgets('en Windows: solo que el bot funciona en Linux', (tester) async {
    final (_, server) = await startApp(tester);
    await _openTelegram(tester);
    expect(find.text(Strings.telegramLinuxOnly), findsOneWidget);
    expect(find.textContaining('Telegram ·'), findsNothing);
    expect(find.text(Strings.telegramBarHint), findsNothing);
    expect(
      server.methods.where(
        (m) => m.startsWith('addons.') || m.startsWith('service.'),
      ),
      isEmpty,
    );
  });

  testWidgets('sin systemd de usuario: el mensaje en lugar de los botones', (
    tester,
  ) async {
    await startApp(
      tester,
      addons: ['telegram'],
      handlers: telegramHandlers(
        status: serviceStatus(
          systemd: false,
          active: '',
          summary: 'No hay systemd de usuario en esta sesión.',
        ),
      ),
    );
    await _openTelegram(tester);
    expect(
      find.text('No hay systemd de usuario en esta sesión.'),
      findsOneWidget,
    );
    expect(find.text(Strings.restartService), findsNothing);
    expect(find.text(Strings.refreshStatus), findsNothing);
  });

  testWidgets('los errores de check junto al campo; no se guarda', (
    tester,
  ) async {
    final (_, server) = await startApp(
      tester,
      addons: ['telegram'],
      handlers: {
        ...telegramHandlers(),
        'addons.telegram.check': (_) => {
          'field_errors': {
            'daily_limit': 'El tope de mensajes por usuario por día debe ser de 1 a 10000.',
          },
          'form_error':
              'Mensajes por día: El tope de mensajes por usuario por día debe '
              'ser de 1 a 10000.',
          'needs_restart': false,
        },
      },
    );
    await _openTelegram(tester);
    await _type(tester, 'Mensajes por día', '0');
    await _save(tester);
    expect(
      find.text(
        'El tope de mensajes por usuario por día debe ser de 1 a 10000.',
      ),
      findsOneWidget,
    );
    expect(find.text(Strings.reviewMarked), findsOneWidget);
    expect(server.methods, isNot(contains('addons.telegram.save')));
  });

  testWidgets('read_only: el motivo y los campos deshabilitados', (
    tester,
  ) async {
    const reason =
        'La sección es de una versión más nueva (3) de la configuración: '
        'solo lectura.';
    await startApp(
      tester,
      addons: ['telegram'],
      handlers: telegramHandlers(
        get: telegramGet(readOnly: true, readOnlyReason: reason),
      ),
    );
    await _openTelegram(tester);
    expect(find.text(reason), findsOneWidget);
    final box = find.ancestor(
      of: _field('Mensajes por día'),
      matching: find.byType(TextField),
    );
    expect(tester.widget<TextField>(box).enabled, isFalse);
    final generate = tester.widget<SecondaryButton>(
      find.widgetWithText(SecondaryButton, Strings.generateCode),
    );
    expect(generate.onPressed, isNull);
  });

  testWidgets('load_error y env_note', (tester) async {
    await startApp(
      tester,
      addons: ['telegram'],
      handlers: telegramHandlers(
        get: telegramGet(
          readOnly: true,
          loadError: 'No se pudo leer /p/credentials.json.',
          envNote:
              'CHAT_API_KEY está definida en esta terminal, pero el servicio '
              'no la ve.',
        ),
      ),
    );
    await _openTelegram(tester);
    expect(find.text('No se pudo leer /p/credentials.json.'), findsOneWidget);
    expect(find.textContaining('CHAT_API_KEY está definida'), findsOneWidget);
  });

  testWidgets('secretos: tapados, vacío conserva, Quitar y nada en pantalla', (
    tester,
  ) async {
    final (_, server) = await startApp(
      tester,
      addons: ['telegram'],
      handlers: telegramHandlers(),
    );
    await _openTelegram(tester);
    await _type(tester, 'Token', _token);
    await _type(tester, 'Key del modelo', _modelKey);
    expect(tester.widget<EditableText>(_field('Token')).obscureText, isTrue);
    expect(
      tester.widget<EditableText>(_field('Key del modelo')).obscureText,
      isTrue,
    );
    // Quitar la de búsqueda: se usará la de la app.
    await tester.ensureVisible(find.text(Strings.removeKey).last);
    await tester.tap(find.text(Strings.removeKey).last);
    await tester.pump();
    expect(find.text(Strings.keyWillBeRemoved), findsOneWidget);

    await _save(tester);
    final form = server.lastParams('addons.telegram.save')['form'] as Map;
    expect(form['fields']['token'], _token);
    expect(form['fields']['model_key'], _modelKey);
    expect(form['fields']['search_key'], '');
    expect(form['remove_model_key'], isFalse);
    expect(form['remove_search_key'], isTrue);
    for (final e in tester.widgetList<EditableText>(
      find.byType(EditableText),
    )) {
      expect(e.controller.text, isNot(contains('secreto')));
      expect(e.controller.text, isNot(contains('secreta')));
    }
    expect(find.textContaining('secret'), findsNothing);
  });

  testWidgets('vacío conserva: sin escribir un secreto, va vacío', (
    tester,
  ) async {
    final (_, server) = await startApp(
      tester,
      addons: ['telegram'],
      handlers: telegramHandlers(),
    );
    await _openTelegram(tester);
    await _type(tester, 'Mensajes por día', '50');
    await _save(tester);
    final form = server.lastParams('addons.telegram.save')['form'] as Map;
    expect(form['fields']['daily_limit'], '50');
    expect(form['fields']['token'], '');
    expect(form['remove_model_key'], isFalse);
  });

  testWidgets('guardar: llaves descartadas con la pregunta del servidor', (
    tester,
  ) async {
    final (_, server) = await startApp(
      tester,
      addons: ['telegram'],
      handlers: {
        ...telegramHandlers(),
        'addons.telegram.save': (p) => p['discard_confirmed'] == true
            ? {'needs_restart': false}
            : throw const FakeError(-32008, 'confirmar', {
                'message': 'Se descartarán: rara. ¿Guardar? (s/n)',
              }),
      },
    );
    await _openTelegram(tester);
    await _type(tester, 'Mensajes por día', '50');
    await _save(tester);
    await _answer(tester, 'Se descartarán: rara. ¿Guardar?', Strings.yes);
    expect(
      server.lastParams('addons.telegram.save')['discard_confirmed'],
      isTrue,
    );
    expect(server.methods, contains('service.apply'));
  });

  for (final (answer, restart) in [(Strings.yes, true), (Strings.no, false)]) {
    testWidgets('guardar: ¿Reiniciar ahora? $answer', (tester) async {
      final (_, server) = await startApp(
        tester,
        addons: ['telegram'],
        handlers: telegramHandlers(needsRestart: true),
      );
      await _openTelegram(tester);
      await _type(tester, 'Respuestas a la vez', '2');
      await _save(tester);
      await _answer(tester, Strings.restartQuestion, answer);
      expect(server.lastParams('service.apply'), {
        'addon': 'telegram',
        'restart': restart,
        'replace_foreign': false,
      });
      expect(
        find.text('Guardado. Configuración aplicada al bot.'),
        findsOneWidget,
      );
    });
  }

  testWidgets('guardar: ¿Reiniciar ahora? con Esc no toca el servicio', (
    tester,
  ) async {
    final (_, server) = await startApp(
      tester,
      addons: ['telegram'],
      handlers: telegramHandlers(needsRestart: true),
    );
    await _openTelegram(tester);
    await _type(tester, 'Respuestas a la vez', '2');
    await _save(tester);
    expect(find.text(Strings.restartQuestion), findsOneWidget);
    await tester.sendKeyEvent(LogicalKeyboardKey.escape);
    await tester.pumpAndSettle();
    expect(server.methods, isNot(contains('service.apply')));
    expect(find.text(Strings.savedServiceUntouched), findsOneWidget);
    // Sigue en Configuración: el Esc fue de la ventana.
    expect(find.text(Strings.telegramTitle), findsWidgets);
  });

  testWidgets('guardar: unidad ajena, se pregunta y se reemplaza', (
    tester,
  ) async {
    final (_, server) = await startApp(
      tester,
      addons: ['telegram'],
      handlers: {
        ...telegramHandlers(),
        'service.apply': (p) => p['replace_foreign'] == true
            ? {'message': 'Servicio activado.', 'ok': true}
            : throw const FakeError(-32008, 'confirmar', {
                'message': '¿Reemplazar el servicio existente? (s/n)',
              }),
      },
    );
    await _openTelegram(tester);
    await _type(tester, 'Mensajes por día', '50');
    await _save(tester);
    await _answer(tester, '¿Reemplazar el servicio existente?', Strings.yes);
    expect(server.lastParams('service.apply')['replace_foreign'], isTrue);
    expect(find.text('Guardado. Servicio activado.'), findsOneWidget);
  });

  testWidgets('guardar: el addon sin instalar es aviso, no error', (
    tester,
  ) async {
    const notInstalled =
        'No instalado: copia moss-telegram junto a moss o en el PATH.';
    await startApp(
      tester,
      addons: ['telegram'],
      handlers: {
        ...telegramHandlers(),
        'service.apply': (_) => {'message': notInstalled, 'ok': false},
      },
    );
    await _openTelegram(tester);
    await _type(tester, 'Mensajes por día', '50');
    await _save(tester);
    expect(find.text('Guardado. $notInstalled'), findsOneWidget);
    expect(find.text(Strings.reviewMarked), findsNothing);
  });

  testWidgets('guardar con una respuesta del chat en curso', (tester) async {
    final (_, server) = await startApp(
      tester,
      addons: ['telegram'],
      conversation: longChat(phase: 'streaming', inProgress: true),
      disableAnimations: true, // El cursor que parpadea no deja asentarse.
      handlers: telegramHandlers(),
    );
    await _openTelegram(tester);
    await _type(tester, 'Mensajes por día', '50');
    await _save(tester);
    expect(server.methods, contains('addons.telegram.save'));
    expect(server.methods, contains('service.apply'));
  });

  testWidgets('Busy de otra sección: en la barra, sin guardar Telegram', (
    tester,
  ) async {
    final (_, server) = await startApp(
      tester,
      addons: ['telegram'],
      handlers: {
        ...telegramHandlers(),
        'settings.system_prompt.check': (_) => throw const FakeError(
          -32002,
          'Busy',
          {'message': 'Espera a que termine la respuesta.'},
        ),
      },
    );
    await tester.sendKeyEvent(LogicalKeyboardKey.f2);
    await tester.pumpAndSettle();
    await tester.tap(find.text(Strings.modelTitle));
    await tester.pumpAndSettle();
    await tester.tap(find.text(Strings.promptNone));
    await tester.pumpAndSettle();
    await tester.tap(find.text(Strings.telegramMenu));
    await tester.pumpAndSettle();
    await _type(tester, 'Mensajes por día', '50');
    await _save(tester);
    expect(find.text('Espera a que termine la respuesta.'), findsOneWidget);
    expect(server.methods, isNot(contains('addons.telegram.save')));
  });

  testWidgets('Generar código: va al campo, sin guardar', (tester) async {
    final (_, server) = await startApp(
      tester,
      addons: ['telegram'],
      handlers: {
        ...telegramHandlers(),
        'addons.telegram.invite_code': (_) => {
          'invite_code': 'AbC123_-xyz45678',
        },
      },
    );
    await _openTelegram(tester);
    await _tapText(tester, Strings.generateCode);
    await tester.pumpAndSettle();
    expect(
      tester
          .widget<EditableText>(_field('Código de invitación'))
          .controller
          .text,
      'AbC123_-xyz45678',
    );
    expect(server.methods, isNot(contains('addons.telegram.save')));
  });

  testWidgets('Quitar invitado: la pregunta, la acción y su aviso', (
    tester,
  ) async {
    final (_, server) = await startApp(
      tester,
      addons: ['telegram'],
      handlers: {
        ...telegramHandlers(invited: ['555123987']),
        'addons.telegram.invited.remove': (p) => {
          'users': <String>[],
          'message': 'Se quitó ${p['user']}. Configuración aplicada al bot.',
        },
      },
    );
    await _openTelegram(tester);
    await tester.ensureVisible(find.text(Strings.removeInvited).last);
    await tester.tap(find.text(Strings.removeInvited).last);
    await tester.pumpAndSettle();
    expect(
      find.text(Strings.removeInvitedQuestion('555123987')),
      findsOneWidget,
    );
    // El foco empieza en Cancelar: Enter no quita.
    await tester.tap(find.text(Strings.removeInvited).last);
    await tester.pumpAndSettle();
    expect(server.lastParams('addons.telegram.invited.remove'), {
      'user': '555123987',
    });
    expect(
      find.text('Se quitó 555123987. Configuración aplicada al bot.'),
      findsOneWidget,
    );
    expect(find.text(Strings.noInvited), findsOneWidget);
    expect(find.text(Strings.invitedTitle(0)), findsOneWidget);
  });

  testWidgets(r'acción larga: el botón espera y salir manda $/cancel', (
    tester,
  ) async {
    final (app, server) = await startApp(
      tester,
      addons: ['telegram'],
      handlers: {...telegramHandlers(), 'service.restart': (_) => noReply},
    );
    await _openTelegram(tester);
    await tester.tap(find.text(Strings.restartService));
    await tester.pump(const Duration(milliseconds: 100));
    final button = find.widgetWithText(SecondaryButton, Strings.restartService);
    expect(tester.widget<SecondaryButton>(button).waiting, isTrue);
    expect(
      find.descendant(
        of: button,
        matching: find.byType(CircularProgressIndicator),
      ),
      findsNothing,
    );
    await tester.pump(const Duration(milliseconds: 300));
    expect(
      find.descendant(
        of: button,
        matching: find.byType(CircularProgressIndicator),
      ),
      findsOneWidget,
    );
    final id = server.lastId('service.restart');
    await tester.sendKeyEvent(LogicalKeyboardKey.escape);
    // El fundido: el primer cuadro lo arranca, el segundo lo termina y en el
    // tercero Configuración se desmonta.
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 500));
    await tester.pump();
    expect(app.settingsOpen.value, isNull);
    expect(find.byType(SettingsScreen), findsNothing);
    expect(server.lastParams(r'$/cancel'), {'id': id});
    // -32800 una vez; service.changed llega igual y no hay nada que pedir.
    server.respond(id, error: const FakeError(-32800, 'cancelada'));
    server.notify('service.changed', {'addon': 'telegram'});
    await tester.pumpAndSettle();
    expect(server.methods.where((m) => m == 'service.status'), hasLength(1));
  });
}
