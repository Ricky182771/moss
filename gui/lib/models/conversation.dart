/// Modelos de datos según PROTOCOL.md §5
library;

/// Tipo de entrada en la conversación
enum EntryKind {
  user,
  assistant,
  error,
  notice,
  sources;

  factory EntryKind.fromString(String value) {
    return EntryKind.values.firstWhere((e) => e.name == value);
  }
}

/// Fuente de búsqueda web
class Source {
  final int n;
  final String title;
  final String url;
  final String? publishedDate;

  Source({
    required this.n,
    required this.title,
    required this.url,
    this.publishedDate,
  });

  factory Source.fromJson(Map<String, dynamic> json) {
    return Source(
      n: json['n'] as int,
      title: json['title'] as String,
      url: json['url'] as String,
      publishedDate: json['published_date'] as String?,
    );
  }
}

/// Entrada de la conversación
class Entry {
  final EntryKind kind;
  final String text;
  final bool inProgress;
  final bool incomplete;
  final bool cancelled;
  final String note;
  final String reasoning;
  final List<Source> sources;

  Entry({
    required this.kind,
    required this.text,
    this.inProgress = false,
    this.incomplete = false,
    this.cancelled = false,
    this.note = '',
    this.reasoning = '',
    this.sources = const [],
  });

  factory Entry.fromJson(Map<String, dynamic> json) {
    return Entry(
      kind: EntryKind.fromString(json['kind'] as String),
      text: json['text'] as String,
      inProgress: json['in_progress'] as bool? ?? false,
      incomplete: json['incomplete'] as bool? ?? false,
      cancelled: json['cancelled'] as bool? ?? false,
      note: json['note'] as String? ?? '',
      reasoning: json['reasoning'] as String? ?? '',
      sources:
          (json['sources'] as List<dynamic>?)
              ?.map((s) => Source.fromJson(s as Map<String, dynamic>))
              .toList() ??
          [],
    );
  }

  Entry copyWith({
    EntryKind? kind,
    String? text,
    bool? inProgress,
    bool? incomplete,
    bool? cancelled,
    String? note,
    String? reasoning,
    List<Source>? sources,
  }) {
    return Entry(
      kind: kind ?? this.kind,
      text: text ?? this.text,
      inProgress: inProgress ?? this.inProgress,
      incomplete: incomplete ?? this.incomplete,
      cancelled: cancelled ?? this.cancelled,
      note: note ?? this.note,
      reasoning: reasoning ?? this.reasoning,
      sources: sources ?? this.sources,
    );
  }
}

/// Fase de la conversación
enum Phase {
  idle,
  searching,
  thinking,
  streaming;

  factory Phase.fromString(String value) {
    return Phase.values.firstWhere((e) => e.name == value);
  }
}

/// Snapshot de la conversación
class ConversationSnapshot {
  final String conversation;
  final int rev;
  final String? id;
  final String title;
  final String createdAt;
  final String lastModel;
  final Phase phase;
  final List<Entry> entries;

  ConversationSnapshot({
    required this.conversation,
    required this.rev,
    this.id,
    required this.title,
    required this.createdAt,
    required this.lastModel,
    required this.phase,
    required this.entries,
  });

  factory ConversationSnapshot.fromJson(Map<String, dynamic> json) {
    return ConversationSnapshot(
      conversation: json['conversation'] as String,
      rev: json['rev'] as int,
      id: json['id'] as String?,
      title: json['title'] as String,
      createdAt: json['created_at'] as String,
      lastModel: json['last_model'] as String,
      phase: Phase.fromString(json['phase'] as String),
      entries: (json['entries'] as List<dynamic>)
          .map((e) => Entry.fromJson(e as Map<String, dynamic>))
          .toList(),
    );
  }

  ConversationSnapshot copyWith({
    String? conversation,
    int? rev,
    String? id,
    String? title,
    String? createdAt,
    String? lastModel,
    Phase? phase,
    List<Entry>? entries,
  }) {
    return ConversationSnapshot(
      conversation: conversation ?? this.conversation,
      rev: rev ?? this.rev,
      id: id ?? this.id,
      title: title ?? this.title,
      createdAt: createdAt ?? this.createdAt,
      lastModel: lastModel ?? this.lastModel,
      phase: phase ?? this.phase,
      entries: entries ?? this.entries,
    );
  }
}
