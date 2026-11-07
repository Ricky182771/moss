import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/frame_stats.dart';

FrameSample _frame(int vsyncMs, {int build = 1000, int raster = 2000}) =>
    (vsyncStart: vsyncMs * 1000, build: build, raster: raster);

void main() {
  group('FrameStats', () {
    test('resume un segundo al llegar un cuadro del siguiente', () {
      final stats = FrameStats(60);
      expect(stats.add(_frame(0, build: 1000, raster: 2000)), isNull);
      expect(stats.add(_frame(17, build: 3000, raster: 6000)), isNull);
      // 50 ms > 1.5 × 16.7 ms: un intervalo lento.
      expect(stats.add(_frame(67)), isNull);
      expect(
        stats.add(_frame(1000)),
        'cuadros 3 · build prom 1.7 máx 3.0 ms · '
        'raster prom 3.3 máx 6.0 ms · '
        'intervalo prom 33.5 máx 50.0 ms (>1.5×: 1) · esperado 16.7 ms',
      );
    });

    test('un hueco largo es la app quieta, no un intervalo', () {
      final stats = FrameStats(60);
      stats.add(_frame(0));
      stats.add(_frame(500));
      expect(stats.flush(), contains('intervalo — ·'));
    });

    test('el intervalo sigue entre segundos', () {
      final stats = FrameStats(60);
      stats.add(_frame(990));
      stats.add(_frame(1010));
      expect(stats.flush(), contains('intervalo prom 20.0 máx 20.0 ms'));
    });

    test('escribe lo pendiente tras dos ticks sin cuadros', () {
      final stats = FrameStats(60);
      stats.add(_frame(0));
      expect(stats.tick(), isNull);
      expect(stats.tick(), startsWith('cuadros 1 ·'));
      expect(stats.tick(), isNull);
      expect(stats.flush(), isNull);
    });

    test('usa la frecuencia de la pantalla', () {
      final stats = FrameStats(120);
      stats.add(_frame(0));
      stats.add(_frame(17)); // 17 ms > 1.5 × 8.3 ms.
      expect(stats.flush(), contains('(>1.5×: 1) · esperado 8.3 ms'));
    });
  });
}
