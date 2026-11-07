import 'dart:async';
import 'dart:io';
import 'dart:math' as math;
import 'dart:ui';

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';

/// Un cuadro de `FrameTiming`, en microsegundos.
typedef FrameSample = ({int vsyncStart, int build, int raster});

/// Cada cuánto se revisa si la app quedó quieta para escribir el último segundo.
const _tick = Duration(seconds: 1);

/// Un hueco mayor entre cuadros es la app quieta, no un tirón: no cuenta como intervalo.
const _idleGapMicros = 250000;

/// Agrupa cuadros por segundo (según su vsync) y los resume en una línea.
class FrameStats {
  final double refreshRate;
  FrameStats(this.refreshRate);

  int? _second;
  int? _lastVsync;
  int _idleTicks = 0;
  final _build = <int>[];
  final _raster = <int>[];
  final _intervals = <int>[];

  double get _expected => 1e6 / refreshRate;

  /// Agrega un cuadro; si ya es de otro segundo, devuelve la línea del anterior.
  String? add(FrameSample s) {
    _idleTicks = 0;
    final second = s.vsyncStart ~/ 1000000;
    final line = _second != null && second != _second ? flush() : null;
    _second = second;
    if (_lastVsync case final last?) {
      final gap = s.vsyncStart - last;
      if (gap <= _idleGapMicros) _intervals.add(gap);
    }
    _lastVsync = s.vsyncStart;
    _build.add(s.build);
    _raster.add(s.raster);
    return line;
  }

  /// Llamar cada segundo: tras dos sin cuadros, escribe lo pendiente.
  String? tick() => ++_idleTicks >= 2 ? flush() : null;

  /// Resume lo acumulado y empieza de nuevo; null si no hay cuadros.
  String? flush() {
    if (_build.isEmpty) return null;
    final slow = _intervals.where((i) => i > _expected * 1.5).length;
    final interval = _intervals.isEmpty
        ? 'intervalo —'
        : 'intervalo ${_avgMax(_intervals)} ms (>1.5×: $slow)';
    final line =
        'cuadros ${_build.length} · build ${_avgMax(_build)} ms · '
        'raster ${_avgMax(_raster)} ms · $interval · '
        'esperado ${(_expected / 1000).toStringAsFixed(1)} ms';
    _build.clear();
    _raster.clear();
    _intervals.clear();
    _second = null;
    return line;
  }

  static String _avgMax(List<int> us) {
    final avg = us.reduce((a, b) => a + b) / us.length / 1000;
    final max = us.reduce(math.max) / 1000;
    return 'prom ${avg.toStringAsFixed(1)} máx ${max.toStringAsFixed(1)}';
  }
}

/// `--frame-stats`: escribe en stderr el renderizador, la frecuencia de la
/// pantalla y, cada segundo con cuadros, sus tiempos. Nunca en release.
void startFrameStats() {
  if (kReleaseMode) return;
  final binding = WidgetsFlutterBinding.ensureInitialized();
  FrameStats? stats;
  void write(String? line) {
    if (line != null) stderr.writeln('frame-stats: $line');
  }

  binding.addTimingsCallback((timings) {
    // La pantalla se lee con el primer cuadro: antes puede no estar reportada.
    if (stats == null) {
      final rate =
          PlatformDispatcher.instance.views.firstOrNull?.display.refreshRate ??
          0;
      // isShaderFilterSupported es true solo con Impeller (dart:ui).
      final renderer = ImageFilter.isShaderFilterSupported
          ? 'Impeller'
          : 'Skia';
      write(
        'renderizador $renderer · pantalla ${rate.toStringAsFixed(2)} Hz · '
        'modo ${kProfileMode ? 'profile' : 'debug'}',
      );
      stats = FrameStats(rate > 0 ? rate : 60);
    }
    for (final t in timings) {
      write(
        stats!.add((
          vsyncStart: t.timestampInMicroseconds(FramePhase.vsyncStart),
          build: t.buildDuration.inMicroseconds,
          raster: t.rasterDuration.inMicroseconds,
        )),
      );
    }
  });
  Timer.periodic(_tick, (_) => write(stats?.tick()));
}
