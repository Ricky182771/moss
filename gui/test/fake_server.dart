import 'dart:async';
import 'dart:convert';

import 'package:moss_gui/rpc/client.dart';
import 'package:stream_channel/stream_channel.dart';

/// Un error JSON-RPC que el servidor falso responde.
class FakeError implements Exception {
  final int code;
  final String message;
  final Map<String, Object?>? data;
  const FakeError(this.code, this.message, [this.data]);
}

/// Resultado de un handler para no responder nunca (un `moss-rpc` colgado).
const noReply = Object();

/// `moss-rpc` falso sobre un canal en memoria, con un proceso falso.
class FakeServer {
  final _channel = StreamChannelController<String>();
  final requests = <Map<String, dynamic>>[];
  final _exit = Completer<int>();
  bool killed = false;

  /// Respuestas por método: un resultado, o una función que lo da o lanza
  /// [FakeError].
  final Map<String, Object? Function(Map<String, dynamic> params)> handlers;

  /// Si `exit` hace salir al proceso falso.
  final bool exitsOnExit;

  FakeServer({
    Map<String, Object? Function(Map<String, dynamic>)>? handlers,
    this.exitsOnExit = true,
  }) : handlers = {
         'initialize': (_) => initializeResult(),
         'shutdown': (_) => <String, Object?>{},
         ...?handlers,
       } {
    _channel.foreign.stream.listen((line) {
      final msg = jsonDecode(line) as Map<String, dynamic>;
      requests.add(msg);
      final method = msg['method'] as String;
      if (method == 'exit') {
        if (exitsOnExit) _finish(0);
        return;
      }
      if (!msg.containsKey('id')) return;
      final handler = this.handlers[method];
      try {
        final result = handler == null
            ? throw const FakeError(-32601, 'Method not found')
            : handler((msg['params'] as Map?)?.cast<String, dynamic>() ?? {});
        if (identical(result, noReply)) return;
        _send({'id': msg['id'], 'result': result});
      } on FakeError catch (e) {
        _send({
          'id': msg['id'],
          'error': {
            'code': e.code,
            'message': e.message,
            if (e.data != null) 'data': e.data,
          },
        });
      }
    });
  }

  static Map<String, Object?> initializeResult({int protocol = 1}) => {
    'protocol_version': protocol,
    'server': {'name': 'moss-rpc', 'version': '0.x'},
    'platform': 'linux',
    'portable': false,
    'conversation': 'c1',
    'paths': {'config': '/tmp/c', 'conversations': '/tmp/d', 'downloads': null},
    'ready': true,
    'problem': null,
    'warnings': <String>[],
    'capabilities': {'search': true, 'addons': <String>[], 'service': null},
  };

  void _send(Map<String, Object?> m) =>
      _channel.foreign.sink.add(jsonEncode({'jsonrpc': '2.0', ...m}));

  void _finish(int code) {
    if (_exit.isCompleted) return;
    _exit.complete(code);
    _channel.foreign.sink.close();
  }

  /// La respuesta tardía de una petición a la que su handler dio [noReply]
  /// (una petición larga): [result] o el error [error].
  void respond(Object id, {Object? result, FakeError? error}) => _send({
    'id': id,
    if (error == null)
      'result': result
    else
      'error': {
        'code': error.code,
        'message': error.message,
        if (error.data != null) 'data': error.data,
      },
  });

  /// El `id` de la última petición de [method].
  Object lastId(String method) =>
      requests.lastWhere((r) => r['method'] == method)['id'] as Object;

  /// Los `params` de la última petición de [method].
  Map<String, dynamic> lastParams(String method) =>
      (requests.lastWhere((r) => r['method'] == method)['params'] as Map)
          .cast<String, dynamic>();

  /// Una notificación del servidor (un evento).
  void notify(String method, Map<String, Object?> params) =>
      _send({'method': method, 'params': params});

  /// El proceso se cae.
  void crash() => _finish(1);

  Iterable<String> get methods => requests.map((r) => r['method'] as String);

  RpcClient client() => RpcClient(
    _channel.local,
    exitCode: _exit.future,
    kill: () {
      killed = true;
      _finish(-9);
    },
  );
}

Map<String, Object?> entry(
  String kind, {
  String text = '',
  bool inProgress = false,
  bool incomplete = false,
  bool cancelled = false,
  String note = '',
  String reasoning = '',
  List<Map<String, Object?>> sources = const [],
}) => {
  'kind': kind,
  'text': text,
  'in_progress': inProgress,
  'incomplete': incomplete,
  'cancelled': cancelled,
  'note': note,
  'reasoning': reasoning,
  'sources': sources,
};

Map<String, Object?> snapshot({
  String handle = 'c1',
  int rev = 0,
  String? id,
  String title = '',
  String phase = 'idle',
  List<Map<String, Object?>> entries = const [],
}) => {
  'conversation': handle,
  'rev': rev,
  'id': id,
  'title': title,
  'created_at': '2026-10-25T18:52:00-06:00',
  'last_model': 'modelo',
  'phase': phase,
  'entries': entries,
};

Map<String, Object?> summary(
  String id,
  String title, {
  String group = 'Hoy',
  bool readable = true,
  String error = '',
  bool usedSearch = false,
}) => {
  'id': id,
  'title': title,
  'updated_at': '2026-11-01T10:00:00-06:00',
  'message_count': 2,
  'last_model': 'modelo',
  'readable': readable,
  'error': error,
  'group': group,
  'used_search': usedSearch,
  'open_as': null,
};
