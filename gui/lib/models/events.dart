/// Eventos del servidor que usa la GUI (PROTOCOL.md §7). Los demás llegan al
/// `registerFallback` y se ignoran.
library;

import 'conversation.dart';

sealed class RpcEvent {
  const RpcEvent();

  /// Los métodos que esta versión conoce.
  static const methods = {
    'conversation.entry_added',
    'conversation.entry_delta',
    'conversation.reasoning_delta',
    'conversation.entry_updated',
    'conversation.phase',
    'conversation.saved',
    'conversation.renamed',
    'chat.finished',
    'conversations.changed',
    'settings.changed',
    'service.changed',
  };

  static RpcEvent parse(String method, Map<String, dynamic> p) {
    if (method == 'conversations.changed') return const ConversationsChanged();
    if (method == 'settings.changed') {
      return SettingsChanged(p['section'] as String);
    }
    if (method == 'service.changed') {
      return ServiceChanged(p['addon'] as String);
    }
    final c = p['conversation'] as String;
    final rev = p['rev'] as int;
    return switch (method) {
      'conversation.entry_added' => EntryAdded(
        c,
        rev,
        p['index'] as int,
        Entry.fromJson(p['entry'] as Map<String, dynamic>),
      ),
      'conversation.entry_delta' => EntryDelta(
        c,
        rev,
        p['index'] as int,
        p['text'] as String,
      ),
      'conversation.reasoning_delta' => ReasoningDelta(
        c,
        rev,
        p['index'] as int,
        p['text'] as String,
      ),
      'conversation.entry_updated' => EntryUpdated(
        c,
        rev,
        p['index'] as int,
        Entry.fromJson(p['entry'] as Map<String, dynamic>),
      ),
      'conversation.phase' => PhaseChanged(c, rev, Phase.parse(p['phase'])),
      'conversation.saved' => Saved(
        c,
        rev,
        p['id'] as String,
        p['title'] as String,
      ),
      'conversation.renamed' => Renamed(c, rev, p['title'] as String),
      'chat.finished' => ChatFinished(
        c,
        rev,
        p['outcome'] as String,
        p['restore_text'] as String?,
      ),
      _ => throw ArgumentError.value(method, 'method'),
    };
  }
}

class ConversationsChanged extends RpcEvent {
  const ConversationsChanged();
}

/// `service.changed`: terminó una acción del servicio de un addon (PROTOCOL.md
/// §6.8), aunque se haya cancelado o haya fallado.
class ServiceChanged extends RpcEvent {
  final String addon;
  const ServiceChanged(this.addon);
}

/// `settings.changed`: `provider`, `search`, `system_prompt`, `gui`…
class SettingsChanged extends RpcEvent {
  final String section;
  const SettingsChanged(this.section);
}

/// Evento de una conversación: lleva el `rev` que deja.
sealed class ConversationEvent extends RpcEvent {
  final String conversation;
  final int rev;
  const ConversationEvent(this.conversation, this.rev);
}

class EntryAdded extends ConversationEvent {
  final int index;
  final Entry entry;
  const EntryAdded(super.conversation, super.rev, this.index, this.entry);
}

class EntryDelta extends ConversationEvent {
  final int index;
  final String text;
  const EntryDelta(super.conversation, super.rev, this.index, this.text);
}

class ReasoningDelta extends ConversationEvent {
  final int index;
  final String text;
  const ReasoningDelta(super.conversation, super.rev, this.index, this.text);
}

class EntryUpdated extends ConversationEvent {
  final int index;
  final Entry entry;
  const EntryUpdated(super.conversation, super.rev, this.index, this.entry);
}

class PhaseChanged extends ConversationEvent {
  final Phase phase;
  const PhaseChanged(super.conversation, super.rev, this.phase);
}

class Saved extends ConversationEvent {
  final String id;
  final String title;
  const Saved(super.conversation, super.rev, this.id, this.title);
}

class Renamed extends ConversationEvent {
  final String title;
  const Renamed(super.conversation, super.rev, this.title);
}

class ChatFinished extends ConversationEvent {
  /// ok | error | cancelled | no_results
  final String outcome;

  /// Con error, cancelled o no_results: el texto vuelve a la caja.
  final String? restoreText;
  const ChatFinished(
    super.conversation,
    super.rev,
    this.outcome,
    this.restoreText,
  );
}
