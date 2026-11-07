import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/rpc/client.dart';
import 'package:moss_gui/rpc/errors.dart';

import 'fake_server.dart';

void main() {
  test('initialize manda la versión y el nombre del cliente', () async {
    final server = FakeServer();
    final r = await server.client().initialize();
    expect(r['conversation'], 'c1');
    expect(server.requests.first['params'], {
      'protocol_version': 1,
      'client': {'name': 'moss-gui', 'version': kClientVersion},
    });
  });

  test('otra versión: ProtocolMismatch', () async {
    final server = FakeServer(
      handlers: {
        'initialize': (_) =>
            throw const FakeError(-32010, 'ProtocolMismatch', {'server': 2}),
      },
    );
    expect(
      server.client().initialize(),
      throwsA(isA<RpcStartError>().having((e) => e.otherVersion, 'v', true)),
    );
  });

  test('otra versión en el resultado', () async {
    final server = FakeServer(
      handlers: {'initialize': (_) => FakeServer.initializeResult(protocol: 2)},
    );
    expect(
      server.client().initialize(),
      throwsA(isA<RpcStartError>().having((e) => e.otherVersion, 'v', true)),
    );
  });

  test('StartupFailed: el detalle es data.message', () async {
    final server = FakeServer(
      handlers: {
        'initialize': (_) => throw const FakeError(-32011, 'StartupFailed', {
          'message': 'config.json no es JSON válido',
        }),
      },
    );
    expect(
      server.client().initialize(),
      throwsA(
        isA<RpcStartError>().having(
          (e) => e.detail,
          'detail',
          'config.json no es JSON válido',
        ),
      ),
    );
  });

  test('un evento desconocido se ignora y los demás siguen', () async {
    final server = FakeServer();
    final client = server.client();
    final seen = <String>[];
    client.onEvents(['conversations.changed'], (m, _) => seen.add(m));
    await client.initialize();
    server.notify('algo.nuevo', {'x': 1});
    server.notify('conversations.changed', {});
    await pumpEventQueue();
    expect(seen, ['conversations.changed']);
    expect(client.isClosed, isFalse);
  });

  test('el error muestra data.message', () async {
    final server = FakeServer(
      handlers: {
        'chat.send': (_) => throw const FakeError(-32002, 'Busy', {
          'message': 'Espera la respuesta o cancélala con Esc.',
        }),
      },
    );
    final client = server.client();
    await client.initialize();
    expect(
      client.request('chat.send', {'conversation': 'c1', 'text': 'x'}),
      throwsA(
        isA<RpcError>()
            .having((e) => e.code, 'code', RpcError.busy)
            .having(
              (e) => e.displayMessage,
              'displayMessage',
              'Espera la respuesta o cancélala con Esc.',
            ),
      ),
    );
  });

  test('sin data.message muestra message', () {
    expect(const RpcError(code: -1, message: 'x').displayMessage, 'x');
  });

  testWidgets('initialize sin respuesta: kill a los 10 s', (tester) async {
    final server = FakeServer(handlers: {'initialize': (_) => noReply});
    Object? error;
    server.client().initialize().catchError((Object e) {
      error = e;
      return <String, dynamic>{};
    });
    await tester.pump(const Duration(milliseconds: 9900));
    expect((server.killed, error), (false, null));
    await tester.pump(const Duration(milliseconds: 200));
    expect(server.killed, isTrue);
    expect(error, isA<RpcStartError>().having((e) => e.timedOut, 't', true));
  });

  test('cerrar: shutdown y exit, sin kill si sale', () async {
    final server = FakeServer();
    final client = server.client();
    await client.initialize();
    await client.close();
    expect(server.methods, ['initialize', 'shutdown', 'exit']);
    expect(server.killed, isFalse);
  });

  testWidgets('cerrar: kill a los 3 s si no sale', (tester) async {
    final server = FakeServer(exitsOnExit: false);
    final client = server.client();
    await client.initialize();
    var closed = false;
    client.close().then((_) => closed = true);
    await tester.pump(const Duration(milliseconds: 2900));
    expect(server.methods, ['initialize', 'shutdown', 'exit']);
    expect((server.killed, closed), (false, false));
    await tester.pump(const Duration(milliseconds: 200));
    expect((server.killed, closed), (true, true));
  });
}
