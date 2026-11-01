import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:json_rpc_2/json_rpc_2.dart' as rpc;
import 'package:stream_channel/stream_channel.dart';

import 'errors.dart';

/// Cliente RPC para moss-rpc según PROTOCOL.md
class RpcClient {
  Process? _process;
  rpc.Peer? _peer;
  final String executablePath;
  final List<String> _stderrLines = [];
  static const int _maxStderrLines = 200;

  RpcClient(this.executablePath);

  /// Búfer de stderr (últimas ~200 líneas)
  List<String> get stderrLines => List.unmodifiable(_stderrLines);

  /// Arranca moss-rpc y realiza initialize
  Future<InitializeResult> start({
    required String clientName,
    required String clientVersion,
  }) async {
    // Arrancar el proceso
    try {
      _process = await Process.start(
        executablePath,
        [],
        mode: ProcessStartMode.normal,
      );
    } catch (e) {
      throw RpcError(code: -1, message: 'No se pudo arrancar moss-rpc: $e');
    }

    // Capturar stderr
    _process!.stderr
        .transform(utf8.decoder)
        .transform(const LineSplitter())
        .listen((line) {
          _stderrLines.add(line);
          if (_stderrLines.length > _maxStderrLines) {
            _stderrLines.removeAt(0);
          }
        });

    // Crear el peer sobre stdin/stdout
    final stdoutStream = _process!.stdout
        .transform(utf8.decoder)
        .transform(const LineSplitter());

    final stdinSink = StreamController<String>();
    stdinSink.stream.listen(
      (line) {
        _process!.stdin.add(utf8.encode('$line\n'));
      },
      onError: (error) {
        // Error al escribir en stdin: cerrar el sink
        stdinSink.close();
      },
    );

    final channel = StreamChannel(stdoutStream, stdinSink.sink);

    _peer = rpc.Peer(channel);
    _peer!.listen();

    // initialize
    try {
      final result = await _peer!.sendRequest('initialize', {
        'protocol_version': 1,
        'client': {'name': clientName, 'version': clientVersion},
      });

      return InitializeResult.fromJson(result as Map<String, dynamic>);
    } on rpc.RpcException catch (e) {
      throw RpcError(
        code: e.code,
        message: e.message,
        data: e.data as Map<String, dynamic>?,
      );
    }
  }

  /// Cierra el cliente (shutdown, exit, kill si no responde)
  Future<void> close() async {
    if (_peer == null || _process == null) {
      return;
    }

    try {
      // shutdown
      await _peer!
          .sendRequest('shutdown', {})
          .timeout(const Duration(seconds: 3), onTimeout: () => null);

      // exit (notificación)
      _peer!.sendNotification('exit');

      // Esperar a que termine o matarlo
      await _process!.exitCode.timeout(
        const Duration(seconds: 3),
        onTimeout: () {
          _process!.kill();
          return -1;
        },
      );

      await _peer!.close();
    } catch (e) {
      // Si falla el cierre graceful, matar el proceso
      _process!.kill();
    }

    _peer = null;
    _process = null;
  }

  /// Envía una petición y devuelve el resultado
  Future<dynamic> sendRequest(String method, [dynamic params]) async {
    if (_peer == null) {
      throw RpcError(
        code: RpcError.notInitialized,
        message: 'Cliente no inicializado',
      );
    }

    try {
      return await _peer!.sendRequest(method, params ?? {});
    } on rpc.RpcException catch (e) {
      throw RpcError(
        code: e.code,
        message: e.message,
        data: e.data as Map<String, dynamic>?,
      );
    }
  }

  /// Envía una notificación (sin esperar respuesta)
  void sendNotification(String method, [dynamic params]) {
    if (_peer == null) {
      throw RpcError(
        code: RpcError.notInitialized,
        message: 'Cliente no inicializado',
      );
    }

    _peer!.sendNotification(method, params ?? {});
  }

  /// Registra un manejador de eventos
  void registerMethod(String method, void Function(rpc.Parameters) callback) {
    if (_peer == null) {
      throw RpcError(
        code: RpcError.notInitialized,
        message: 'Cliente no inicializado',
      );
    }

    _peer!.registerMethod(method, callback);
  }

  /// Registra un manejador para eventos desconocidos
  void registerFallback(void Function(rpc.Parameters) callback) {
    if (_peer == null) {
      throw RpcError(
        code: RpcError.notInitialized,
        message: 'Cliente no inicializado',
      );
    }

    _peer!.registerFallback(callback);
  }
}

/// Resultado de initialize según PROTOCOL.md §3
class InitializeResult {
  final int protocolVersion;
  final ServerInfo server;
  final String platform;
  final bool portable;
  final String conversation;
  final Paths paths;
  final bool ready;
  final String? problem;
  final List<String> warnings;
  final Capabilities capabilities;

  InitializeResult({
    required this.protocolVersion,
    required this.server,
    required this.platform,
    required this.portable,
    required this.conversation,
    required this.paths,
    required this.ready,
    this.problem,
    required this.warnings,
    required this.capabilities,
  });

  factory InitializeResult.fromJson(Map<String, dynamic> json) {
    return InitializeResult(
      protocolVersion: json['protocol_version'] as int,
      server: ServerInfo.fromJson(json['server'] as Map<String, dynamic>),
      platform: json['platform'] as String,
      portable: json['portable'] as bool,
      conversation: json['conversation'] as String,
      paths: Paths.fromJson(json['paths'] as Map<String, dynamic>),
      ready: json['ready'] as bool,
      problem: json['problem'] as String?,
      warnings: (json['warnings'] as List<dynamic>).cast<String>(),
      capabilities: Capabilities.fromJson(
        json['capabilities'] as Map<String, dynamic>,
      ),
    );
  }
}

class ServerInfo {
  final String name;
  final String version;

  ServerInfo({required this.name, required this.version});

  factory ServerInfo.fromJson(Map<String, dynamic> json) {
    return ServerInfo(
      name: json['name'] as String,
      version: json['version'] as String,
    );
  }
}

class Paths {
  final String config;
  final String conversations;
  final String? downloads;

  Paths({required this.config, required this.conversations, this.downloads});

  factory Paths.fromJson(Map<String, dynamic> json) {
    return Paths(
      config: json['config'] as String,
      conversations: json['conversations'] as String,
      downloads: json['downloads'] as String?,
    );
  }
}

class Capabilities {
  final bool search;
  final List<String> addons;
  final String? service;

  Capabilities({required this.search, required this.addons, this.service});

  factory Capabilities.fromJson(Map<String, dynamic> json) {
    return Capabilities(
      search: json['search'] as bool,
      addons: (json['addons'] as List<dynamic>).cast<String>(),
      service: json['service'] as String?,
    );
  }
}
