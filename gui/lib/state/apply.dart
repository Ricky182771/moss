/// Aplicar eventos a la copia de la conversación (PROTOCOL.md §5), sin estado.
library;

import '../models/conversation.dart';
import '../models/events.dart';
import '../strings.dart';

/// El snapshot después de [e].
///
/// - Evento de otra conversación o viejo (`rev` ya visto): el mismo [s].
/// - `null` si hay un hueco (`rev` no es el siguiente o el índice no cuadra):
///   hay que pedir `conversation.get`.
ConversationSnapshot? applyEvent(ConversationSnapshot s, ConversationEvent e) {
  if (e.conversation != s.conversation || e.rev <= s.rev) return s;
  if (e.rev != s.rev + 1) return null;
  final entries = s.entries;
  final last = entries.length - 1;

  List<Entry> replaceLast(Entry Function(Entry) f) => [
    ...entries.take(last),
    f(entries[last]),
  ];

  switch (e) {
    case EntryAdded(:final index, :final entry):
      if (index != entries.length) return null;
      return s.copyWith(rev: e.rev, entries: [...entries, entry]);
    case EntryDelta(:final index, :final text):
      if (index != last) return null;
      return s.copyWith(
        rev: e.rev,
        entries: replaceLast((x) => x.copyWith(text: x.text + text)),
      );
    case ReasoningDelta(:final index, :final text):
      if (index != last) return null;
      return s.copyWith(
        rev: e.rev,
        entries: replaceLast((x) => x.copyWith(reasoning: x.reasoning + text)),
      );
    case EntryUpdated(:final index, :final entry):
      if (index != last) return null;
      return s.copyWith(rev: e.rev, entries: replaceLast((_) => entry));
    case PhaseChanged(:final phase):
      return s.copyWith(rev: e.rev, phase: phase);
    case Saved(:final id, :final title):
      return s.copyWith(rev: e.rev, id: id, title: title);
    case Renamed(:final title):
      return s.copyWith(rev: e.rev, title: title);
    case ChatFinished():
      return s.copyWith(rev: e.rev);
  }
}

/// "Pensamiento · N palabras", contado como la TUI (`reasoning_summary`).
String thoughtSummary(String reasoning) {
  final words = RegExp(r'[^ \n\t\r]+').allMatches(reasoning).length;
  final digits = words.toString().replaceAllMapped(
    RegExp(r'\B(?=(\d{3})+$)'),
    (_) => ' ',
  );
  return Strings.thoughtSummary(digits, words == 1);
}
