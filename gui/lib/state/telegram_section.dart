import 'dart:async';

import 'package:flutter/widgets.dart';

import '../rpc/errors.dart';
import '../strings.dart';
import '../theme/motion.dart';
import 'app_state.dart';
import 'provider_section.dart' show kCheckDelay;
import 'settings_state.dart';

/// El nombre del addon en el protocolo (`known_addons`).
const kTelegram = 'telegram';

/// Los campos de texto de `form.fields` (PROTOCOL.md §6.8).
const kTelegramFields = [
  'token',
  'model',
  'model_key',
  'search_key',
  'allowed_users',
  'invite_code',
  'daily_limit',
  'concurrent',
  'idle_minutes',
  'stale_minutes',
  'draft_ms',
  'time_zone',
];

/// Los secretos: vacíos se conservan; nunca llegan del servidor.
const kTelegramSecrets = {'token', 'model_key', 'search_key'};

/// Un elemento de `addons.list` o la respuesta de `addons.check_install`.
@immutable
class AddonInstall {
  final bool installed;
  final String? path;

  /// `No instalado: …`, `Incompatible: …` o vacío.
  final String problem;
  const AddonInstall(this.installed, this.path, this.problem);

  factory AddonInstall.fromJson(Map<String, dynamic> j) => AddonInstall(
    j['installed'] as bool,
    j['path'] as String?,
    j['problem'] as String,
  );
}

/// Lo de `service.status` que se muestra. Los textos son del servidor.
@immutable
class ServiceStatus {
  final bool systemd;
  final String active;
  final String summary;
  final String problem;
  final String mismatch;

  /// Sin linger, el aviso y el comando (solo se muestra); si no, null.
  final String? lingerNotice;
  final String? lingerCommand;
  const ServiceStatus({
    required this.systemd,
    required this.active,
    required this.summary,
    required this.problem,
    required this.mismatch,
    this.lingerNotice,
    this.lingerCommand,
  });

  factory ServiceStatus.fromJson(Map<String, dynamic> j) => ServiceStatus(
    systemd: j['systemd'] as bool,
    active: j['active'] as String,
    summary: j['summary'] as String,
    problem: j['problem'] as String,
    mismatch: j['mismatch'] as String,
    lingerNotice: j['linger_notice'] as String?,
    lingerCommand: j['linger_command'] as String?,
  );
}

/// Las acciones largas (PROTOCOL.md §4): su botón espera mientras duran.
abstract final class TelegramAction {
  static const status = 'status';
  static const restart = 'restart';
  static String remove(String user) => 'remove:$user';
}

/// Configuración → Telegram: el formulario de `addons.telegram.*`, el
/// servicio (`service.*`) y los invitados. Las reglas (validar, qué necesita
/// reiniciar, los textos) son del servidor; aquí solo se arma el borrador.
class TelegramSection extends SettingsSection {
  TelegramSection(super.app) {
    if (supported) {
      _service = app.serviceEvents.listen((addon) {
        if (addon == kTelegram) refreshStatus();
      });
    }
  }

  /// `capabilities.addons` lo trae (en Windows, no).
  bool get supported => app.addons.contains(kTelegram);

  StreamSubscription<String>? _service;

  // Lo guardado, de `addons.telegram.get`.
  Map<String, String> _saved = {for (final k in kTelegramFields) k: ''};
  bool _savedEnabled = false;
  bool _savedLogDetails = false;
  Map<String, String> secrets = const {};
  Map<String, String> labels = const {};
  String envNote = '';
  bool readOnly = false;
  String? readOnlyReason;

  /// `load_error`: `config.json` o `credentials.json` no se pudieron leer.
  String? fileError;

  // El borrador. Los secretos nunca salen de su campo salvo para mandarlos.
  final fields = {for (final k in kTelegramFields) k: TextEditingController()};
  bool enabled = false;
  bool logErrorDetails = false;
  bool removeModelKey = false;
  bool removeSearchKey = false;

  /// De `addons.telegram.check` (o del `-32005` de `save`).
  Map<String, String> fieldErrors = const {};
  String? formError;

  AddonInstall? install;
  ServiceStatus? status;

  /// `service.status` falló (sin `HOME`, sin `moss` junto a `moss-rpc`).
  String? statusError;

  /// El error de Reiniciar, junto a los botones.
  String? actionError;

  List<String> invited = const [];
  String? invitedError;

  /// Las acciones en curso (deshabilitadas) y las que tardan más de 300 ms
  /// (con el indicador).
  final waiting = <String>{};
  final slow = <String>{};

  /// Los `id` de las peticiones largas, para `$/cancel` al salir.
  final _pending = <int>{};
  bool _statusAgain = false;
  bool _statusStarted = false;
  Timer? _checkTimer;
  int _checkGeneration = 0;

  String label(String field) => labels[field] ?? field;

  /// El estado de un secreto: el del servidor o, con Quitar, que se quita.
  String secretStatus(String field) => switch (field) {
    'model_key' when removeModelKey => Strings.keyWillBeRemoved,
    'search_key' when removeSearchKey => Strings.keyWillBeRemoved,
    _ => secrets[field] ?? '',
  };

  @override
  Set<String> get events => const {kTelegram};

  @override
  bool get dirty =>
      supported &&
      !readOnly &&
      (enabled != _savedEnabled ||
          logErrorDetails != _savedLogDetails ||
          removeModelKey ||
          removeSearchKey ||
          kTelegramFields.any((k) => fields[k]!.text != _saved[k]));

  Map<String, Object> get _form => {
    'enabled': enabled,
    'log_error_details': logErrorDetails,
    'fields': {for (final k in kTelegramFields) k: fields[k]!.text},
    'remove_model_key': removeModelKey,
    'remove_search_key': removeSearchKey,
  };

  @override
  Future<void> load() async {
    if (!supported) return;
    final r = await rpc.request('addons.telegram.get');
    // Salir de Configuración mientras se esperaba: ya no hay borrador.
    if (disposed) return;
    _apply(r, form: true);
    await _loadInvited();
    await _loadInstall();
    if (disposed) return;
    // Al entrar a Configuración, como F2: `needs_restart` usa el último
    // estado. Después, solo con Actualizar estado y `service.changed`.
    if (!_statusStarted) {
      _statusStarted = true;
      unawaited(refreshStatus());
    }
  }

  void _apply(Map<String, dynamic> r, {required bool form}) {
    secrets = (r['secrets'] as Map).cast<String, String>();
    labels = (r['labels'] as Map).cast<String, String>();
    envNote = r['env_note'] as String;
    readOnly = r['read_only'] as bool;
    readOnlyReason = r['read_only_reason'] as String?;
    fileError = r['load_error'] as String?;
    if (!form) {
      notifyListeners();
      return;
    }
    final f = r['form'] as Map<String, dynamic>;
    _saved = (f['fields'] as Map).cast<String, String>();
    _savedEnabled = f['enabled'] as bool;
    _savedLogDetails = f['log_error_details'] as bool;
    discard();
  }

  Future<void> _loadInvited() async {
    try {
      final r = await rpc.request('addons.telegram.invited.list');
      invited = (r['users'] as List).cast<String>();
      invitedError = null;
    } on RpcError catch (e) {
      invitedError = e.displayMessage;
    }
    notifyListeners();
  }

  Future<void> _loadInstall() async {
    final r = await rpc.request('addons.list');
    for (final a in r['addons'] as List) {
      final j = a as Map<String, dynamic>;
      if (j['name'] == kTelegram) install = AddonInstall.fromJson(j);
    }
    notifyListeners();
  }

  @override
  void discard() {
    _checkTimer?.cancel();
    _checkGeneration++;
    for (final k in kTelegramFields) {
      fields[k]!.text = _saved[k] ?? '';
    }
    enabled = _savedEnabled;
    logErrorDetails = _savedLogDetails;
    removeModelKey = false;
    removeSearchKey = false;
    fieldErrors = const {};
    formError = null;
    notifyListeners();
  }

  /// Cualquier campo escrito: el `check` con retraso. Escribir una key
  /// deshace su Quitar.
  void edited(String field) {
    if (field == 'model_key' && fields[field]!.text.isNotEmpty) {
      removeModelKey = false;
    }
    if (field == 'search_key' && fields[field]!.text.isNotEmpty) {
      removeSearchKey = false;
    }
    notifyListeners();
    _checkTimer?.cancel();
    _checkTimer = Timer(kCheckDelay, _quietCheck);
  }

  void setEnabled(bool v) {
    enabled = v;
    edited('');
  }

  void setLogErrorDetails(bool v) {
    logErrorDetails = v;
    edited('');
  }

  /// Quitar (o no quitar) la key guardada del bot: se usará la de la app.
  void toggleRemove(String field) {
    fields[field]!.clear();
    if (field == 'model_key') removeModelKey = !removeModelKey;
    if (field == 'search_key') removeSearchKey = !removeSearchKey;
    edited('');
  }

  Future<void> _quietCheck() async {
    if (!dirty) {
      fieldErrors = const {};
      formError = null;
      notifyListeners();
      return;
    }
    try {
      await check();
    } on RpcError catch (e) {
      if (disposed) return;
      formError = e.displayMessage;
      notifyListeners();
    } on StateError {
      // El canal se cerró.
    }
  }

  @override
  Future<bool> check() async {
    _checkTimer?.cancel();
    final generation = ++_checkGeneration;
    final r = await rpc.request('addons.telegram.check', {'form': _form});
    final error = r['form_error'] as String?;
    if (disposed || generation != _checkGeneration) return error == null;
    fieldErrors = (r['field_errors'] as Map).cast<String, String>();
    formError = error;
    notifyListeners();
    return error == null;
  }

  @override
  List<SaveStep> get steps => [SaveStep(SaveOrder.telegram, _save)];

  /// `save` y después `service.apply`, el paso obligatorio (PROTOCOL.md
  /// §6.8), con las preguntas de la F2 de la TUI.
  Future<void> _save() async {
    final form = _form;
    Map<String, dynamic> r;
    try {
      r = await rpc.request('addons.telegram.save', {'form': form});
    } on RpcError catch (e) {
      if (e.code != RpcError.confirmationRequired) rethrow;
      // Llaves de la sección que se descartarían.
      if (await _ask(e.displayMessage) != true) {
        _toast(Strings.cancelledNothingSaved);
        return;
      }
      r = await rpc.request('addons.telegram.save', {
        'form': form,
        'discard_confirmed': true,
      });
    }
    if (disposed) return;
    // Lo escrito ya es lo guardado; los secretos salen de sus campos.
    _saved = {
      for (final k in kTelegramFields)
        k: kTelegramSecrets.contains(k) ? '' : fields[k]!.text,
    };
    _savedEnabled = enabled;
    _savedLogDetails = logErrorDetails;
    for (final k in kTelegramSecrets) {
      fields[k]!.clear();
    }
    removeModelKey = false;
    removeSearchKey = false;
    notifyListeners();

    var restart = false;
    if (r['needs_restart'] as bool) {
      final yes = await _ask(Strings.restartQuestion);
      if (yes == null) {
        _toast(Strings.savedServiceUntouched);
        return;
      }
      restart = yes;
    }
    await _applyService(restart: restart);
  }

  Future<void> _applyService({
    required bool restart,
    bool replaceForeign = false,
  }) async {
    try {
      final r = await _long('service.apply', {
        'addon': kTelegram,
        'restart': restart,
        'replace_foreign': replaceForeign,
      });
      // También `{message, ok: false}` con el addon sin instalar: es aviso.
      _toast(Strings.savedWith(r['message'] as String));
    } on RpcError catch (e) {
      switch (e.code) {
        case RpcError.cancelled:
          return;
        case RpcError.unsupported: // Sin systemd de usuario: ya se guardó.
          _toast(Strings.savedWith(e.displayMessage));
        case RpcError.confirmationRequired: // Unidad ajena.
          switch (await _ask(e.displayMessage)) {
            case true:
              await _applyService(restart: restart, replaceForeign: true);
            case false:
              _toast(Strings.savedForeignKept);
            case null:
              _toast(Strings.savedForeignUntouched);
          }
        default:
          rethrow;
      }
    }
  }

  Future<bool?> _ask(String question) async {
    final ask = owner?.ask;
    if (ask == null || disposed) return null;
    return ask(question);
  }

  void _toast(String text) {
    if (!disposed) owner?.toast.value = Toast(text);
  }

  @override
  bool showSaveError(RpcError e) {
    if (e.code != RpcError.validationFailed) return false;
    final errors = e.data?['field_errors'];
    if (errors is! Map) return false; // Solo lectura: va en la barra.
    fieldErrors = errors.cast<String, String>();
    formError = e.displayMessage;
    notifyListeners();
    return true;
  }

  // Acciones al momento ----------------------------------------------------

  /// Generar: el código va al campo, sin guardar.
  Future<void> generateInviteCode() async {
    try {
      final r = await rpc.request('addons.telegram.invite_code');
      if (disposed) return;
      fields['invite_code']!.text = r['invite_code'] as String;
      edited('invite_code');
    } on RpcError catch (e) {
      fieldErrors = {...fieldErrors, 'invite_code': e.displayMessage};
      notifyListeners();
    } on StateError {
      // El canal se cerró.
    }
  }

  /// Una petición larga que se cancela con `$/cancel` al salir.
  Future<Map<String, dynamic>> _long(
    String method,
    Map<String, dynamic> params,
  ) async {
    final (id, result) = rpc.start(method, params);
    _pending.add(id);
    try {
      return await result;
    } finally {
      _pending.remove(id);
    }
  }

  /// [action] con su botón en espera; el indicador, si tarda más de 300 ms.
  Future<void> _wait(String action, Future<void> Function() f) async {
    waiting.add(action);
    notifyListeners();
    final timer = Timer(Motion.loadingDelay, () {
      if (!waiting.contains(action)) return;
      slow.add(action);
      notifyListeners();
    });
    try {
      await f();
    } on StateError {
      // El canal se cerró.
    } finally {
      timer.cancel();
      waiting.remove(action);
      slow.remove(action);
      notifyListeners();
    }
  }

  /// `service.status` (Actualizar estado, al entrar y con `service.changed`).
  /// También corre `--config-version`: la instalación y `read_only` se
  /// vuelven a pedir.
  Future<void> refreshStatus() async {
    if (!supported || disposed) return;
    if (waiting.contains(TelegramAction.status)) {
      _statusAgain = true;
      return;
    }
    await _wait(TelegramAction.status, () async {
      try {
        status = ServiceStatus.fromJson(
          await _long('service.status', {'addon': kTelegram}),
        );
        statusError = null;
      } on RpcError catch (e) {
        if (e.code == RpcError.cancelled) return;
        statusError = e.displayMessage;
        // Sin `service.status` no corrió `--config-version`.
        try {
          install = AddonInstall.fromJson(
            await _long('addons.check_install', {'name': kTelegram}),
          );
        } on RpcError {
          // Se queda lo de `addons.list`.
        }
      }
      if (disposed) return;
      if (statusError == null) {
        try {
          await _loadInstall(); // Ya con `config_version_ok`.
        } on RpcError {
          // Se queda lo de antes.
        }
      }
      try {
        final r = await rpc.request('addons.telegram.get');
        if (!disposed) _apply(r, form: !dirty);
      } on RpcError {
        // Se queda lo que se leyó al entrar.
      }
    });
    if (_statusAgain && !disposed) {
      _statusAgain = false;
      await refreshStatus();
    }
  }

  /// Reiniciar: su mensaje como aviso o, si falló, junto a los botones.
  Future<void> restart() => _wait(TelegramAction.restart, () async {
    actionError = null;
    try {
      final r = await _long('service.restart', {'addon': kTelegram});
      final message = r['message'] as String;
      if (r['ok'] as bool) {
        if (message.isNotEmpty) _toast(message);
      } else {
        actionError = message;
      }
    } on RpcError catch (e) {
      if (e.code != RpcError.cancelled) actionError = e.displayMessage;
    }
  });

  /// Quitar a un invitado: al momento, sin esperar a Guardar.
  Future<void> removeInvited(String user) => _wait(
    TelegramAction.remove(user),
    () async {
      try {
        final r = await _long('addons.telegram.invited.remove', {'user': user});
        invited = (r['users'] as List).cast<String>();
        invitedError = null;
        _toast(r['message'] as String);
      } on RpcError catch (e) {
        if (e.code != RpcError.cancelled) invitedError = e.displayMessage;
      }
    },
  );

  @override
  void dispose() {
    _service?.cancel();
    _checkTimer?.cancel();
    // Salir de Configuración: la respuesta se descarta, la acción sigue.
    for (final id in _pending) {
      rpc.cancel(id);
    }
    for (final k in kTelegramSecrets) {
      fields[k]!.clear();
    }
    for (final c in fields.values) {
      c.dispose();
    }
    super.dispose();
  }
}
