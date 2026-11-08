/// Ver registro: partir las líneas `short-iso` de `service.log` y agruparlas
/// por día (DISENO.md → Configuración → Ver registro). Es presentación, no
/// regla del núcleo: qué línea es un error lo dice el servidor (`errors`).
library;

import '../strings.dart';

/// Una línea del registro como se muestra.
class LogLine {
  /// La línea completa, tal como llegó (Copiar usa esta).
  final String raw;

  /// `HH:MM:SS`, o null si la línea no tiene el formato `short-iso`.
  final String? time;

  /// El día de la línea (sin hora), o null sin formato.
  final DateTime? day;

  /// Lo que se muestra: el mensaje, o la línea completa sin formato.
  final String message;
  final bool error;

  const LogLine({
    required this.raw,
    required this.time,
    required this.day,
    required this.message,
    required this.error,
  });
}

/// Un separador de día o una línea.
sealed class LogItem {
  const LogItem();
}

class LogDay extends LogItem {
  final String label;
  const LogDay(this.label);
}

class LogEntry extends LogItem {
  final LogLine line;
  const LogEntry(this.line);
}

/// `<fecha>T<hora><desfase> <equipo> <programa>[<pid>]: <mensaje>`; el pid
/// es opcional. La fecha es la hora local que escribió `journalctl`.
final _shortIso = RegExp(
  r'^(\d{4})-(\d{2})-(\d{2})T(\d{2}:\d{2}:\d{2})(?:[+-]\d{2}:?\d{2}|Z)? '
  r'\S+ [^\s\[:]+(?:\[\d+\])?: (.*)$',
);

LogLine parseLogLine(String raw, {bool error = false}) {
  final m = _shortIso.firstMatch(raw);
  if (m == null) {
    return LogLine(raw: raw, time: null, day: null, message: raw, error: error);
  }
  return LogLine(
    raw: raw,
    time: m.group(4),
    day: DateTime(
      int.parse(m.group(1)!),
      int.parse(m.group(2)!),
      int.parse(m.group(3)!),
    ),
    message: m.group(5)!,
    error: error,
  );
}

/// Hoy, Ayer, `31 oct` y, de otro año, `31 oct 2025`.
String dayLabel(DateTime day, DateTime now) {
  final today = DateTime(now.year, now.month, now.day);
  final d = DateTime(day.year, day.month, day.day);
  if (d == today) return Strings.today;
  // Por fecha y no por 24 h: también con el cambio de horario.
  final yesterday = DateTime(now.year, now.month, now.day - 1);
  if (d == yesterday) return Strings.yesterday;
  final label = '${d.day} ${Strings.monthsShort[d.month - 1]}';
  return d.year == now.year ? label : '$label ${d.year}';
}

/// Las líneas con un separador cada vez que cambia el día. Una línea sin
/// formato se queda en el día de la anterior. [errors] son los índices de
/// `errors` del servidor.
List<LogItem> logItems(List<String> lines, Iterable<int> errors, DateTime now) {
  final marked = errors.toSet();
  final items = <LogItem>[];
  DateTime? current;
  for (final (i, raw) in lines.indexed) {
    final line = parseLogLine(raw, error: marked.contains(i));
    if (line.day case final day? when day != current) {
      current = day;
      items.add(LogDay(dayLabel(day, now)));
    }
    items.add(LogEntry(line));
  }
  return items;
}
