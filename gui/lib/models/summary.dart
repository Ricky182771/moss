/// Resumen de conversación para la lista según PROTOCOL.md §6.2
library;

class ConversationSummary {
  final String id;
  final String title;
  final String updatedAt;
  final int messageCount;
  final String lastModel;
  final bool readable;
  final String error;
  final String group;
  final bool usedSearch;
  final String? openAs;

  ConversationSummary({
    required this.id,
    required this.title,
    required this.updatedAt,
    required this.messageCount,
    required this.lastModel,
    required this.readable,
    required this.error,
    required this.group,
    required this.usedSearch,
    this.openAs,
  });

  factory ConversationSummary.fromJson(Map<String, dynamic> json) {
    return ConversationSummary(
      id: json['id'] as String,
      title: json['title'] as String,
      updatedAt: json['updated_at'] as String,
      messageCount: json['message_count'] as int,
      lastModel: json['last_model'] as String,
      readable: json['readable'] as bool,
      error: json['error'] as String? ?? '',
      group: json['group'] as String,
      usedSearch: json['used_search'] as bool,
      openAs: json['open_as'] as String?,
    );
  }

  /// Si esta conversación está abierta en este proceso
  bool get isOpen => openAs != null;
}
