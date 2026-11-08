import 'dart:async';

import 'package:flutter/widgets.dart';

import '../rpc/errors.dart';
import 'provider_section.dart' show kCheckDelay;
import 'settings_state.dart';

enum PromptMode { defaults, none, custom }

/// Un campo numérico de los ajustes: su llave en `params` y en `limits`.
const kNumberFields = [
  'temperature',
  'top_p',
  'top_k',
  'max_tokens',
  'frequency_penalty',
  'presence_penalty',
  'seed',
];

/// Rango de un campo, de `limits` (PROTOCOL.md §6.7): la GUI no los copia.
class Limit {
  final num min;
  final num max;
  final bool minExclusive;
  final bool integer;
  const Limit(
    this.min,
    this.max, {
    this.minExclusive = false,
    this.integer = false,
  });

  factory Limit.fromJson(Map<String, dynamic> j) => Limit(
    j['min'] as num,
    j['max'] as num,
    minExclusive: j['min_exclusive'] as bool? ?? false,
    integer: j['integer'] as bool? ?? false,
  );
}

/// Configuración del modelo: las instrucciones del sistema (para todos) y
/// los ajustes del modelo en uso. Las reglas (rangos, el JSON, los bytes) las
/// da el servidor con sus `check`.
class ModelSection extends SettingsSection {
  ModelSection(super.app);

  // Instrucciones del sistema -------------------------------------------------

  PromptMode savedMode = PromptMode.defaults;
  String savedText = '';
  String defaultText = '';
  int maxBytes = 0;
  PromptMode mode = PromptMode.defaults;
  final prompt = TextEditingController();
  int? bytes;
  String? promptError;
  String? promptWarning;

  // Ajustes del modelo ------------------------------------------------------

  /// `-32004`: sin modelo, el texto del servidor.
  String? noModel;
  String model = '';
  String provider = '';

  /// El nombre del proveedor (de `settings.provider.get`), para el título.
  String providerName = '';
  Map<String, Limit> limits = const {};

  /// Los textos de ayuda de `settings.model_params.get`.
  String reasoningHelp = '';
  String topKNote = '';
  String extraNote = '';

  String? savedReasoning;
  Map<String, String> savedFields = const {};
  String savedExtra = '';
  String? reasoning;
  final fields = {for (final k in kNumberFields) k: TextEditingController()};
  final extra = TextEditingController();
  Map<String, String> fieldErrors = const {};
  String? extraError;

  /// Error que no es de un campo (al guardar).
  String? paramsError;
  String? preview;

  Timer? _promptTimer;
  Timer? _paramsTimer;

  /// El último `check` dijo que lo escrito es igual a lo guardado, por valor.
  bool _sameAsSaved = false;
  int _promptGeneration = 0;
  int _paramsGeneration = 0;

  @override
  Set<String> get events => const {'system_prompt', 'model_params', 'provider'};

  bool get promptDirty =>
      mode != savedMode ||
      (mode == PromptMode.custom && prompt.text != savedText);

  bool get paramsDirty {
    if (noModel != null) return false;
    if (reasoning != savedReasoning || extra.text != savedExtra) return true;
    if (kNumberFields.every((k) => fields[k]!.text == savedFields[k])) {
      return false;
    }
    // Por valor, no por texto: "0.60" es lo mismo que 0.6 guardado, y eso lo
    // dice el servidor (`dirty` de `check`).
    return !_sameAsSaved;
  }

  @override
  bool get dirty => promptDirty || paramsDirty;

  static String _show(Object? v) => switch (v) {
    null => '',
    // 2048.0 de JSON se ve como 2048.
    final double d when d == d.roundToDouble() => '${d.round()}',
    _ => '$v',
  };

  /// Lo que se manda de las instrucciones: el predeterminado se guarda como
  /// "sin la llave" en el servidor (`system_prompt_to_store`).
  String get promptToSend => switch (mode) {
    PromptMode.defaults => defaultText,
    PromptMode.none => '',
    PromptMode.custom => prompt.text,
  };

  @override
  Future<void> load() async {
    final sp = await rpc.request('settings.system_prompt.get');
    savedMode = switch (sp['mode']) {
      'none' => PromptMode.none,
      'custom' => PromptMode.custom,
      _ => PromptMode.defaults,
    };
    savedText = sp['text'] as String? ?? '';
    defaultText = sp['default_text'] as String? ?? '';
    maxBytes = sp['max_bytes'] as int? ?? 0;
    promptWarning = sp['warning'] as String?;
    try {
      final r = await rpc.request('settings.model_params.get');
      noModel = null;
      provider = r['provider'] as String;
      model = r['model'] as String;
      final params = (r['params'] as Map).cast<String, Object?>();
      savedReasoning = params['reasoning'] as String?;
      savedFields = {for (final k in kNumberFields) k: _show(params[k])};
      savedExtra = r['extra'] as String? ?? '';
      limits = {
        for (final e in (r['limits'] as Map).entries)
          e.key as String: Limit.fromJson(e.value as Map<String, dynamic>),
      };
      final help = r['help'] as Map? ?? const {};
      reasoningHelp = help['reasoning'] as String? ?? '';
      topKNote = help['top_k'] as String? ?? '';
      extraNote = help['extra'] as String? ?? '';
      preview = r['preview'] as String?;
      providerName = await _providerName(provider);
    } on RpcError catch (e) {
      if (e.code != RpcError.notConfigured) rethrow;
      noModel = e.displayMessage;
    }
    _resetPrompt();
    _resetParams();
    notifyListeners();
    _schedulePrompt(now: true);
  }

  Future<String> _providerName(String id) async {
    try {
      final r = await rpc.request('settings.provider.get');
      for (final p in r['providers'] as List) {
        if (p['id'] == id) return p['name'] as String;
      }
    } on RpcError {
      // Sin la lista, el id.
    }
    return id;
  }

  void _resetPrompt() {
    mode = savedMode;
    prompt.text = savedMode == PromptMode.custom ? savedText : '';
    promptError = null;
  }

  void _resetParams() {
    reasoning = savedReasoning;
    for (final k in kNumberFields) {
      fields[k]!.text = savedFields[k] ?? '';
    }
    extra.text = savedExtra;
    _sameAsSaved = false;
    fieldErrors = const {};
    extraError = null;
    paramsError = null;
  }

  @override
  void discard() {
    _resetPrompt();
    _resetParams();
    notifyListeners();
    _schedulePrompt(now: true);
  }

  // Instrucciones -------------------------------------------------------------

  void setMode(PromptMode m) {
    mode = m;
    // Personalizadas empieza con el texto predeterminado para editarlo.
    if (m == PromptMode.custom && prompt.text.isEmpty) {
      prompt.text = savedMode == PromptMode.custom ? savedText : defaultText;
    }
    notifyListeners();
    _schedulePrompt(now: true);
  }

  void promptEdited() {
    notifyListeners();
    _schedulePrompt();
  }

  /// Restaurar: las predeterminadas.
  void restorePrompt() => setMode(PromptMode.defaults);

  void _schedulePrompt({bool now = false}) {
    _promptTimer?.cancel();
    if (now) {
      _quiet(_checkPrompt, prompt: true);
    } else {
      _promptTimer = Timer(
        kCheckDelay,
        () => _quiet(_checkPrompt, prompt: true),
      );
    }
  }

  Future<bool> _checkPrompt() async {
    _promptTimer?.cancel();
    final generation = ++_promptGeneration;
    final r = await rpc.request('settings.system_prompt.check', {
      'text': promptToSend,
    });
    final error = r['error'] as String?;
    if (disposed || generation != _promptGeneration) return error == null;
    promptError = error;
    promptWarning = r['warning'] as String?;
    bytes = r['bytes'] as int?;
    notifyListeners();
    return error == null;
  }

  // Ajustes ---------------------------------------------------------------------

  void setReasoning(String? v) {
    reasoning = v;
    paramsEdited(now: true);
  }

  void paramsEdited({bool now = false}) {
    _sameAsSaved = false;
    // Un `check` en camino ya no es de lo que está escrito.
    _paramsGeneration++;
    notifyListeners();
    _paramsTimer?.cancel();
    if (now) {
      _quiet(_checkParams);
    } else {
      _paramsTimer = Timer(kCheckDelay, () => _quiet(_checkParams));
    }
  }

  /// Restaurar todo: todo en Predeterminado y sin JSON.
  void restoreAll() {
    reasoning = null;
    for (final f in fields.values) {
      f.clear();
    }
    extra.clear();
    paramsEdited(now: true);
  }

  /// Lo que se manda: los números tal como se escribieron; leerlos es del
  /// servidor (PROTOCOL.md §6.7).
  Map<String, Object?> _params() => {
    'reasoning': reasoning,
    for (final k in kNumberFields) k: fields[k]!.text,
  };

  Future<bool> _checkParams() async {
    _paramsTimer?.cancel();
    if (noModel != null) return true;
    final generation = ++_paramsGeneration;
    final r = await rpc.request('settings.model_params.check', {
      'params': _params(),
      'extra': extra.text,
    });
    final errors = (r['field_errors'] as Map? ?? const {})
        .cast<String, String>();
    final extraErr = r['extra_error'] as String?;
    if (disposed || generation != _paramsGeneration) {
      return errors.isEmpty && extraErr == null;
    }
    fieldErrors = errors;
    extraError = extraErr;
    paramsError = null;
    preview = r['preview'] as String?;
    _sameAsSaved = r['dirty'] == false;
    notifyListeners();
    return errors.isEmpty && extraErr == null;
  }

  /// Un `check` mientras se escribe: si falla (Busy, no se pudo leer), su
  /// mensaje queda a la vista, en su parte.
  Future<void> _quiet(Future<bool> Function() f, {bool prompt = false}) async {
    try {
      await f();
    } on RpcError catch (e) {
      if (disposed) return;
      if (prompt || noModel != null) {
        promptError = e.displayMessage;
      } else {
        paramsError = e.displayMessage;
      }
      notifyListeners();
    } on StateError {
      // El canal se cerró.
    }
  }

  @override
  Future<bool> check() async {
    final ok = await Future.wait([
      if (promptDirty) _checkPrompt(),
      if (paramsDirty) _checkParams(),
    ]);
    return ok.every((v) => v);
  }

  /// Qué guardado falló, para poner su error en su lugar.
  bool _savingParams = false;

  @override
  List<SaveStep> get steps => [
    if (paramsDirty)
      SaveStep(SaveOrder.modelParams, () async {
        _savingParams = true;
        // Lo que se manda: lo que se escriba mientras responde sigue como
        // cambio sin guardar.
        final params = _params();
        final sent = extra.text;
        await rpc.request('settings.model_params.save', {
          'params': params,
          'extra': sent,
        });
        savedReasoning = params['reasoning'] as String?;
        savedFields = {for (final k in kNumberFields) k: params[k] as String};
        savedExtra = sent;
        notifyListeners();
      }),
    if (promptDirty)
      SaveStep(SaveOrder.systemPrompt, () async {
        _savingParams = false;
        final (sentMode, sentText) = (mode, prompt.text);
        await rpc.request('settings.system_prompt.save', {
          'text': promptToSend,
        });
        savedMode = sentMode;
        savedText = sentText;
        notifyListeners();
      }),
  ];

  @override
  bool showSaveError(RpcError e) {
    if (e.code != RpcError.validationFailed) return false;
    if (_savingParams) {
      final fieldsWrong = (e.data?['field_errors'] as Map? ?? const {})
          .cast<String, String>();
      fieldErrors = fieldsWrong;
      paramsError = fieldsWrong.isEmpty ? e.displayMessage : null;
    } else {
      promptError = e.displayMessage;
    }
    notifyListeners();
    return true;
  }

  @override
  void dispose() {
    _promptTimer?.cancel();
    _paramsTimer?.cancel();
    prompt.dispose();
    extra.dispose();
    for (final f in fields.values) {
      f.dispose();
    }
    super.dispose();
  }
}
