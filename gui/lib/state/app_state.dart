import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

import '../models/conversation.dart';
import '../models/events.dart';
import '../models/settings.dart';
import '../models/summary.dart';
import '../rpc/client.dart';
import '../rpc/errors.dart';
import '../strings.dart';
import 'apply.dart';
import 'settings_state.dart';

/// Un aviso corto. Cada uno es otro objeto, así el mismo texto se repite.
class Toast {
  final String text;
  Toast(this.text);
}

/// Pide abrir Opciones (`/model` sin modelo), en los modelos de [provider] o,
/// con null, en la lista de proveedores. Cada uno es otro objeto.
class OptionsRequest {
  final String? provider;
  OptionsRequest(this.provider);
}

/// Con la búsqueda activa, el texto va como lo muestra la TUI (PROTOCOL.md
/// §6.3: `text` es lo que se ve y se guarda; `search`, la consulta).
const _searchCommand = '/buscar';

/// Estado de la app sobre `moss-rpc`: la lista de chats y la conversación
/// abierta, copiada de los eventos (PROTOCOL.md §5).
class AppState extends ChangeNotifier {
  RpcClient _rpc;

  /// Arranca otro `moss-rpc` cuando el que había se cayó.
  final Future<RpcClient> Function()? reconnect;

  AppState(this._rpc, {this.reconnect}) {
    _attach();
  }

  ConversationSnapshot? conversation;
  List<ConversationSummary> summaries = const [];

  /// `moss-rpc` se cayó a media sesión.
  bool disconnected = false;

  /// De `app.status`: si falta configurar, el `problem` va en el chat vacío.
  bool ready = true;
  String? problem;

  /// El interruptor "Buscar en la web" de Opciones. Solo de esta sesión de la
  /// GUI: no se guarda.
  final searchOn = ValueNotifier<bool>(false);

  final openOptions = ValueNotifier<OptionsRequest?>(null);

  /// La sección de Configuración abierta; null en el chat.
  final settingsOpen = ValueNotifier<SettingsPage?>(null);

  /// `settings.changed` con su sección, para Configuración.
  final _settingsEvents = StreamController<String>.broadcast(sync: true);
  Stream<String> get settingsEvents => _settingsEvents.stream;

  void openSettings([SettingsPage page = SettingsPage.provider]) =>
      settingsOpen.value = page;

  /// Para Opciones, que tiene su propio estado (`OptionsState`).
  RpcClient get rpc => _rpc;

  /// Error de JSON-RPC que va junto a la caja (Busy, vacío, sin configurar).
  final inputError = ValueNotifier<String?>(null);
  final toast = ValueNotifier<Toast?>(null);

  /// Texto que vuelve a la caja tras un error o una cancelación.
  final restoreText = ValueNotifier<String?>(null);

  /// Eventos que llegan mientras se espera un `conversation.get`.
  List<ConversationEvent>? _pending;

  /// Hay una búsqueda o una respuesta en curso.
  bool get busy => (conversation?.phase ?? Phase.idle) != Phase.idle;

  void _attach() {
    final rpc = _rpc;
    rpc.onEvents(RpcEvent.methods, (m, p) => _onEvent(RpcEvent.parse(m, p)));
    rpc.done.then((_) {
      if (identical(rpc, _rpc) && !_closing) {
        disconnected = true;
        notifyListeners();
      }
    });
  }

  /// Carga la conversación abierta ([handle], de `initialize`) y la lista.
  Future<void> load(String handle) async {
    conversation = ConversationSnapshot.fromJson(
      await _rpc.request('conversation.get', {'conversation': handle}),
    );
    await _refreshList();
    await refreshStatus();
    notifyListeners();
  }

  /// `app.status`: `ready`, `problem` y si hay key de búsqueda.
  Future<void> refreshStatus() async {
    try {
      final r = await _rpc.request('app.status');
      ready = r['ready'] as bool;
      problem = r['problem'] as String?;
      // Sin key ya no se puede buscar: se apaga la pastilla.
      if (r['has_search_key'] == false) searchOn.value = false;
    } on RpcError {
      // Sin estado, el chat vacío no muestra el problema.
    } on StateError {
      // El canal se cerró: `done` ya marcó `disconnected`.
    }
  }

  Future<void> _refreshList() async {
    final r = await _rpc.request('conversations.list');
    summaries = [
      for (final i in r['items'] as List)
        ConversationSummary.fromJson(i as Map<String, dynamic>),
    ];
  }

  // Acciones ----------------------------------------------------------------

  /// Envía lo de la caja. Devuelve si hay que vaciarla.
  Future<bool> submit(String text) async {
    final c = conversation;
    if (c == null) return false;
    inputError.value = null;
    try {
      if (!text.startsWith('/')) {
        final search = searchOn.value;
        await _rpc.request('chat.send', {
          'conversation': c.conversation,
          'text': search ? '$_searchCommand $text' : text,
          if (search) 'search': text,
        });
        return true;
      }
      final r = await _rpc.request('commands.run', {
        'conversation': c.conversation,
        'text': text,
      });
      switch (r['kind']) {
        case 'copy':
          await Clipboard.setData(ClipboardData(text: r['text'] as String));
        case 'status':
          toast.value = Toast(r['message'] as String);
        case 'model_picker':
          openOptions.value = OptionsRequest(r['provider'] as String?);
        // message, search, save, export y model: lo demás llega en eventos.
      }
      return r['clear'] == true;
    } on RpcError catch (e) {
      _showError(e, fromInput: true);
    } on StateError {
      // El canal se cerró: `done` ya marcó `disconnected`.
    }
    return false;
  }

  Future<void> cancel() async {
    final c = conversation;
    if (c == null || !busy) return;
    await _call(
      () => _rpc.request('chat.cancel', {'conversation': c.conversation}),
    );
  }

  Future<void> newChat() => _replace(() => _rpc.request('conversation.new'));

  Future<void> open(String id) {
    if (id == conversation?.id) return Future.value();
    return _replace(() => _rpc.request('conversation.open', {'id': id}));
  }

  /// Exporta la abierta o, con [id], la guardada (menú `⋯`). Aviso corto.
  Future<void> export({String? id}) async {
    final c = conversation;
    if (id == null && c == null) return;
    await _call(() async {
      final r = await _rpc.request(
        'conversation.export',
        id != null ? {'id': id} : {'conversation': c!.conversation},
      );
      toast.value = Toast(r['message'] as String);
    });
  }

  /// `conversation.rename`. Devuelve el error del servidor (va debajo del
  /// campo) o null; el título nuevo llega con `conversations.changed`.
  Future<String?> rename(String id, String title) => _ask(
    () => _rpc.request('conversation.rename', {'id': id, 'title': title}),
  );

  /// `conversation.delete`. Si era la abierta, se queda `replaced_by`.
  /// Devuelve el error (Busy…) para la ventana de confirmación, o null.
  Future<String?> delete(String id) => _ask(() async {
    final r = await _rpc.request('conversation.delete', {'id': id});
    if (r['replaced_by'] case final Map<String, dynamic> s) {
      conversation = ConversationSnapshot.fromJson(s);
      inputError.value = null;
      notifyListeners();
    }
    return r;
  });

  /// `commands.list` para el menú de `/` (con su disponibilidad de ahora).
  /// Si falla, lista vacía: el texto se manda como siempre.
  Future<List<CommandInfo>> commands() async {
    final c = conversation;
    if (c == null) return const [];
    try {
      final r = await _rpc.request('commands.list', {
        'conversation': c.conversation,
      });
      return [
        for (final x in r['commands'] as List)
          CommandInfo.fromJson(x as Map<String, dynamic>),
      ];
    } on Object {
      return const [];
    }
  }

  /// Una petición cuyo error se muestra junto a lo que la causó.
  Future<String?> _ask(Future<Object?> Function() f) async {
    try {
      await f();
      return null;
    } on RpcError catch (e) {
      return e.displayMessage;
    } on StateError {
      return null; // El canal se cerró: `done` ya marcó `disconnected`.
    }
  }

  /// Arranca otro `moss-rpc` y vuelve a abrir la conversación, si tenía id.
  Future<void> retry() async {
    final make = reconnect;
    if (make == null) return;
    final id = conversation?.id;
    RpcClient? rpc;
    try {
      rpc = await make();
      final init = await rpc.initialize();
      _rpc = rpc;
      _attach();
      disconnected = false;
      if (id != null) {
        conversation = ConversationSnapshot.fromJson(
          await _rpc.request('conversation.open', {'id': id}),
        );
        await _refreshList();
        await refreshStatus();
        notifyListeners();
      } else {
        await load(init['conversation'] as String);
      }
    } catch (e) {
      if (!identical(rpc, _rpc)) await rpc?.close(); // Que no quede huérfano.
      toast.value = Toast(switch (e) {
        RpcError() => e.displayMessage,
        RpcStartError(timedOut: true) => Strings.rpcNotResponding,
        RpcStartError(:final detail) => detail ?? Strings.rpcCannotStart,
        _ => Strings.rpcCannotStart,
      });
    }
  }

  bool _closing = false;

  Future<void> close() {
    _closing = true;
    return _rpc.close();
  }

  Future<void> _replace(Future<Map<String, dynamic>> Function() call) =>
      _call(() async {
        conversation = ConversationSnapshot.fromJson(await call());
        inputError.value = null;
        notifyListeners();
      });

  Future<void> _call(Future<void> Function() f) async {
    try {
      await f();
    } on RpcError catch (e) {
      _showError(e);
    } on StateError {
      // El canal se cerró: `done` ya marcó `disconnected`.
    }
  }

  /// Lo escrito en la caja (vacío, sin configurar, sin key) y Busy van junto
  /// a la caja; lo demás (Exportar, abrir), como aviso corto.
  void _showError(RpcError e, {bool fromInput = false}) {
    if (e.code == RpcError.busy || (fromInput && e.belongsToInput)) {
      inputError.value = e.displayMessage;
    } else {
      toast.value = Toast(e.displayMessage);
    }
  }

  // Eventos -----------------------------------------------------------------

  void _onEvent(RpcEvent e) {
    switch (e) {
      case ConversationsChanged():
        _refreshList().then((_) => notifyListeners(), onError: (_) {});
      case SettingsChanged(:final section):
        _settingsEvents.add(section);
        if (section == 'provider' || section == 'search') {
          refreshStatus().then((_) => notifyListeners(), onError: (_) {});
        }
      case ConversationEvent():
        if (e is ChatFinished && e.conversation == conversation?.conversation) {
          if (e.restoreText case final t?) restoreText.value = t;
        }
        _applyOrResync(e);
    }
  }

  void _applyOrResync(ConversationEvent e) {
    final c = conversation;
    if (c == null) return;
    if (_pending case final pending?) {
      pending.add(e);
      return;
    }
    final next = applyEvent(c, e);
    if (next == null) {
      _resync(c.conversation);
    } else if (!identical(next, c)) {
      conversation = next;
      notifyListeners();
    }
  }

  /// Hueco de `rev`: `conversation.get` y luego lo que llegó mientras.
  Future<void> _resync(String handle) async {
    final pending = _pending = [];
    try {
      var s = ConversationSnapshot.fromJson(
        await _rpc.request('conversation.get', {'conversation': handle}),
      );
      for (final e in pending) {
        s = applyEvent(s, e) ?? s;
      }
      if (s.conversation == conversation?.conversation) {
        conversation = s;
        notifyListeners();
      }
    } on Object {
      // Otra conversación la reemplazó o el canal se cerró.
    } finally {
      _pending = null;
    }
  }

  @override
  void dispose() {
    inputError.dispose();
    toast.dispose();
    restoreText.dispose();
    searchOn.dispose();
    openOptions.dispose();
    settingsOpen.dispose();
    _settingsEvents.close();
    super.dispose();
  }
}
