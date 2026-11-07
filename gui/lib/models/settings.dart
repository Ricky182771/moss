/// Lo que usa Opciones de `settings.provider.get`, `app.status` y
/// `commands.list` (PROTOCOL.md §6.1, §6.4 y §6.5). Solo `fromJson`.
library;

/// Un proveedor de la tabla o un endpoint (`custom:<id>`).
class ProviderChoice {
  final String id;
  final String name;

  /// Tiene key (o es local): se puede elegir. Si no, va atenuado.
  final bool selectable;

  /// "" o "sin key".
  final String note;

  const ProviderChoice({
    required this.id,
    required this.name,
    required this.selectable,
    this.note = '',
  });

  factory ProviderChoice.fromJson(Map<String, dynamic> json) => ProviderChoice(
    id: json['id'] as String,
    name: json['name'] as String,
    selectable: json['selectable'] as bool,
    note: json['note'] as String? ?? '',
  );
}

/// `settings.provider.get`.
class ProviderSettings {
  final List<ProviderChoice> providers;

  /// El proveedor guardado y el modelo que usa la sesión.
  final String provider;
  final String model;

  /// Fijados por `CHAT_BASE_URL` y `CHAT_MODEL`.
  final bool baseUrlLocked;
  final bool modelLocked;

  const ProviderSettings({
    required this.providers,
    required this.provider,
    required this.model,
    this.baseUrlLocked = false,
    this.modelLocked = false,
  });

  factory ProviderSettings.fromJson(Map<String, dynamic> json) {
    final locks = json['locks'] as Map<String, dynamic>;
    return ProviderSettings(
      providers: [
        for (final p in json['providers'] as List)
          ProviderChoice.fromJson(p as Map<String, dynamic>),
      ],
      provider: (json['saved'] as Map<String, dynamic>)['provider'] as String,
      model: (json['effective'] as Map<String, dynamic>)['model'] as String,
      baseUrlLocked: locks['base_url'] as bool,
      modelLocked: locks['model'] as bool,
    );
  }
}

/// Un comando de `commands.list`.
class CommandInfo {
  final String name;
  final String arguments;
  final String description;
  final bool needsArgument;
  final bool available;

  /// Por qué no está disponible; "" si lo está.
  final String note;

  const CommandInfo({
    required this.name,
    this.arguments = '',
    this.description = '',
    this.needsArgument = false,
    this.available = true,
    this.note = '',
  });

  factory CommandInfo.fromJson(Map<String, dynamic> json) => CommandInfo(
    name: json['name'] as String,
    arguments: json['arguments'] as String? ?? '',
    description: json['description'] as String? ?? '',
    needsArgument: json['needs_argument'] as bool? ?? false,
    available: json['available'] as bool? ?? true,
    note: json['note'] as String? ?? '',
  );
}
