import 'dart:async';

import 'package:flutter/widgets.dart';

import '../rpc/errors.dart';
import 'provider_section.dart' show kCheckDelay;
import 'settings_state.dart';

/// Búsqueda web: el estado y una key nueva, revisada con
/// `settings.search.check` al escribir. Los errores van junto al campo.
class SearchSection extends SettingsSection {
  SearchSection(super.app);

  bool configured = false;
  bool locked = false;

  /// El de `settings.search.get`, enmascarado.
  String keyStatus = '';
  final apiKey = TextEditingController();
  String? error;

  Timer? _checkTimer;
  int _generation = 0;

  @override
  Set<String> get events => const {'search'};

  @override
  bool get dirty => apiKey.text.isNotEmpty;

  @override
  Future<void> load() async {
    final r = await rpc.request('settings.search.get');
    configured = r['configured'] as bool;
    locked = r['locked'] as bool;
    keyStatus = r['key_status'] as String? ?? '';
    discard();
  }

  void edited() {
    _checkTimer?.cancel();
    _generation++;
    error = null;
    notifyListeners();
    // Vacía: se conserva la guardada, no hay nada que revisar.
    if (apiKey.text.isNotEmpty) _checkTimer = Timer(kCheckDelay, _quietCheck);
  }

  /// El `check` mientras se escribe: si falla (no se pudo leer), su mensaje
  /// va donde iría el error.
  Future<void> _quietCheck() async {
    try {
      await check();
    } on RpcError catch (e) {
      if (disposed) return;
      error = e.displayMessage;
      notifyListeners();
    } on StateError {
      // El canal se cerró.
    }
  }

  @override
  Future<bool> check() async {
    _checkTimer?.cancel();
    final generation = ++_generation;
    final r = await rpc.request('settings.search.check', {
      'api_key': apiKey.text,
    });
    final problem = r['error'] as String?;
    if (disposed || generation != _generation) return problem == null;
    error = problem;
    notifyListeners();
    return problem == null;
  }

  @override
  void discard() {
    _checkTimer?.cancel();
    _generation++;
    apiKey.clear();
    error = null;
    notifyListeners();
  }

  @override
  List<SaveStep> get steps => [
    SaveStep(SaveOrder.search, () async {
      await rpc.request('settings.search.save', {'api_key': apiKey.text});
      apiKey.clear();
      notifyListeners();
    }),
  ];

  @override
  bool showSaveError(RpcError e) {
    if (e.code != RpcError.validationFailed) return false;
    error = e.displayMessage;
    notifyListeners();
    return true;
  }

  @override
  void dispose() {
    _checkTimer?.cancel();
    apiKey.clear();
    apiKey.dispose();
    super.dispose();
  }
}
