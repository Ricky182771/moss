import 'dart:async';

import 'client.dart';
import 'errors.dart';
import '../models/conversation.dart';
import '../models/summary.dart';

/// API de alto nivel sobre RpcClient según PROTOCOL.md
class MossRpc {
  final RpcClient _client;
  final StreamController<RpcEvent> _events = StreamController.broadcast();

  MossRpc(this._client) {
    _setupEventHandlers();
  }

  /// Stream de eventos del servidor
  Stream<RpcEvent> get events => _events.stream;

  void _setupEventHandlers() {
    // Registrar manejadores para todos los eventos según §7
    _client.registerMethod('conversation.entry_added', (params) {
      _events.add(
        ConversationEntryAddedEvent.fromJson(
          params.asMap.cast<String, dynamic>(),
        ),
      );
    });

    _client.registerMethod('conversation.entry_delta', (params) {
      _events.add(
        ConversationEntryDeltaEvent.fromJson(
          params.asMap.cast<String, dynamic>(),
        ),
      );
    });

    _client.registerMethod('conversation.entry_updated', (params) {
      _events.add(
        ConversationEntryUpdatedEvent.fromJson(
          params.asMap.cast<String, dynamic>(),
        ),
      );
    });

    _client.registerMethod('conversation.phase', (params) {
      _events.add(
        ConversationPhaseEvent.fromJson(params.asMap.cast<String, dynamic>()),
      );
    });

    _client.registerMethod('conversation.reasoning_delta', (params) {
      _events.add(
        ConversationReasoningDeltaEvent.fromJson(
          params.asMap.cast<String, dynamic>(),
        ),
      );
    });

    _client.registerMethod('conversation.saved', (params) {
      _events.add(
        ConversationSavedEvent.fromJson(params.asMap.cast<String, dynamic>()),
      );
    });

    _client.registerMethod('conversation.renamed', (params) {
      _events.add(
        ConversationRenamedEvent.fromJson(params.asMap.cast<String, dynamic>()),
      );
    });

    _client.registerMethod('conversation.closed', (params) {
      _events.add(
        ConversationClosedEvent.fromJson(params.asMap.cast<String, dynamic>()),
      );
    });

    _client.registerMethod('chat.finished', (params) {
      _events.add(
        ChatFinishedEvent.fromJson(params.asMap.cast<String, dynamic>()),
      );
    });

    _client.registerMethod('conversations.changed', (params) {
      _events.add(ConversationsChangedEvent());
    });

    _client.registerMethod('settings.changed', (params) {
      _events.add(
        SettingsChangedEvent.fromJson(params.asMap.cast<String, dynamic>()),
      );
    });
  }

  // § 6.1 Aplicación

  Future<AppStatus> getStatus() async {
    final result = await _client.sendRequest('app.status', {});
    return AppStatus.fromJson(result as Map<String, dynamic>);
  }

  // § 6.2 Lista de conversaciones

  Future<List<ConversationSummary>> listConversations() async {
    final result = await _client.sendRequest('conversations.list', {});
    final items = (result as Map<String, dynamic>)['items'] as List<dynamic>;
    return items
        .map(
          (item) => ConversationSummary.fromJson(item as Map<String, dynamic>),
        )
        .toList();
  }

  Future<ConversationSnapshot?> deleteConversation(String id) async {
    final result = await _client.sendRequest('conversation.delete', {'id': id});
    final replacedBy = (result as Map<String, dynamic>)['replaced_by'];
    if (replacedBy == null) return null;
    return ConversationSnapshot.fromJson(replacedBy as Map<String, dynamic>);
  }

  Future<String> renameConversation(String id, String title) async {
    final result = await _client.sendRequest('conversation.rename', {
      'id': id,
      'title': title,
    });
    return (result as Map<String, dynamic>)['title'] as String;
  }

  // § 6.3 Conversación abierta

  Future<ConversationSnapshot> newConversation() async {
    final result = await _client.sendRequest('conversation.new', {});
    return ConversationSnapshot.fromJson(result as Map<String, dynamic>);
  }

  Future<ConversationSnapshot> openConversation(String id) async {
    final result = await _client.sendRequest('conversation.open', {'id': id});
    return ConversationSnapshot.fromJson(result as Map<String, dynamic>);
  }

  Future<ConversationSnapshot> getConversation(String handle) async {
    final result = await _client.sendRequest('conversation.get', {
      'conversation': handle,
    });
    return ConversationSnapshot.fromJson(result as Map<String, dynamic>);
  }

  Future<ExportResult> exportConversation({String? handle, String? id}) async {
    if ((handle == null && id == null) || (handle != null && id != null)) {
      throw ArgumentError('Exactly one of handle or id must be provided');
    }

    final params = handle != null ? {'conversation': handle} : {'id': id!};

    final result = await _client.sendRequest('conversation.export', params);
    return ExportResult.fromJson(result as Map<String, dynamic>);
  }

  // § 6.4 Chat

  Future<void> sendMessage({
    required String handle,
    required String text,
    String? search,
  }) async {
    final params = {
      'conversation': handle,
      'text': text,
      if (search != null) 'search': search,
    };

    await _client.sendRequest('chat.send', params);
  }

  Future<void> cancelChat(String handle) async {
    await _client.sendRequest('chat.cancel', {'conversation': handle});
  }

  void close() {
    _events.close();
  }
}

// Modelos de respuesta

class AppStatus {
  final bool ready;
  final String? problem;
  final bool busy;
  final String conversation;
  final List<String> envLocks;
  final bool hasSearchKey;

  AppStatus({
    required this.ready,
    this.problem,
    required this.busy,
    required this.conversation,
    required this.envLocks,
    required this.hasSearchKey,
  });

  factory AppStatus.fromJson(Map<String, dynamic> json) {
    return AppStatus(
      ready: json['ready'] as bool,
      problem: json['problem'] as String?,
      busy: json['busy'] as bool,
      conversation: json['conversation'] as String,
      envLocks: (json['env_locks'] as List<dynamic>).cast<String>(),
      hasSearchKey: json['has_search_key'] as bool,
    );
  }
}

class ExportResult {
  final String path;
  final String message;

  ExportResult({required this.path, required this.message});

  factory ExportResult.fromJson(Map<String, dynamic> json) {
    return ExportResult(
      path: json['path'] as String,
      message: json['message'] as String,
    );
  }
}

// Eventos según §7

sealed class RpcEvent {}

class ConversationEntryAddedEvent extends RpcEvent {
  final String conversation;
  final int rev;
  final int index;
  final Entry entry;

  ConversationEntryAddedEvent({
    required this.conversation,
    required this.rev,
    required this.index,
    required this.entry,
  });

  factory ConversationEntryAddedEvent.fromJson(Map<String, dynamic> json) {
    return ConversationEntryAddedEvent(
      conversation: json['conversation'] as String,
      rev: json['rev'] as int,
      index: json['index'] as int,
      entry: Entry.fromJson(json['entry'] as Map<String, dynamic>),
    );
  }
}

class ConversationEntryDeltaEvent extends RpcEvent {
  final String conversation;
  final int rev;
  final int index;
  final String delta;

  ConversationEntryDeltaEvent({
    required this.conversation,
    required this.rev,
    required this.index,
    required this.delta,
  });

  factory ConversationEntryDeltaEvent.fromJson(Map<String, dynamic> json) {
    return ConversationEntryDeltaEvent(
      conversation: json['conversation'] as String,
      rev: json['rev'] as int,
      index: json['index'] as int,
      delta: json['delta'] as String,
    );
  }
}

class ConversationEntryUpdatedEvent extends RpcEvent {
  final String conversation;
  final int rev;
  final int index;
  final bool? inProgress;
  final bool? incomplete;
  final bool? cancelled;
  final String? note;

  ConversationEntryUpdatedEvent({
    required this.conversation,
    required this.rev,
    required this.index,
    this.inProgress,
    this.incomplete,
    this.cancelled,
    this.note,
  });

  factory ConversationEntryUpdatedEvent.fromJson(Map<String, dynamic> json) {
    return ConversationEntryUpdatedEvent(
      conversation: json['conversation'] as String,
      rev: json['rev'] as int,
      index: json['index'] as int,
      inProgress: json['in_progress'] as bool?,
      incomplete: json['incomplete'] as bool?,
      cancelled: json['cancelled'] as bool?,
      note: json['note'] as String?,
    );
  }
}

class ConversationPhaseEvent extends RpcEvent {
  final String conversation;
  final int rev;
  final Phase phase;

  ConversationPhaseEvent({
    required this.conversation,
    required this.rev,
    required this.phase,
  });

  factory ConversationPhaseEvent.fromJson(Map<String, dynamic> json) {
    return ConversationPhaseEvent(
      conversation: json['conversation'] as String,
      rev: json['rev'] as int,
      phase: Phase.fromString(json['phase'] as String),
    );
  }
}

class ConversationReasoningDeltaEvent extends RpcEvent {
  final String conversation;
  final int rev;
  final int index;
  final String delta;

  ConversationReasoningDeltaEvent({
    required this.conversation,
    required this.rev,
    required this.index,
    required this.delta,
  });

  factory ConversationReasoningDeltaEvent.fromJson(Map<String, dynamic> json) {
    return ConversationReasoningDeltaEvent(
      conversation: json['conversation'] as String,
      rev: json['rev'] as int,
      index: json['index'] as int,
      delta: json['delta'] as String,
    );
  }
}

class ConversationSavedEvent extends RpcEvent {
  final String conversation;
  final int rev;
  final String id;
  final String title;

  ConversationSavedEvent({
    required this.conversation,
    required this.rev,
    required this.id,
    required this.title,
  });

  factory ConversationSavedEvent.fromJson(Map<String, dynamic> json) {
    return ConversationSavedEvent(
      conversation: json['conversation'] as String,
      rev: json['rev'] as int,
      id: json['id'] as String,
      title: json['title'] as String,
    );
  }
}

class ConversationRenamedEvent extends RpcEvent {
  final String conversation;
  final int rev;
  final String title;

  ConversationRenamedEvent({
    required this.conversation,
    required this.rev,
    required this.title,
  });

  factory ConversationRenamedEvent.fromJson(Map<String, dynamic> json) {
    return ConversationRenamedEvent(
      conversation: json['conversation'] as String,
      rev: json['rev'] as int,
      title: json['title'] as String,
    );
  }
}

class ConversationClosedEvent extends RpcEvent {
  final String conversation;
  final String reason;

  ConversationClosedEvent({required this.conversation, required this.reason});

  factory ConversationClosedEvent.fromJson(Map<String, dynamic> json) {
    return ConversationClosedEvent(
      conversation: json['conversation'] as String,
      reason: json['reason'] as String,
    );
  }
}

class ChatFinishedEvent extends RpcEvent {
  final String conversation;
  final int rev;
  final ChatError? error;

  ChatFinishedEvent({
    required this.conversation,
    required this.rev,
    this.error,
  });

  factory ChatFinishedEvent.fromJson(Map<String, dynamic> json) {
    return ChatFinishedEvent(
      conversation: json['conversation'] as String,
      rev: json['rev'] as int,
      error: json['error'] != null
          ? ChatError.fromJson(json['error'] as Map<String, dynamic>)
          : null,
    );
  }
}

class ConversationsChangedEvent extends RpcEvent {}

class SettingsChangedEvent extends RpcEvent {
  final String section;

  SettingsChangedEvent({required this.section});

  factory SettingsChangedEvent.fromJson(Map<String, dynamic> json) {
    return SettingsChangedEvent(section: json['section'] as String);
  }
}
