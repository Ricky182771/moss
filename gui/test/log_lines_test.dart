import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/state/log_lines.dart';
import 'package:moss_gui/strings.dart';

void main() {
  group('parseLogLine', () {
    test('con pid: la hora y el mensaje', () {
      final l = parseLogLine(
        '2026-11-07T14:06:20-0600 equipo moss-telegram[4242]: '
        '2026-11-07 14:06:20 error chat=5 http=429',
      );
      expect(l.time, '14:06:20');
      expect(l.day, DateTime(2026, 11, 7));
      expect(l.message, '2026-11-07 14:06:20 error chat=5 http=429');
    });

    test('sin pid', () {
      final l = parseLogLine(
        '2026-10-31T23:10:02-0600 equipo systemd: Stopping moss-telegram',
      );
      expect(l.time, '23:10:02');
      expect(l.message, 'Stopping moss-telegram');
    });

    test('desfase con dos puntos o Z', () {
      expect(
        parseLogLine('2026-10-31T23:10:02+05:30 pc x[1]: a').time,
        '23:10:02',
      );
      expect(parseLogLine('2026-10-31T23:10:02Z pc x[1]: a').message, 'a');
    });

    test('sin el formato: la línea completa, sin hora ni día', () {
      for (final raw in [
        '-- Boot 3f2a… --',
        'una línea sin formato',
        '2026-11-07 14:06:20 sin la T',
        '',
      ]) {
        final l = parseLogLine(raw);
        expect(l.time, isNull, reason: raw);
        expect(l.day, isNull, reason: raw);
        expect(l.message, raw);
      }
    });

    test('UTF-8 en el mensaje y en el programa', () {
      final l = parseLogLine(
        '2026-11-07T09:00:00-0600 pc moss-telegram[7]: «Límite» ñandú 🚀: ok',
      );
      expect(l.message, '«Límite» ñandú 🚀: ok');
      expect(l.raw, contains('🚀'));
    });

    test('la marca de error es la del servidor', () {
      expect(parseLogLine('x', error: true).error, isTrue);
      // Un "error" en el texto no la pone: la regla es del servidor.
      expect(
        parseLogLine('2026-11-07T09:00:00-0600 pc a[1]: error').error,
        isFalse,
      );
    });
  });

  group('dayLabel', () {
    final now = DateTime(2026, 11, 7, 13, 58);
    test('Hoy, Ayer, el día y el año', () {
      expect(dayLabel(DateTime(2026, 11, 7), now), Strings.today);
      expect(dayLabel(DateTime(2026, 11, 6), now), Strings.yesterday);
      expect(dayLabel(DateTime(2026, 10, 31), now), '31 oct');
      expect(dayLabel(DateTime(2025, 12, 31), now), '31 dic 2025');
    });

    test('en el cambio de día, por fecha y no por 24 h', () {
      final midnight = DateTime(2026, 11, 8, 0, 0, 5);
      expect(dayLabel(DateTime(2026, 11, 7), midnight), Strings.yesterday);
      expect(dayLabel(DateTime(2026, 11, 8), midnight), Strings.today);
      // Año nuevo: ayer es 31 dic del año anterior.
      expect(
        dayLabel(DateTime(2026, 12, 31), DateTime(2027, 1, 1)),
        Strings.yesterday,
      );
      // Primero de mes.
      expect(
        dayLabel(DateTime(2026, 2, 28), DateTime(2026, 3, 1, 8)),
        Strings.yesterday,
      );
    });
  });

  test('logItems: un separador por día, sin formato se queda en su día', () {
    final items = logItems(
      [
        '2026-11-06T22:41:07-0600 pc moss-telegram[1]: a',
        '2026-11-06T23:59:59-0600 pc moss-telegram[1]: b',
        'sin formato',
        '2026-11-07T00:00:01-0600 pc moss-telegram[1]: c',
        '2026-11-07T08:00:00-0600 pc moss-telegram[1]: d',
      ],
      [1, 3],
      DateTime(2026, 11, 7, 9),
    );
    expect(
      [
        for (final i in items)
          switch (i) {
            LogDay(:final label) => '# $label',
            LogEntry(:final line) => '${line.error ? '!' : ' '}${line.message}',
          },
      ],
      [
        '# ${Strings.yesterday}',
        ' a',
        '!b',
        ' sin formato',
        '# ${Strings.today}',
        '!c',
        ' d',
      ],
    );
  });
}
