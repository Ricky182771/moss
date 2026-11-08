import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/rpc/errors.dart';
import 'package:moss_gui/state/app_state.dart';
import 'package:moss_gui/state/settings_state.dart';
import 'package:moss_gui/state/telegram_section.dart';
import 'package:moss_gui/strings.dart';

import 'fake_server.dart';
import 'harness.dart';

/// La sección Telegram sin pantalla, contra un `moss-rpc` falso: cada método
/// de `addons.*` y `service.*` con su error.
Future<(TelegramSection, SettingsState, FakeServer)> _start(
  Map<String, Object? Function(Map<String, dynamic>)> handlers, {
  List<bool?> answers = const [],
}) async {
  final server = FakeServer(handlers: {...telegramHandlers(), ...handlers});
  final client = server.client();
  await client.initialize();
  final app = AppState(client, addons: const ['telegram']);
  final section = TelegramSection(app);
  final state = SettingsState(app, {SettingsPage.telegram: section});
  final pending = [...answers];
  final asked = <String>[];
  state.ask = (q) async {
    asked.add(q);
    return pending.removeAt(0);
  };
  addTearDown(() {
    if (!_closed.contains(state)) state.dispose();
    app.dispose();
  });
  await _settle();
  return (section, state, server);
}

/// Las que la prueba ya cerró (salir de Configuración).
final _closed = <SettingsState>{};

/// Las respuestas del servidor falso llegan en microtareas.
Future<void> _settle() =>
    Future<void>.delayed(const Duration(milliseconds: 20));

FakeError _error(int code, String message, [Map<String, Object?>? data]) =>
    FakeError(code, 'error', {'message': message, ...?data});

void main() {
  test('get, invitados, instalación y estado al entrar', () async {
    final (t, _, server) = await _start({});
    expect(t.loaded, isTrue);
    expect(t.label('daily_limit'), 'Mensajes por día');
    expect(t.fields['daily_limit']!.text, '34');
    expect(t.install?.path, '/usr/local/bin/moss-telegram');
    expect(t.status?.summary, 'Servicio: activo · habilitado');
    expect(t.dirty, isFalse);
    expect(server.methods, contains('service.status'));
  });

  test('addons.telegram.get falla: el error de la sección', () async {
    final (t, _, _) = await _start({
      'addons.telegram.get': (_) =>
          throw _error(-32007, 'No se pudo leer /x/config.json.'),
    });
    expect(t.loadError, 'No se pudo leer /x/config.json.');
  });

  test('invited.list falla: el error en Invitados', () async {
    final (t, _, _) = await _start({
      'addons.telegram.invited.list': (_) =>
          throw _error(-32007, 'invitados.json está dañado.'),
    });
    expect(t.invitedError, 'invitados.json está dañado.');
  });

  test('service.status falla: su mensaje y addons.check_install', () async {
    final (t, _, server) = await _start({
      'service.status': (_) => throw _error(
        -32007,
        'El servicio arranca moss, y no está junto a moss-rpc: /x/moss.',
      ),
      'addons.check_install': (_) =>
          addonInstall(problem: 'Incompatible: versión 2 y 1.'),
    });
    expect(t.statusError, contains('no está junto a moss-rpc'));
    expect(server.methods, contains('addons.check_install'));
    expect(t.install?.problem, 'Incompatible: versión 2 y 1.');
  });

  test('Reiniciar: el mensaje, ok: false y -32005', () async {
    var reply = <String, Object?>{'message': 'Bot reiniciado.', 'ok': true};
    FakeError? error;
    final (t, state, _) = await _start({
      'service.restart': (_) => error == null ? reply : throw error,
    });
    await t.restart();
    expect(state.toast.value?.text, 'Bot reiniciado.');
    expect(t.actionError, isNull);
    reply = {'message': 'No se pudo reiniciar el bot: x', 'ok': false};
    await t.restart();
    expect(t.actionError, 'No se pudo reiniciar el bot: x');
    error = _error(-32005, 'Activa el addon y guarda para arrancarlo.');
    await t.restart();
    expect(t.actionError, 'Activa el addon y guarda para arrancarlo.');
  });

  test(
    'invite_code: el código va al campo; su error, junto al campo',
    () async {
      FakeError? error;
      final (t, _, server) = await _start({
        'addons.telegram.invite_code': (_) =>
            error == null ? {'invite_code': 'AbC123_-xyz45678'} : throw error,
      });
      await t.generateInviteCode();
      expect(t.fields['invite_code']!.text, 'AbC123_-xyz45678');
      expect(t.dirty, isTrue);
      expect(server.methods, isNot(contains('addons.telegram.save')));
      error = _error(-32603, 'No se pudo generar el código: sin azar.');
      await t.generateInviteCode();
      expect(t.fieldErrors['invite_code'], contains('No se pudo generar'));
    },
  );

  test('invited.remove: la lista que queda y su error', () async {
    FakeError? error;
    final (t, state, server) = await _start({
      'addons.telegram.invited.list': (_) => {
        'users': ['555'],
      },
      'addons.telegram.invited.remove': (p) => error == null
          ? {'users': <String>[], 'message': 'Se quitó ${p['user']}.'}
          : throw error,
    });
    await t.removeInvited('555');
    expect(server.lastParams('addons.telegram.invited.remove'), {
      'user': '555',
    });
    expect(t.invited, isEmpty);
    expect(state.toast.value?.text, 'Se quitó 555.');
    error = _error(-32007, 'No se pudo escribir invitados.json.');
    await t.removeInvited('555');
    expect(t.invitedError, 'No se pudo escribir invitados.json.');
  });

  test(
    'save con -32008: se pregunta y se repite con discard_confirmed',
    () async {
      final (t, state, server) = await _start(
        {
          'addons.telegram.save': (p) => p['discard_confirmed'] == true
              ? {'needs_restart': false}
              : throw _error(-32008, 'Se descartarán: rara. ¿Guardar? (s/n)'),
        },
        answers: [true],
      );
      t.fields['daily_limit']!.text = '50';
      await state.save();
      final saves = server.requests.where(
        (r) => r['method'] == 'addons.telegram.save',
      );
      expect(saves, hasLength(2));
      expect(saves.last['params']['discard_confirmed'], isTrue);
      expect(server.lastParams('service.apply'), {
        'addon': 'telegram',
        'restart': false,
        'replace_foreign': false,
      });
      expect(t.dirty, isFalse);
    },
  );

  test('save con -32008 y No: no se guarda ni se aplica', () async {
    final (t, state, server) = await _start(
      {
        'addons.telegram.save': (p) =>
            throw _error(-32008, 'Se descartarán: rara. ¿Guardar? (s/n)'),
      },
      answers: [false],
    );
    t.fields['daily_limit']!.text = '50';
    await state.save();
    expect(server.methods, isNot(contains('service.apply')));
    expect(state.toast.value?.text, Strings.cancelledNothingSaved);
    expect(t.dirty, isTrue);
  });

  test('save con -32005: los errores junto a cada campo', () async {
    final (t, state, _) = await _start({
      'addons.telegram.save': (_) =>
          throw _error(-32005, 'Mensajes por día: de 1 a 10000.', {
            'field_errors': {'daily_limit': 'De 1 a 10000.'},
          }),
    });
    t.fields['daily_limit']!.text = '0';
    await state.save();
    expect(t.fieldErrors, {'daily_limit': 'De 1 a 10000.'});
    expect(state.barError, isNull);
  });

  test(
    'apply con -32008: unidad ajena, Sí repite con replace_foreign',
    () async {
      final (t, state, server) = await _start(
        {
          'service.apply': (p) => p['replace_foreign'] == true
              ? {'message': 'Servicio activado.', 'ok': true}
              : throw _error(
                  -32008,
                  '¿Reemplazar el servicio existente? (s/n)',
                ),
        },
        answers: [true],
      );
      t.fields['daily_limit']!.text = '50';
      await state.save();
      expect(server.lastParams('service.apply')['replace_foreign'], isTrue);
      expect(state.toast.value?.text, 'Guardado. Servicio activado.');
    },
  );

  test('apply: -32006 es aviso; -32007 va en la barra', () async {
    FakeError error = _error(
      -32006,
      'No hay systemd de usuario en esta sesión.',
    );
    final (t, state, _) = await _start({'service.apply': (_) => throw error});
    t.fields['daily_limit']!.text = '50';
    await state.save();
    expect(
      state.toast.value?.text,
      'Guardado. No hay systemd de usuario en esta sesión.',
    );
    expect(state.barError, isNull);
    await _settle(); // La recarga después de guardar.
    error = _error(
      -32007,
      'El servicio arranca moss, y no está junto a moss-rpc.',
    );
    t.fields['daily_limit']!.text = '51';
    await state.save();
    expect(state.barError, contains('no está junto a moss-rpc'));
  });

  test(
    r'$/cancel al salir: una larga en curso, sin segunda respuesta',
    () async {
      final (t, state, server) = await _start({
        'service.restart': (_) => noReply,
      });
      final done = t.restart();
      await _settle();
      expect(t.waiting, contains(TelegramAction.restart));
      final id = server.lastId('service.restart');
      state.dispose(); // Salir de Configuración.
      _closed.add(state);
      await _settle();
      expect(server.lastParams(r'$/cancel'), {'id': id});
      // El servidor responde -32800 una vez (PROTOCOL.md §4) y nada más.
      server.respond(
        id,
        error: const FakeError(RpcError.cancelled, 'cancelada'),
      );
      await done;
      expect(t.actionError, isNull);
    },
  );
}
