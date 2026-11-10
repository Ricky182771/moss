import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/main.dart';
import 'package:moss_gui/models/gui_prefs.dart';
import 'package:moss_gui/theme/colors.dart';
import 'package:moss_gui/widgets/window_channel.dart';

String _hex(Color c) =>
    (c.toARGB32() & 0xFFFFFF).toRadixString(16).padLeft(6, '0').toUpperCase();

void main() {
  group('el canal moss/window', () {
    late List<MethodCall> calls;
    bool? animations;

    setUp(() {
      calls = [];
      animations = null; // Como Linux: el runner no lo sabe.
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(kWindowChannel, (call) async {
            if (call.method == 'animationsEnabled') return animations;
            calls.add(call);
            return null;
          });
    });

    tearDown(() {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(kWindowChannel, null);
    });

    Iterable<Object?> sent() => calls.map((c) {
      expect(c.method, 'setDark');
      return c.arguments;
    });

    testWidgets('se llama al arrancar y al cambiar de tema, sin repetir', (
      tester,
    ) async {
      final prefs = ValueNotifier(const GuiPrefs(theme: ThemeChoice.dark));
      addTearDown(prefs.dispose);
      await tester.pumpWidget(PrefsApp(prefs: prefs, home: const SizedBox()));
      await tester.pumpAndSettle();
      expect(sent(), [true]);

      prefs.value = const GuiPrefs(theme: ThemeChoice.light);
      await tester.pumpAndSettle();
      expect(sent(), [true, false]);

      // Otra preferencia, con el mismo tema: no se vuelve a llamar.
      prefs.value = const GuiPrefs(
        theme: ThemeChoice.light,
        reduceMotion: true,
      );
      await tester.pumpAndSettle();
      expect(sent(), [true, false]);
    });

    testWidgets('con Sistema sigue al del sistema', (tester) async {
      addTearDown(tester.platformDispatcher.clearPlatformBrightnessTestValue);
      tester.platformDispatcher.platformBrightnessTestValue = Brightness.light;
      final prefs = ValueNotifier(const GuiPrefs());
      addTearDown(prefs.dispose);
      await tester.pumpWidget(PrefsApp(prefs: prefs, home: const SizedBox()));
      await tester.pumpAndSettle();
      expect(sent(), [false]);

      tester.platformDispatcher.platformBrightnessTestValue = Brightness.dark;
      await tester.pumpAndSettle();
      expect(sent(), [false, true]);
    });

    group('Efectos de animación de Windows', () {
      late bool reduced;

      Future<void> pumpApp(WidgetTester tester, GuiPrefs saved) async {
        final prefs = ValueNotifier(saved);
        addTearDown(prefs.dispose);
        await tester.pumpWidget(
          PrefsApp(
            prefs: prefs,
            home: Builder(
              builder: (context) {
                reduced = MediaQuery.disableAnimationsOf(context);
                return const SizedBox();
              },
            ),
          ),
        );
        await tester.pumpAndSettle();
      }

      testWidgets('apagados al arrancar: se reducen las animaciones', (
        tester,
      ) async {
        animations = false;
        await pumpApp(tester, const GuiPrefs());
        expect(reduced, isTrue);
      });

      testWidgets('el runner avisa si cambian', (tester) async {
        animations = true;
        await pumpApp(tester, const GuiPrefs());
        expect(reduced, isFalse);

        Future<void> fromRunner(bool enabled) async {
          await tester.binding.defaultBinaryMessenger.handlePlatformMessage(
            kWindowChannel.name,
            kWindowChannel.codec.encodeMethodCall(
              MethodCall('animationsEnabled', enabled),
            ),
            (_) {},
          );
          await tester.pumpAndSettle();
        }

        await fromRunner(false);
        expect(reduced, isTrue);
        await fromRunner(true);
        expect(reduced, isFalse);
      });

      testWidgets('encendidos: manda Reducir animaciones de la app', (
        tester,
      ) async {
        animations = true;
        await pumpApp(tester, const GuiPrefs(reduceMotion: true));
        expect(reduced, isTrue);
      });
    });

    testWidgets('Contraste alto del sistema: los tokens de Más contraste', (
      tester,
    ) async {
      late MossColors colors;
      final prefs = ValueNotifier(const GuiPrefs(theme: ThemeChoice.dark));
      addTearDown(prefs.dispose);
      Widget app(bool highContrast) => MediaQuery(
        data: MediaQueryData(highContrast: highContrast),
        child: PrefsApp(
          prefs: prefs,
          home: Builder(
            builder: (context) {
              colors = context.colors;
              return const SizedBox();
            },
          ),
        ),
      );
      await tester.pumpWidget(app(false));
      await tester.pumpAndSettle();
      expect(colors, MossColors.dark);

      await tester.pumpWidget(app(true));
      await tester.pumpAndSettle();
      expect(colors, MossColors.darkHigh);
      expect(prefs.value.highContrast, isFalse); // Lo guardado no cambia.

      prefs.value = const GuiPrefs(theme: ThemeChoice.light);
      await tester.pumpAndSettle();
      expect(colors, MossColors.lightHigh);
    });

    testWidgets('sin el canal o si falla, la app sigue', (tester) async {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(
            kWindowChannel,
            (_) async => throw PlatformException(code: 'no'),
          );
      final prefs = ValueNotifier(const GuiPrefs(theme: ThemeChoice.dark));
      addTearDown(prefs.dispose);
      await tester.pumpWidget(PrefsApp(prefs: prefs, home: const SizedBox()));
      await tester.pumpAndSettle();
      expect(tester.takeException(), isNull);

      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(kWindowChannel, null);
      prefs.value = const GuiPrefs(theme: ThemeChoice.light);
      await tester.pumpAndSettle();
      expect(tester.takeException(), isNull);
    });
  });

  // Los runners no pueden leer MossColors: el fondo del primer cuadro es copia.
  test('el fondo de los runners es el de MossColors', () {
    final dark = _hex(MossColors.dark.background);
    final light = _hex(MossColors.light.background);
    final linux = File('linux/runner/my_application.cc').readAsStringSync();
    expect(linux, contains('kBackgroundDark[] = "#$dark"'));
    expect(linux, contains('kBackgroundLight[] = "#$light"'));

    String rgb(String hex) =>
        'RGB(0x${hex.substring(0, 2)}, 0x${hex.substring(2, 4)}, '
        '0x${hex.substring(4)})';
    final windows = File('windows/runner/win32_window.cpp').readAsStringSync();
    expect(windows, contains('kBackgroundDark = ${rgb(dark)}'));
    expect(windows, contains('kBackgroundLight = ${rgb(light)}'));
  });
}
