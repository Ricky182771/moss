import 'dart:async';

import 'package:flutter/foundation.dart';

import '../rpc/client.dart';
import '../rpc/errors.dart';
import '../theme/motion.dart';
import 'telegram_section.dart' show kTelegram;

/// Ver registro mientras está abierto: `service.log` (larga; `$/cancel` al
/// cerrar), lo que regresó y si está cargando.
class LogState extends ChangeNotifier {
  final RpcClient rpc;
  LogState(this.rpc);

  List<String> lines = const [];

  /// Los índices de [lines] que el servidor marca como error.
  List<int> errors = const [];
  bool truncated = false;

  /// Sin líneas: el aviso del servidor.
  String notice = '';
  String? error;

  bool loading = false;

  /// Tarda más de 300 ms: "Leyendo el registro…".
  bool slow = false;

  /// Cambia con cada respuesta, para volver abajo.
  int generation = 0;

  int? _pending;
  Timer? _slow;
  bool _disposed = false;

  bool get canCopy => lines.isNotEmpty && !loading;

  Future<void> load() async {
    if (_pending case final id?) rpc.cancel(id);
    loading = true;
    slow = false;
    _slow?.cancel();
    _slow = Timer(Motion.loadingDelay, () {
      if (!loading || _disposed) return;
      slow = true;
      notifyListeners();
    });
    final (id, result) = rpc.start('service.log', {'addon': kTelegram});
    _pending = id;
    notifyListeners();
    try {
      final r = await result;
      if (_disposed || _pending != id) return;
      lines = (r['lines'] as List).cast<String>();
      errors = (r['errors'] as List?)?.cast<int>() ?? const [];
      truncated = r['truncated'] as bool;
      notice = r['notice'] as String;
      error = null;
    } on RpcError catch (e) {
      if (_disposed || _pending != id) return;
      // Cancelada desde fuera: se queda lo que había.
      if (e.code != RpcError.cancelled) {
        lines = const [];
        errors = const [];
        truncated = false;
        error = e.displayMessage;
      }
    } on StateError {
      if (_disposed) return; // El canal se cerró: se queda lo que había.
    }
    _pending = null;
    _slow?.cancel();
    loading = false;
    slow = false;
    generation++;
    notifyListeners();
  }

  @override
  void dispose() {
    _disposed = true;
    _slow?.cancel();
    if (_pending case final id?) rpc.cancel(id);
    super.dispose();
  }
}
