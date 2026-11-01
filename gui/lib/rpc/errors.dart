/// Error de JSON-RPC según PROTOCOL.md
class RpcError {
  final int code;
  final String message;
  final Map<String, dynamic>? data;

  RpcError({required this.code, required this.message, this.data});

  factory RpcError.fromJson(Map<String, dynamic> json) {
    return RpcError(
      code: json['code'] as int,
      message: json['message'] as String,
      data: json['data'] as Map<String, dynamic>?,
    );
  }

  /// Mensaje completo para mostrar al usuario
  /// Usa data.message si está presente, sino message
  String get displayMessage {
    if (data != null && data!.containsKey('message')) {
      return data!['message'] as String;
    }
    return message;
  }

  /// Códigos de error estándar JSON-RPC
  static const int parseError = -32700;
  static const int invalidRequest = -32600;
  static const int methodNotFound = -32601;
  static const int invalidParams = -32602;
  static const int internalError = -32603;
  static const int requestCancelled = -32800;

  /// Códigos de error de la aplicación
  static const int notInitialized = -32001;
  static const int busy = -32002;
  static const int unknownConversation = -32003;
  static const int notConfigured = -32004;
  static const int validationFailed = -32005;
  static const int unsupported = -32006;
  static const int storageError = -32007;
  static const int confirmationRequired = -32008;
  static const int protocolMismatch = -32010;
  static const int startupFailed = -32011;

  @override
  String toString() => 'RpcError($code: $message)';
}

/// Error del modelo según PROTOCOL.md
class ChatError {
  final String kind;
  final int httpStatus;
  final String message;
  final int? retryAfterSeconds;

  ChatError({
    required this.kind,
    required this.httpStatus,
    required this.message,
    this.retryAfterSeconds,
  });

  factory ChatError.fromJson(Map<String, dynamic> json) {
    return ChatError(
      kind: json['kind'] as String,
      httpStatus: json['http_status'] as int,
      message: json['message'] as String,
      retryAfterSeconds: json['retry_after_seconds'] as int?,
    );
  }

  /// Texto completo con la etiqueta del tipo (como en la TUI)
  String get fullMessage {
    final label = _labelForKind(kind);
    return '[$label] $message';
  }

  static String _labelForKind(String kind) {
    switch (kind) {
      case 'config':
        return 'error de configuración';
      case 'auth':
        return 'error de autenticación';
      case 'model_not_found':
        return 'modelo no encontrado';
      case 'rate_limited':
        return 'límite de tasa excedido';
      case 'server':
        return 'error del servidor';
      case 'network':
        return 'error de red';
      case 'timeout':
        return 'tiempo de espera agotado';
      case 'bad_response':
        return 'respuesta inválida';
      case 'invalid_request':
        return 'petición inválida';
      case 'cancelled':
        return 'cancelado';
      case 'no_search_results':
        return 'sin resultados de búsqueda';
      default:
        return 'error';
    }
  }

  @override
  String toString() => 'ChatError($kind: $message)';
}
