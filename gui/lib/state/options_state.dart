import 'dart:async';

import 'package:flutter/foundation.dart';

import '../models/settings.dart';
import '../rpc/client.dart';
import '../rpc/errors.dart';
import '../theme/motion.dart';
import 'app_state.dart';

/// Estado de la ventana de Opciones mientras está abierta (DISENO.md →
/// Chat → Opciones): el proveedor, sus modelos y la búsqueda web. Lo crea la
/// ventana al abrirse y lo descarta al cerrarse.
class OptionsState extends ChangeNotifier {
  final AppState app;
  OptionsState(this.app);

  RpcClient get _rpc => app.rpc;

  ProviderSettings? settings;

  /// `settings.provider.get` falló (no se pudo leer la configuración).
  String? loadError;

  /// Sin key de búsqueda, el interruptor va deshabilitado con [searchNote].
  bool hasSearchKey = true;
  String searchNote = '';

  /// La nota de `/model` (p. ej. fijado por `CHAT_MODEL`).
  String modelNote = '';

  /// Se ve la lista de proveedores en vez de la de modelos.
  bool pickingProvider = false;

  /// El proveedor cuyos modelos se ven.
  String? shown;

  /// null mientras carga o si falló.
  List<String>? models;

  /// "Cargando modelos…": solo si tarda más de [Motion.loadingDelay].
  bool loadingVisible = false;

  /// `data.message` de `models.list`: se puede escribir el modelo a mano.
  String? modelsError;

  /// El error de `settings.provider.switch` (Busy, sin key…).
  String? switchError;

  int? _pendingId;
  int _generation = 0;
  Timer? _slow;
  bool _disposed = false;

  /// [provider] abre en sus modelos; [providers], en la lista de proveedores
  /// (`/model` sin nada).
  Future<void> load({String? provider, bool providers = false}) async {
    final handle = app.conversation?.conversation;
    try {
      final r = await Future.wait([
        _rpc.request('settings.provider.get'),
        _rpc.request('app.status'),
        if (handle != null)
          _rpc.request('commands.list', {'conversation': handle}),
      ]);
      if (_disposed) return;
      settings = ProviderSettings.fromJson(r[0]);
      hasSearchKey = r[1]['has_search_key'] as bool;
      if (r.length > 2) {
        for (final c in r[2]['commands'] as List) {
          final cmd = CommandInfo.fromJson(c as Map<String, dynamic>);
          if (cmd.name == '/buscar') searchNote = cmd.note;
          if (cmd.name == '/model') modelNote = cmd.note;
        }
      }
    } on RpcError catch (e) {
      if (_disposed) return;
      loadError = e.displayMessage;
      notifyListeners();
      return;
    } on StateError {
      return; // El canal se cerró: `done` ya marcó `disconnected`.
    }
    if (providers) {
      pickingProvider = true;
      notifyListeners();
    } else {
      showModels(provider ?? settings!.provider);
    }
  }

  void showProviders() {
    pickingProvider = true;
    switchError = null;
    notifyListeners();
  }

  /// Pide los modelos de [provider]; cancela la carga anterior.
  void showModels(String provider) {
    pickingProvider = false;
    shown = provider;
    models = null;
    modelsError = null;
    switchError = null;
    loadingVisible = false;
    if (_pendingId case final id?) _rpc.cancel(id);
    final generation = ++_generation;
    _slow?.cancel();
    _slow = Timer(Motion.loadingDelay, () {
      if (generation != _generation) return;
      loadingVisible = true;
      notifyListeners();
    });
    // PROTOCOL.md §6.5 (`model_picker`): el borrador vacío usa la URL de la
    // tabla y la key guardada de ese proveedor.
    final (id, result) = _rpc.start('models.list', {
      'draft': {'provider': provider, 'base_url': '', 'model': ''},
    });
    _pendingId = id;
    notifyListeners();
    result
        .then(
          (r) {
            if (generation == _generation) {
              models = (r['models'] as List).cast<String>();
            }
          },
          onError: (Object e) {
            if (generation != _generation) return;
            if (e is RpcError) modelsError = e.displayMessage;
          },
        )
        .whenComplete(() {
          if (generation != _generation || _disposed) return;
          _pendingId = null;
          _slow?.cancel();
          loadingVisible = false;
          notifyListeners();
        });
  }

  /// `settings.provider.switch` al modelo [model] del proveedor que se ve.
  /// Devuelve si cambió (la ventana se cierra).
  Future<bool> pick(String model) async {
    switchError = null;
    notifyListeners();
    try {
      await _rpc.request('settings.provider.switch', {
        'provider': shown,
        'model': model,
      });
      return true;
    } on RpcError catch (e) {
      if (!_disposed) {
        switchError = e.displayMessage;
        notifyListeners();
      }
    } on StateError {
      // El canal se cerró.
    }
    return false;
  }

  bool isCurrent(String model) =>
      settings != null &&
      shown == settings!.provider &&
      model == settings!.model;

  @override
  void notifyListeners() {
    if (!_disposed) super.notifyListeners();
  }

  @override
  void dispose() {
    _disposed = true;
    _slow?.cancel();
    if (_pendingId case final id?) _rpc.cancel(id);
    super.dispose();
  }
}
