import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/models/gui_prefs.dart';
import 'package:moss_gui/screens/settings_screen.dart';
import 'package:moss_gui/state/app_state.dart';
import 'package:moss_gui/strings.dart';
import 'package:moss_gui/theme/colors.dart';
import 'package:moss_gui/theme/motion.dart';

import 'fake_server.dart';
import 'harness.dart';

/// Razón de contraste de WCAG 2.
double _contrast(Color a, Color b) {
  double l(Color c) {
    double ch(double v) => v <= 0.03928
        ? v / 12.92
        : math.pow((v + 0.055) / 1.055, 2.4).toDouble();
    return 0.2126 * ch(c.r) + 0.7152 * ch(c.g) + 0.0722 * ch(c.b);
  }

  final (x, y) = (l(a), l(b));
  return (math.max(x, y) + 0.05) / (math.min(x, y) + 0.05);
}

Future<void> _openColors(WidgetTester tester) async {
  await tester.sendKeyEvent(LogicalKeyboardKey.f2);
  await tester.pumpAndSettle();
  await tester.tap(find.text(Strings.colorsTitle).first);
  await tester.pumpAndSettle();
}

BuildContext _ctx(WidgetTester tester) =>
    tester.element(find.byType(SettingsScreen));

void main() {
  group('GuiPrefs', () {
    test('sin nada, otra versión o algo raro: lo predeterminado', () {
      expect(GuiPrefs.fromJson(null), const GuiPrefs());
      expect(GuiPrefs.fromJson('x'), const GuiPrefs());
      expect(
        GuiPrefs.fromJson({'version': 2, 'theme': 'dark'}),
        const GuiPrefs(),
      );
    });

    test('un valor desconocido o faltante usa el predeterminado', () {
      final p = GuiPrefs.fromJson({
        'version': 1,
        'theme': 'morado',
        'text_size': 'large',
        'reduce_motion': 'sí',
        'high_contrast': true,
      });
      expect(p.theme, ThemeChoice.system);
      expect(p.textSize, TextSize.large);
      expect(p.reduceMotion, isFalse);
      expect(p.highContrast, isTrue);
    });

    test('ida y vuelta con version 1', () {
      const p = GuiPrefs(
        theme: ThemeChoice.light,
        textSize: TextSize.small,
        reduceMotion: true,
      );
      expect(p.toJson()['version'], 1);
      expect(GuiPrefs.fromJson(p.toJson()), p);
    });

    test('al arrancar, un error de settings.gui.get no rompe nada', () async {
      final server = FakeServer(
        handlers: {
          'settings.gui.get': (_) =>
              throw const FakeError(-32007, 'No se pudo leer config.json'),
        },
      );
      final client = server.client();
      await client.initialize();
      expect(await loadGuiPrefs(client), const GuiPrefs());
    });
  });

  test('Más contraste: todos los pares de texto en 7:1 o más', () {
    for (final c in [MossColors.darkHigh, MossColors.lightHigh]) {
      for (final fg in [c.text, c.textMuted, c.accent, c.code, c.error]) {
        for (final bg in [c.background, c.sidebar, c.field, c.accentSoft]) {
          expect(_contrast(fg, bg), greaterThanOrEqualTo(7), reason: '$fg/$bg');
        }
      }
      expect(_contrast(c.textOnAccent, c.accent), greaterThanOrEqualTo(7));
      expect(c.focusWidth, greaterThan(MossColors.dark.focusWidth));
    }
  });

  testWidgets('el tema cambia al momento y Cancelar regresa', (tester) async {
    await startApp(tester);
    await _openColors(tester);
    expect(Theme.of(_ctx(tester)).brightness, Brightness.dark);

    await tester.tap(find.text(Strings.themeLight));
    await tester.pump();
    expect(Theme.of(_ctx(tester)).brightness, Brightness.light);
    // La marca de cambios en el menú.
    expect(
      find.bySemanticsLabel(Strings.withUnsaved(Strings.colorsTitle)),
      findsOneWidget,
    );

    await tester.tap(find.text(Strings.cancel));
    await tester.pump();
    expect(Theme.of(_ctx(tester)).brightness, Brightness.dark);
  });

  testWidgets('Guardar manda settings.gui.set con version 1', (tester) async {
    Map<String, dynamic>? sent;
    final (_, server) = await startApp(
      tester,
      handlers: {
        'settings.gui.set': (p) {
          sent = p;
          return <String, Object?>{};
        },
      },
    );
    await _openColors(tester);
    await tester.tap(find.text(Strings.highContrast));
    await tester.pump();
    expect(_ctx(tester).colors.textMuted, MossColors.darkHigh.textMuted);

    await tester.tap(find.text(Strings.save));
    await tester.pumpAndSettle();
    expect(sent!['value'], {
      'version': 1,
      'theme': 'dark',
      'text_size': 'normal',
      'reduce_motion': false,
      'high_contrast': true,
    });
    // Ya no hay cambios: Esc sale sin preguntar y Más contraste se queda.
    await tester.sendKeyEvent(LogicalKeyboardKey.escape);
    await tester.pumpAndSettle();
    expect(find.byType(SettingsScreen), findsNothing);
    expect(server.methods, contains('app.status'));
  });

  testWidgets('Reducir animaciones deja todo en 0 ms', (tester) async {
    await startApp(tester);
    await _openColors(tester);
    expect(Motion.of(_ctx(tester)).screenFade, isNot(Duration.zero));
    await tester.tap(find.text(Strings.reduceMotion));
    await tester.pump();
    expect(Motion.of(_ctx(tester)).screenFade, Duration.zero);
    expect(Motion.of(_ctx(tester)).cursorBlink, Duration.zero);
  });

  testWidgets('el texto Grande cabe en 720×520', (tester) async {
    await startApp(
      tester,
      size: const Size(720, 520),
      prefs: const GuiPrefs(theme: ThemeChoice.dark, textSize: TextSize.large),
    );
    final scaler = MediaQuery.textScalerOf(
      tester.element(find.text(Strings.emptyPrompt)),
    );
    expect(scaler.scale(10), closeTo(12, 0.001));
    expect(tester.takeException(), isNull);
    await tester.sendKeyEvent(LogicalKeyboardKey.f2);
    await tester.pumpAndSettle();
    for (final p in [
      Strings.colorsTitle,
      Strings.modelTitle,
      Strings.webSearch,
      Strings.providerMenu,
    ]) {
      await tester.tap(find.text(p).first);
      await tester.pumpAndSettle();
      expect(tester.takeException(), isNull, reason: p);
    }
  });

  testWidgets('salir con cambios pregunta «¿Descartar los cambios?»', (
    tester,
  ) async {
    await startApp(tester);
    await _openColors(tester);
    await tester.tap(find.text(Strings.themeLight));
    await tester.pump();

    await tester.sendKeyEvent(LogicalKeyboardKey.escape);
    await tester.pumpAndSettle();
    expect(find.text(Strings.discardQuestion), findsOneWidget);
    await tester.tap(find.text(Strings.keepEditing));
    await tester.pumpAndSettle();
    expect(find.byType(SettingsScreen), findsOneWidget);
    expect(Theme.of(_ctx(tester)).brightness, Brightness.light);

    await tester.tap(find.text(Strings.backToChat));
    await tester.pumpAndSettle();
    await tester.tap(find.text(Strings.discard));
    await tester.pumpAndSettle();
    expect(find.byType(SettingsScreen), findsNothing);
    expect(
      Theme.of(tester.element(find.text(Strings.emptyPrompt))).brightness,
      Brightness.dark,
    );
  });

  testWidgets('un error de escritura va en la barra y no se pierde nada', (
    tester,
  ) async {
    await startApp(
      tester,
      handlers: {
        'settings.gui.set': (_) => throw const FakeError(-32007, 'Error', {
          'message': 'No se pudo escribir config.json.',
        }),
      },
    );
    await _openColors(tester);
    await tester.tap(find.text(Strings.themeLight));
    await tester.pump();
    await tester.tap(find.text(Strings.save));
    await tester.pumpAndSettle();
    expect(find.text('No se pudo escribir config.json.'), findsOneWidget);
    // El borrador sigue y con su marca.
    expect(Theme.of(_ctx(tester)).brightness, Brightness.light);
    expect(
      find.bySemanticsLabel(Strings.withUnsaved(Strings.colorsTitle)),
      findsOneWidget,
    );
  });

  testWidgets('settings.changed: sin cambios recarga; con cambios, el aviso', (
    tester,
  ) async {
    var stored = const GuiPrefs(theme: ThemeChoice.dark);
    final (_, server) = await startApp(
      tester,
      handlers: {
        'settings.gui.get': (_) => {'value': stored.toJson()},
      },
    );
    await _openColors(tester);

    stored = const GuiPrefs(theme: ThemeChoice.dark, highContrast: true);
    server.notify('settings.changed', {'section': 'gui'});
    await tester.pumpAndSettle();
    expect(_ctx(tester).colors.textMuted, MossColors.darkHigh.textMuted);
    expect(find.text(Strings.changedOutside), findsNothing);

    await tester.tap(find.text(Strings.themeLight));
    await tester.pump();
    server.notify('settings.changed', {'section': 'gui'});
    await tester.pumpAndSettle();
    expect(find.text(Strings.changedOutside), findsOneWidget);
    expect(Theme.of(_ctx(tester)).brightness, Brightness.light);
  });
}
