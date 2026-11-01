import 'dart:async';

import 'package:flutter/foundation.dart';

import '../models/conversation.dart';
import '../models/summary.dart';
import '../rpc/moss_rpc.dart';
import '../rpc/errors.dart';

/// Estado de la aplicación según PROTOCOL.md §5
/// Mantiene el snapshot de la conversación sincronizado con los eventos
class AppState extends ChangeNotifier {
  final MossRpc _rpc;
  StreamSubscription<RpcEvent>? _eventSubscription;

  // Estado de la app
  AppStatus? _status;
  ConversationSnapshot? _conversation;
  List<ConversationSummary> _summaries = [];

  // Estado de UI
  bool _isSending = false;
  ChatError? _lastError;

  AppState(this._rpc) {
    _eventSubscription = _rpc.events.listen(_handleEvent);
  }

  // Getters

  AppStatus? get status => _status;
  ConversationSnapshot? get conversation => _conversation;
  List<ConversationSummary> get summaries => List.unmodifiable(_summaries);
  bool get isSending => _isSending;
  ChatError? get lastError => _lastError;

  bool get isReady => _status?.ready ?? false;
  bool get isBusy => _status?.busy ?? false;
  String? get problem => _status?.problem;

  // Inicialización

  Future<void> loadInitialState() async {
    try {
      _status = await _rpc.getStatus();
      _conversation = await _rpc.getConversation(_status!.conversation);
      _summaries = await _rpc.listConversations();
      notifyListeners();
    } catch (e) {
      if (e is RpcError) {
        _lastError = ChatError(
          kind: 'network',
          httpStatus: 0,
          message: e.displayMessage,
        );
      }
      notifyListeners();
      rethrow;
    }
  }

  // Métodos de conversación

  Future<void> newConversation() async {
    try {
      _conversation = await _rpc.newConversation();
      notifyListeners();
    } catch (e) {
      _handleError(e);
      rethrow;
    }
  }

  Future<void> openConversation(String id) async {
    try {
      _conversation = await _rpc.openConversation(id);
      notifyListeners();
    } catch (e) {
      _handleError(e);
      rethrow;
    }
  }

  Future<void> deleteConversation(String id) async {
    try {
      final replaced = await _rpc.deleteConversation(id);
      if (replaced != null) {
        _conversation = replaced;
      }
      // conversations.changed llegará después
      notifyListeners();
    } catch (e) {
      _handleError(e);
      rethrow;
    }
  }

  Future<void> renameConversation(String id, String title) async {
    try {
      await _rpc.renameConversation(id, title);
      // conversation.renamed y conversations.changed llegarán después
    } catch (e) {
      _handleError(e);
      rethrow;
    }
  }

  Future<ExportResult> exportConversation({String? id}) async {
    try {
      return await _rpc.exportConversation(
        handle: id == null ? _conversation?.conversation : null,
        id: id,
      );
    } catch (e) {
      _handleError(e);
      rethrow;
    }
  }

  // Chat

  Future<void> sendMessage(String text, {String? search}) async {
    if (_conversation == null) return;

    _isSending = true;
    _lastError = null;
    notifyListeners();

    try {
      await _rpc.sendMessage(
        handle: _conversation!.conversation,
        text: text,
        search: search,
      );
      // Los eventos entry_added, phase, etc. llegarán después
    } catch (e) {
      _isSending = false;
      _handleError(e);
      rethrow;
    }
  }

  Future<void> cancelMessage() async {
    if (_conversation == null) return;

    try {
      await _rpc.cancelChat(_conversation!.conversation);
      // entry_updated y chat.finished llegarán después
    } catch (e) {
      _handleError(e);
      rethrow;
    }
  }

  // Manejo de eventos según §5 y §7

  void _handleEvent(RpcEvent event) {
    switch (event) {
      case ConversationEntryAddedEvent():
        _handleEntryAdded(event);
      case ConversationEntryDeltaEvent():
        _handleEntryDelta(event);
      case ConversationEntryUpdatedEvent():
        _handleEntryUpdated(event);
      case ConversationPhaseEvent():
        _handlePhase(event);
      case ConversationReasoningDeltaEvent():
        _handleReasoningDelta(event);
      case ConversationSavedEvent():
        _handleSaved(event);
      case ConversationRenamedEvent():
        _handleRenamed(event);
      case ConversationClosedEvent():
        _handleClosed(event);
      case ChatFinishedEvent():
        _handleChatFinished(event);
      case ConversationsChangedEvent():
        _handleConversationsChanged();
      case SettingsChangedEvent():
        _handleSettingsChanged(event);
    }
  }

  void _handleEntryAdded(ConversationEntryAddedEvent event) {
    if (_conversation?.conversation != event.conversation) return;

    // Verificar que no nos saltamos eventos
    if (event.rev != _conversation!.rev + 1) {
      _resyncConversation();
      return;
    }

    _conversation = _conversation!.copyWith(
      rev: event.rev,
      entries: [..._conversation!.entries, event.entry],
    );
    notifyListeners();
  }

  void _handleEntryDelta(ConversationEntryDeltaEvent event) {
    if (_conversation?.conversation != event.conversation) return;

    if (event.rev != _conversation!.rev + 1) {
      _resyncConversation();
      return;
    }

    final entries = List<Entry>.from(_conversation!.entries);
    if (event.index < entries.length) {
      entries[event.index] = entries[event.index].copyWith(
        text: entries[event.index].text + event.delta,
      );
    }

    _conversation = _conversation!.copyWith(rev: event.rev, entries: entries);
    notifyListeners();
  }

  void _handleEntryUpdated(ConversationEntryUpdatedEvent event) {
    if (_conversation?.conversation != event.conversation) return;

    if (event.rev != _conversation!.rev + 1) {
      _resyncConversation();
      return;
    }

    final entries = List<Entry>.from(_conversation!.entries);
    if (event.index < entries.length) {
      entries[event.index] = entries[event.index].copyWith(
        inProgress: event.inProgress ?? entries[event.index].inProgress,
        incomplete: event.incomplete ?? entries[event.index].incomplete,
        cancelled: event.cancelled ?? entries[event.index].cancelled,
        note: event.note ?? entries[event.index].note,
      );
    }

    _conversation = _conversation!.copyWith(rev: event.rev, entries: entries);
    notifyListeners();
  }

  void _handlePhase(ConversationPhaseEvent event) {
    if (_conversation?.conversation != event.conversation) return;

    if (event.rev != _conversation!.rev + 1) {
      _resyncConversation();
      return;
    }

    _conversation = _conversation!.copyWith(rev: event.rev, phase: event.phase);
    notifyListeners();
  }

  void _handleReasoningDelta(ConversationReasoningDeltaEvent event) {
    if (_conversation?.conversation != event.conversation) return;

    if (event.rev != _conversation!.rev + 1) {
      _resyncConversation();
      return;
    }

    final entries = List<Entry>.from(_conversation!.entries);
    if (event.index < entries.length) {
      entries[event.index] = entries[event.index].copyWith(
        reasoning: entries[event.index].reasoning + event.delta,
      );
    }

    _conversation = _conversation!.copyWith(rev: event.rev, entries: entries);
    notifyListeners();
  }

  void _handleSaved(ConversationSavedEvent event) {
    if (_conversation?.conversation != event.conversation) return;

    if (event.rev != _conversation!.rev + 1) {
      _resyncConversation();
      return;
    }

    _conversation = _conversation!.copyWith(
      rev: event.rev,
      id: event.id,
      title: event.title,
    );
    notifyListeners();
  }

  void _handleRenamed(ConversationRenamedEvent event) {
    if (_conversation?.conversation != event.conversation) return;

    if (event.rev != _conversation!.rev + 1) {
      _resyncConversation();
      return;
    }

    _conversation = _conversation!.copyWith(rev: event.rev, title: event.title);
    notifyListeners();
  }

  void _handleClosed(ConversationClosedEvent event) {
    // La conversación fue cerrada (deleted, replaced, shutdown)
    // No hacemos nada aquí: los métodos ya actualizaron el estado
  }

  void _handleChatFinished(ChatFinishedEvent event) {
    if (_conversation?.conversation != event.conversation) return;

    _isSending = false;
    _lastError = event.error;
    notifyListeners();
  }

  void _handleConversationsChanged() {
    // Recargar la lista de conversaciones
    _rpc
        .listConversations()
        .then((summaries) {
          _summaries = summaries;
          notifyListeners();
        })
        .catchError((e) {
          // Error al recargar, no hacemos nada
        });
  }

  void _handleSettingsChanged(SettingsChangedEvent event) {
    // Recargar el estado de la app
    _rpc
        .getStatus()
        .then((status) {
          _status = status;
          notifyListeners();
        })
        .catchError((e) {
          // Error al recargar, no hacemos nada
        });
  }

  // Resincronización cuando nos saltamos un evento

  void _resyncConversation() {
    if (_conversation == null) return;

    _rpc
        .getConversation(_conversation!.conversation)
        .then((snapshot) {
          _conversation = snapshot;
          notifyListeners();
        })
        .catchError((e) {
          // Error al resincronizar
          _handleError(e);
        });
  }

  // Manejo de errores

  void _handleError(Object error) {
    if (error is RpcError) {
      _lastError = ChatError(
        kind: 'network',
        httpStatus: 0,
        message: error.displayMessage,
      );
      notifyListeners();
    }
  }

  void clearError() {
    _lastError = null;
    notifyListeners();
  }

  @override
  void dispose() {
    _eventSubscription?.cancel();
    super.dispose();
  }
}
