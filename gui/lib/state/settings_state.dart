import 'dart:async';

import 'package:flutter/foundation.dart';

import '../models/conversation.dart';
import '../rpc/client.dart';
import '../rpc/errors.dart';
import 'app_state.dart';

/// Las secciones de Configuración, en el orden del menú (las maquetas).
enum SettingsPage { provider, colors, model, search }

/// Un guardado de una sección: [order] es su lugar entre todos (el de la F2
/// de la TUI), [run] lo manda y deja el borrador como lo guardado.
class SaveStep {
  final int order;
  final Future<void> Function() run;
  const SaveStep(this.order, this.run);
}

/// Orden de los guardados, el de la F2 de la TUI: los ajustes del modelo van
/// antes que el proveedor porque `model_params.save` aplica al modelo vigente.
abstract final class SaveOrder {
  static const modelParams = 0;
  static const provider = 1;
  static const gui = 2;
  static const systemPrompt = 3;
  static const search = 4;
}

/// Una sección de Configuración con su borrador. Lo guardado lo da el
/// servidor (`*.get`); la sección solo arma el borrador, lo manda y muestra
/// lo que regresa.
abstract class SettingsSection extends ChangeNotifier {
  final AppState app;
  SettingsSection(this.app);

  RpcClient get rpc => app.rpc;

  /// Las secciones de `settings.changed` que la tocan.
  Set<String> get events;

  /// El borrador es distinto de lo guardado.
  bool get dirty;

  /// Cambió afuera mientras tenía cambios: no se pisan, solo se avisa.
  bool changedOutside = false;

  /// El `*.get` falló.
  String? loadError;
  bool loaded = false;

  /// Lee lo guardado y deja el borrador igual.
  Future<void> load();

  /// Regresa el borrador a lo guardado.
  void discard();

  /// Manda el `*.check`, pone los errores junto a su campo y dice si pasa.
  Future<bool> check() async => true;

  /// Los guardados de lo que cambió.
  List<SaveStep> get steps;

  /// Un error de guardar que va junto a un campo (validación). Si no lo
  /// toma, va en la barra de Guardar.
  bool showSaveError(RpcError e) => false;

  /// `settings.changed` de afuera: sin cambios se vuelve a pedir; con
  /// cambios, el aviso.
  void externalChange() {
    if (dirty) {
      changedOutside = true;
      notifyListeners();
    } else {
      reload();
    }
  }

  Future<void> reload() async {
    changedOutside = false;
    await guard(load);
  }

  /// Corre [f]; un error de JSON-RPC queda en [loadError].
  Future<void> guard(Future<void> Function() f) async {
    try {
      await f();
      loadError = null;
      loaded = true;
    } on RpcError catch (e) {
      loadError = e.displayMessage;
    } on StateError {
      return; // El canal se cerró: `done` ya marcó `disconnected`.
    }
    notifyListeners();
  }

  bool _disposed = false;
  bool get disposed => _disposed;

  @override
  void notifyListeners() {
    if (!_disposed) super.notifyListeners();
  }

  @override
  void dispose() {
    _disposed = true;
    super.dispose();
  }
}

/// Estado de la pantalla de Configuración mientras está abierta: la sección
/// que se ve, los borradores de todas y Guardar. Lo crea la pantalla al
/// abrirse y lo descarta al cerrarse.
class SettingsState extends ChangeNotifier {
  final AppState app;
  final Map<SettingsPage, SettingsSection> sections;
  SettingsPage page;

  SettingsState(this.app, this.sections, {this.page = SettingsPage.provider}) {
    for (final s in sections.values) {
      s.addListener(notifyListeners);
      s.reload();
    }
    _events = app.settingsEvents.listen(_onEvent);
  }

  late final StreamSubscription<String> _events;

  bool saving = false;

  /// Busy o un error de escritura: en rojo junto a Guardar.
  String? barError;

  /// Los avisos que dejó el guardado (`Ahora se usa…`).
  final toast = ValueNotifier<Toast?>(null);

  bool get dirty => sections.values.any((s) => s.dirty);

  void show(SettingsPage p) {
    if (page == p) return;
    page = p;
    notifyListeners();
  }

  void _onEvent(String section) {
    // Los de nuestro guardado: al terminar se recargan las que no cambiaron.
    if (saving) return;
    for (final s in sections.values) {
      if (s.events.contains(section)) s.externalChange();
    }
  }

  /// Cancelar: todo regresa a lo guardado.
  void discard() {
    for (final s in sections.values) {
      s.discard();
    }
    barError = null;
    notifyListeners();
  }

  /// Primero los `*.check` de las secciones con cambios: con un error no se
  /// guarda nada y se va a la primera sección que lo tiene. Si pasan, los
  /// guardados en [SaveOrder]; a la primera falla se detiene (lo anterior
  /// queda guardado, como en la F2 de la TUI).
  Future<void> save() async {
    if (saving || !dirty) return;
    saving = true;
    barError = null;
    notifyListeners();
    final handle = app.conversation?.conversation;
    final before = app.conversation?.entries.length ?? 0;
    try {
      final changed = [
        for (final e in sections.entries)
          if (e.value.dirty) e,
      ];
      final ok = await Future.wait([for (final e in changed) e.value.check()]);
      for (final (i, e) in changed.indexed) {
        if (!ok[i]) {
          page = e.key;
          return;
        }
      }
      final steps = [
        for (final e in changed)
          for (final step in e.value.steps) (e.key, step),
      ]..sort((a, b) => a.$2.order.compareTo(b.$2.order));
      for (final (p, step) in steps) {
        try {
          await step.run();
        } on RpcError catch (e) {
          page = p;
          if (!sections[p]!.showSaveError(e)) barError = e.displayMessage;
          return;
        }
      }
      // Los avisos llegan después de la respuesta (PROTOCOL.md §6.7): con la
      // respuesta de `app.status` ya llegaron.
      await app.refreshStatus();
      final c = app.conversation;
      if (c != null && c.conversation == handle && c.entries.length > before) {
        final notices = [
          for (final e in c.entries.skip(before))
            if (e.kind == EntryKind.notice) e.text,
        ];
        if (notices.isNotEmpty) toast.value = Toast(notices.join('\n'));
      }
    } on StateError {
      return; // El canal se cerró.
    } finally {
      saving = false;
      if (!_disposed) {
        for (final s in sections.values) {
          if (!s.dirty) s.reload();
        }
        notifyListeners();
      }
    }
  }

  bool _disposed = false;

  @override
  void notifyListeners() {
    if (!_disposed) super.notifyListeners();
  }

  @override
  void dispose() {
    _disposed = true;
    _events.cancel();
    for (final s in sections.values) {
      s.discard(); // La vista previa de Colores regresa a lo guardado.
      s.dispose();
    }
    toast.dispose();
    super.dispose();
  }
}
