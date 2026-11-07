/// Error de JSON-RPC (PROTOCOL.md §4). Los errores del modelo no llegan aquí:
/// llegan como entrada `error` en el historial.
class RpcError implements Exception {
  final int code;
  final String message;
  final Map<String, dynamic>? data;

  const RpcError({required this.code, required this.message, this.data});

  /// Lo que se muestra: `data.message` cuando viene.
  String get displayMessage => switch (data?['message']) {
    final String m => m,
    _ => message,
  };

  static const int busy = -32002;
  static const int notConfigured = -32004;
  static const int validationFailed = -32005;
  static const int protocolMismatch = -32010;

  /// Errores que van junto a la caja del mensaje: Busy, sin configurar y
  /// validación (mensaje vacío, falta la key de búsqueda).
  bool get belongsToInput =>
      code == busy || code == notConfigured || code == validationFailed;

  @override
  String toString() => 'RpcError($code, $message)';
}
