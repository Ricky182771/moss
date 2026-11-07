import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:json_rpc_2/json_rpc_2.dart' as rpc;
import 'package:stream_channel/stream_channel.dart';

import 'errors.dart';

/// La versión de `pubspec.yaml` (una prueba vigila que coincidan).
const kClientVersion = '0.1.0';
const kProtocolVersion = 1;

// Tiempos de espera del proceso, no animaciones: no van en motion.dart.

/// `moss-rpc` arrancó pero no respondió `initialize`: se mata.
const kInitializeTimeout = Duration(seconds: 10);

/// PROTOCOL.md §3: `shutdown`, `exit` y `kill` si no sale en 3 s.
const kExitTimeout = Duration(seconds: 3);

/// Dónde se buscó `moss-rpc` y si está.
class RpcLocation {
  final String path;
  final bool found;
  const RpcLocation(this.path, {required this.found});
}

/// `--rpc=<ruta>` o, si no, junto al ejecutable de la app. Nunca en el `PATH`.
RpcLocation locateRpc(
  List<String> args,
  String resolvedExecutable, {
  required bool windows,
  required bool Function(String path) exists,
}) {
  final flag = args.where((a) => a.startsWith('--rpc=')).lastOrNull;
  final String path;
  if (flag != null) {
    path = flag.substring('--rpc='.length);
  } else {
    final sep = windows ? r'\' : '/';
    final dir = resolvedExecutable.substring(
      0,
      resolvedExecutable.lastIndexOf(sep) + 1,
    );
    path = '$dir${windows ? 'moss-rpc.exe' : 'moss-rpc'}';
  }
  return RpcLocation(path, found: exists(path));
}

/// Las últimas ~200 líneas de stderr de `moss-rpc`.
class StderrBuffer {
  static const max = 200;
  final lines = <String>[];

  void add(String line) {
    lines.add(line);
    if (lines.length > max) lines.removeAt(0);
  }
}

/// `moss-rpc` no sirve: no arrancó, no respondió o es de otra versión.
class RpcStartError implements Exception {
  final bool otherVersion;

  /// No respondió `initialize` en [kInitializeTimeout]; ya se mató.
  final bool timedOut;

  /// `data.message` de `StartupFailed`, o la excepción al arrancar.
  final String? detail;
  const RpcStartError({
    this.otherVersion = false,
    this.timedOut = false,
    this.detail,
  });
}

/// JSON-RPC sobre las líneas de un canal. En producción, el de un proceso hijo
/// ([spawn]); en las pruebas, uno en memoria con un proceso falso.
class RpcClient {
  final rpc.Peer _peer;
  final Future<int> _exitCode;
  final void Function() _kill;

  RpcClient(
    StreamChannel<String> channel, {
    required this._exitCode,
    required this._kill,
  }) : _peer = rpc.Peer(channel);

  static Future<RpcClient> spawn(String path, StderrBuffer stderr) async {
    final Process proc;
    try {
      proc = await Process.start(path, const []);
    } on ProcessException catch (e) {
      throw RpcStartError(detail: e.message);
    }
    // Si moss-rpc muere, escribir en el pipe falla; lo avisa `done`.
    proc.stdin.done.ignore();
    proc.stderr
        .transform(utf8.decoder)
        .transform(const LineSplitter())
        .listen(stderr.add);
    final out = StreamController<String>();
    // Siempre '\n', también en Windows.
    out.stream.listen(
      (line) => proc.stdin.add(utf8.encode('$line\n')),
      onDone: () => proc.stdin.close().ignore(),
    );
    final lines = proc.stdout
        .transform(utf8.decoder)
        .transform(const LineSplitter());
    return RpcClient(
      StreamChannel(lines, out.sink),
      exitCode: proc.exitCode,
      kill: proc.kill,
    );
  }

  /// Se completa cuando el canal se cierra (el proceso salió o se cayó).
  Future<void> get done => _peer.done;
  bool get isClosed => _peer.isClosed;

  /// Registra los eventos que conoce y, para los demás, un fallback que los
  /// ignora. Antes de [initialize].
  void onEvents(
    Iterable<String> methods,
    void Function(String method, Map<String, dynamic> params) handler,
  ) {
    for (final m in methods) {
      _peer.registerMethod(
        m,
        (rpc.Parameters p) => handler(m, p.asMap.cast<String, dynamic>()),
      );
    }
    _peer.registerFallback((_) {});
  }

  /// `initialize`; lanza [RpcStartError] si no sirve. Devuelve el resultado.
  Future<Map<String, dynamic>> initialize() async {
    unawaited(_peer.listen());
    try {
      final r =
          await request('initialize', {
            'protocol_version': kProtocolVersion,
            'client': {'name': 'moss-gui', 'version': kClientVersion},
          }).timeout(
            kInitializeTimeout,
            onTimeout: () {
              _kill();
              throw const RpcStartError(timedOut: true);
            },
          );
      if (r['protocol_version'] != kProtocolVersion) {
        throw const RpcStartError(otherVersion: true);
      }
      return r;
    } on RpcError catch (e) {
      if (e.code == RpcError.protocolMismatch) {
        throw const RpcStartError(otherVersion: true);
      }
      throw RpcStartError(detail: e.displayMessage);
    } on StateError {
      throw const RpcStartError(); // Se cerró antes de responder.
    }
  }

  Future<Map<String, dynamic>> request(
    String method, [
    Map<String, dynamic> params = const {},
  ]) async {
    try {
      final r = await _peer.sendRequest(method, params);
      return (r as Map).cast<String, dynamic>();
    } on rpc.RpcException catch (e) {
      throw RpcError(
        code: e.code,
        message: e.message,
        data: (e.data as Map?)?.cast<String, dynamic>(),
      );
    }
  }

  /// `shutdown`, `exit` y `kill` si no sale en 3 s. Nunca queda huérfano.
  Future<void> close() async {
    final killer = Timer(kExitTimeout, _kill);
    try {
      if (!_peer.isClosed) {
        await _peer.sendRequest('shutdown', const {});
        _peer.sendNotification('exit');
      }
    } catch (_) {
      // Ya se cerró o se cayó: solo queda esperar a que salga.
    }
    await _exitCode;
    killer.cancel();
    await _peer.close();
  }
}
