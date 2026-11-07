import 'package:flutter/widgets.dart';

import '../rpc/errors.dart';
import 'settings_state.dart';

/// Búsqueda web: el estado y una key nueva. Sin `check` en el protocolo: los
/// errores llegan al guardar y van junto al campo.
class SearchSection extends SettingsSection {
  SearchSection(super.app);

  bool configured = false;
  bool locked = false;

  /// El de `settings.search.get`, enmascarado.
  String keyStatus = '';
  final apiKey = TextEditingController();
  String? error;

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
    error = null;
    notifyListeners();
  }

  @override
  void discard() {
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
    apiKey.clear();
    apiKey.dispose();
    super.dispose();
  }
}
