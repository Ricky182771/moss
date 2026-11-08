/// Modelo de la conversación de PROTOCOL.md §5.
library;

enum EntryKind { user, assistant, error, notice, sources }

/// Fuente de una búsqueda web.
class Source {
  final int n;
  final String title;
  final String url;
  final String? publishedDate;

  const Source({
    required this.n,
    required this.title,
    required this.url,
    this.publishedDate,
  });

  factory Source.fromJson(Map<String, dynamic> json) => Source(
    n: json['n'] as int,
    title: json['title'] as String,
    url: json['url'] as String,
    publishedDate: json['published_date'] as String?,
  );
}

class Entry {
  final EntryKind kind;

  /// Markdown crudo.
  final String text;
  final bool inProgress;
  final bool incomplete;
  final bool cancelled;
  final String note;

  /// Pensamiento del modelo (markdown crudo); "" si no hay.
  final String reasoning;

  /// Palabras de [reasoning], contadas por el servidor.
  final int reasoningWords;
  final List<Source> sources;

  const Entry({
    required this.kind,
    this.text = '',
    this.inProgress = false,
    this.incomplete = false,
    this.cancelled = false,
    this.note = '',
    this.reasoning = '',
    this.reasoningWords = 0,
    this.sources = const [],
  });

  factory Entry.fromJson(Map<String, dynamic> json) => Entry(
    // Un tipo que esta versión no conoce se muestra como aviso.
    kind: EntryKind.values.asNameMap()[json['kind']] ?? EntryKind.notice,
    text: json['text'] as String? ?? '',
    inProgress: json['in_progress'] as bool? ?? false,
    incomplete: json['incomplete'] as bool? ?? false,
    cancelled: json['cancelled'] as bool? ?? false,
    note: json['note'] as String? ?? '',
    reasoning: json['reasoning'] as String? ?? '',
    reasoningWords: json['reasoning_words'] as int? ?? 0,
    sources: [
      for (final s in json['sources'] as List? ?? const [])
        Source.fromJson(s as Map<String, dynamic>),
    ],
  );

  Entry copyWith({String? text, String? reasoning, int? reasoningWords}) =>
      Entry(
        kind: kind,
        text: text ?? this.text,
        inProgress: inProgress,
        incomplete: incomplete,
        cancelled: cancelled,
        note: note,
        reasoning: reasoning ?? this.reasoning,
        reasoningWords: reasoningWords ?? this.reasoningWords,
        sources: sources,
      );
}

enum Phase {
  idle,
  searching,
  thinking,
  streaming;

  static Phase parse(Object? value) => Phase.values.asNameMap()[value] ?? idle;
}

class ConversationSnapshot {
  /// Handle que asigna el servidor ("c1").
  final String conversation;
  final int rev;

  /// null hasta el primer par guardado.
  final String? id;

  /// "" hasta el primer par guardado.
  final String title;
  final Phase phase;
  final List<Entry> entries;

  const ConversationSnapshot({
    required this.conversation,
    required this.rev,
    this.id,
    this.title = '',
    this.phase = Phase.idle,
    this.entries = const [],
  });

  factory ConversationSnapshot.fromJson(Map<String, dynamic> json) =>
      ConversationSnapshot(
        conversation: json['conversation'] as String,
        rev: json['rev'] as int,
        id: json['id'] as String?,
        title: json['title'] as String? ?? '',
        phase: Phase.parse(json['phase']),
        entries: [
          for (final e in json['entries'] as List)
            Entry.fromJson(e as Map<String, dynamic>),
        ],
      );

  ConversationSnapshot copyWith({
    required int rev,
    String? id,
    String? title,
    Phase? phase,
    List<Entry>? entries,
  }) => ConversationSnapshot(
    conversation: conversation,
    rev: rev,
    id: id ?? this.id,
    title: title ?? this.title,
    phase: phase ?? this.phase,
    entries: entries ?? this.entries,
  );
}
