import 'dart:async';

import 'package:flutter/widgets.dart';

import '../rpc/errors.dart';
import '../strings.dart';
import '../theme/motion.dart';
import 'settings_state.dart';

/// Un proveedor de la tabla o un endpoint, como lo da `settings.provider.get`.
class ProviderRow {
  final String id;
  final bool endpoint;
  final String name;
  final String baseUrl;
  final bool needsKey;
  final bool keySaved;
  final String note;

  const ProviderRow({
    required this.id,
    required this.endpoint,
    required this.name,
    required this.baseUrl,
    required this.needsKey,
    required this.keySaved,
    required this.note,
  });

  factory ProviderRow.fromJson(Map<String, dynamic> j) => ProviderRow(
    id: j['id'] as String,
    endpoint: j['kind'] == 'endpoint',
    name: j['name'] as String,
    baseUrl: j['base_url'] as String,
    needsKey: j['needs_key'] as bool,
    keySaved: j['key_saved'] as bool,
    note: j['note'] as String? ?? '',
  );
}

/// El borrador de `check`, `save` y `models.list` (PROTOCOL.md §6.7).
@immutable
class ProviderDraft {
  /// Un id de la tabla, `custom:<id>` o `custom:` para uno nuevo.
  final String provider;
  final String name;
  final String baseUrl;
  final String model;
  const ProviderDraft(this.provider, this.name, this.baseUrl, this.model);

  bool get isNew => provider == ProviderSection.newEndpoint;

  @override
  bool operator ==(Object other) =>
      other is ProviderDraft &&
      other.provider == provider &&
      other.name == name &&
      other.baseUrl == baseUrl &&
      other.model == model;

  @override
  int get hashCode => Object.hash(provider, name, baseUrl, model);
}

/// Retraso de los `check` al escribir: uno por pausa, no por tecla. Es un
/// tiempo de espera, no una animación: no va en `motion.dart`.
const kCheckDelay = Duration(milliseconds: 400);

/// Proveedor de IA: la lista, el elegido (nombre, URL, key) y su modelo.
/// Las reglas (validar, la URL de la tabla, la key guardada) son del servidor.
class ProviderSection extends SettingsSection {
  ProviderSection(super.app);

  static const newEndpoint = 'custom:';

  List<ProviderRow> providers = const [];
  ProviderDraft saved = const ProviderDraft('', '', '', '');
  bool baseUrlLocked = false;
  bool modelLocked = false;
  bool keyLocked = false;

  /// El de `settings.provider.get`, enmascarado: solo del proveedor guardado.
  String keyStatus = '';

  // El borrador: la key nunca sale de su campo salvo para mandarla.
  String provider = '';
  final name = TextEditingController();
  final baseUrl = TextEditingController();
  final model = TextEditingController();
  final apiKey = TextEditingController();

  /// De `settings.provider.check`.
  String? error;

  /// De qué campo es [error] (`error_field` de `check`); null: de ninguno.
  String? errorField;
  String? hint;
  bool canRequestModels = false;

  /// De `models.list`: null mientras carga o si falló.
  List<String>? models;
  String? modelsError;
  bool loadingVisible = false;

  /// El proveedor de [models]: al elegir otro se vuelven a pedir.
  String? _modelsFor;
  int? _pendingModels;
  int _generation = 0;
  Timer? _slow;
  Timer? _checkTimer;
  int _checkGeneration = 0;

  ProviderDraft get draft =>
      ProviderDraft(provider, name.text, baseUrl.text, model.text);

  ProviderRow? row(String id) => providers.where((p) => p.id == id).firstOrNull;

  bool get isEndpoint =>
      provider == newEndpoint || provider.startsWith('custom:');

  @override
  Set<String> get events => const {'provider'};

  @override
  bool get dirty => draft != saved || apiKey.text.isNotEmpty;

  @override
  Future<void> load() async {
    final r = await rpc.request('settings.provider.get');
    providers = [
      for (final p in r['providers'] as List)
        ProviderRow.fromJson(p as Map<String, dynamic>),
    ];
    final s = r['saved'] as Map<String, dynamic>;
    final locks = r['locks'] as Map<String, dynamic>;
    baseUrlLocked = locks['base_url'] as bool;
    modelLocked = locks['model'] as bool;
    keyLocked = locks['api_key'] as bool;
    keyStatus = r['key_status'] as String? ?? '';
    final id = s['provider'] as String;
    // Con CHAT_MODEL se ve el modelo que de verdad se usa.
    final effective = (r['effective'] as Map<String, dynamic>)['model'];
    saved = ProviderDraft(
      id,
      row(id)?.name ?? '',
      s['base_url'] as String,
      modelLocked ? effective as String : s['model'] as String,
    );
    _reset();
  }

  /// Otro proveedor: la lista del anterior se cancela y se olvida.
  void _clearModels() {
    if (_pendingModels case final id?) rpc.cancel(id);
    _pendingModels = null;
    _generation++;
    _slow?.cancel();
    models = null;
    modelsError = null;
    _modelsFor = null;
    loadingVisible = false;
    canRequestModels = false;
  }

  void _reset() {
    if (provider != saved.provider) _clearModels();
    provider = saved.provider;
    name.text = saved.name;
    baseUrl.text = saved.baseUrl;
    model.text = saved.model;
    apiKey.clear();
    error = null;
    errorField = null;
    hint = null;
    notifyListeners();
    _check(now: true);
  }

  /// Solo la lista (al borrar un endpoint): el borrador se queda.
  Future<void> refreshList() async {
    final r = await rpc.request('settings.provider.get');
    providers = [
      for (final p in r['providers'] as List)
        ProviderRow.fromJson(p as Map<String, dynamic>),
    ];
    if (provider != newEndpoint && row(provider) == null) _reset();
    notifyListeners();
  }

  @override
  void discard() {
    _reset();
  }

  /// Elegir de la lista. El guardado regresa a lo guardado; otro, a lo suyo
  /// con el modelo vacío (se elige de su lista).
  void select(String id) {
    if (id == provider) return;
    if (id == saved.provider) {
      _reset();
      return;
    }
    final p = row(id);
    _clearModels();
    provider = id;
    name.text = p?.name ?? '';
    baseUrl.text = p?.baseUrl ?? '';
    model.text = '';
    apiKey.clear();
    notifyListeners();
    _check(now: true);
  }

  void addEndpoint() {
    _clearModels();
    provider = newEndpoint;
    name.clear();
    baseUrl.clear();
    model.clear();
    apiKey.clear();
    notifyListeners();
    _check(now: true);
  }

  /// Cualquier campo escrito: el `check` con retraso.
  void edited() {
    notifyListeners();
    _check();
  }

  Map<String, Object> _draftJson() => {
    'provider': provider,
    if (isEndpoint) 'name': name.text,
    'base_url': baseUrl.text,
    'model': model.text,
    if (apiKey.text.isNotEmpty) 'api_key': apiKey.text,
  };

  void _check({bool now = false}) {
    _checkTimer?.cancel();
    if (now) {
      _quietCheck();
    } else {
      _checkTimer = Timer(kCheckDelay, _quietCheck);
    }
  }

  /// El `check` mientras se escribe: si falla (Busy, no se pudo leer), su
  /// mensaje va donde iría el error.
  Future<void> _quietCheck() async {
    try {
      await check();
    } on RpcError catch (e) {
      if (disposed) return;
      error = e.displayMessage;
      errorField = null;
      notifyListeners();
    } on StateError {
      // El canal se cerró.
    }
  }

  @override
  Future<bool> check() async {
    _checkTimer?.cancel();
    final generation = ++_checkGeneration;
    final r = await rpc.request('settings.provider.check', {
      'draft': _draftJson(),
    });
    if (disposed || generation != _checkGeneration) {
      return r['error'] == null;
    }
    error = r['error'] as String?;
    errorField = r['error_field'] as String?;
    hint = r['hint'] as String?;
    canRequestModels = r['can_request_models'] as bool;
    if (canRequestModels && _modelsFor != provider) loadModels();
    notifyListeners();
    return error == null;
  }

  /// `models.list` con el borrador; cancela el anterior.
  void loadModels() {
    if (_pendingModels case final id?) rpc.cancel(id);
    _modelsFor = provider;
    models = null;
    modelsError = null;
    loadingVisible = false;
    final generation = ++_generation;
    _slow?.cancel();
    _slow = Timer(Motion.loadingDelay, () {
      if (generation != _generation) return;
      loadingVisible = true;
      notifyListeners();
    });
    final (id, result) = rpc.start('models.list', {'draft': _draftJson()});
    _pendingModels = id;
    notifyListeners();
    _finishModels(generation, result);
  }

  Future<void> _finishModels(
    int generation,
    Future<Map<String, dynamic>> result,
  ) async {
    List<String>? list;
    String? failure;
    try {
      list = ((await result)['models'] as List).cast<String>();
    } on RpcError catch (e) {
      if (e.code == RpcError.cancelled) return;
      failure = e.displayMessage;
    } on Object {
      failure = Strings.modelsFailed; // Canal cerrado o respuesta rara.
    }
    if (generation != _generation || disposed) return;
    models = list;
    modelsError = failure;
    _pendingModels = null;
    _slow?.cancel();
    loadingVisible = false;
    notifyListeners();
  }

  void pickModel(String m) {
    model.text = m;
    edited();
  }

  @override
  List<SaveStep> get steps => [
    SaveStep(SaveOrder.provider, () async {
      final r = await rpc.request('settings.provider.save', {
        'draft': _draftJson(),
      });
      // Un endpoint nuevo regresa con su id final.
      provider = r['provider'] as String;
      saved = draft;
      apiKey.clear();
      notifyListeners();
    }),
  ];

  @override
  bool showSaveError(RpcError e) {
    if (e.code != RpcError.validationFailed) return false;
    error = e.displayMessage;
    errorField = null;
    notifyListeners();
    return true;
  }

  /// El error si es de [field].
  String? errorOf(String field) => errorField == field ? error : null;

  /// El error que no va junto a un campo a la vista: el de ninguno, o el de
  /// Nombre o URL base con un proveedor de la tabla (no tiene esos campos).
  String? get errorApart =>
      errorField == null ||
          (!isEndpoint && (errorField == 'name' || errorField == 'base_url'))
      ? error
      : null;

  /// `settings.endpoint.delete`: al momento, como en la TUI. Devuelve el
  /// error del servidor (va en la ventana) o null.
  Future<String?> deleteEndpoint(String id) async {
    try {
      await owner!.quietly(() async {
        await rpc.request('settings.endpoint.delete', {'id': id});
        try {
          await refreshList();
        } on RpcError {
          // Ya se borró; `quietly` vuelve a pedir la sección después.
        }
      });
      return null;
    } on RpcError catch (e) {
      return e.displayMessage;
    } on StateError {
      return null;
    }
  }

  @override
  void dispose() {
    _slow?.cancel();
    _checkTimer?.cancel();
    if (_pendingModels case final id?) rpc.cancel(id);
    name.dispose();
    baseUrl.dispose();
    model.dispose();
    apiKey.clear();
    apiKey.dispose();
    super.dispose();
  }
}
