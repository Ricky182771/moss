import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/theme/colors.dart';
import 'package:moss_gui/theme/theme.dart';
import 'package:moss_gui/theme/motion.dart';
import 'package:moss_gui/widgets/logo.dart';

void main() {
  group('Tema', () {
    test('MossColors oscuro tiene todos los colores', () {
      const colors = MossColors.dark;
      expect(colors.background, const Color(0xFF0F1411));
      expect(colors.sidebar, const Color(0xFF161D18));
      expect(colors.accent, const Color(0xFF8DBF5F));
    });

    test('MossColors claro tiene todos los colores', () {
      const colors = MossColors.light;
      expect(colors.background, const Color(0xFFFFFFFF));
      expect(colors.sidebar, const Color(0xFFF6F8F3));
      expect(colors.accent, const Color(0xFF3F6B2A));
    });

    testWidgets('ThemeData oscuro se construye correctamente', (
      WidgetTester tester,
    ) async {
      final theme = MossTheme.dark();
      expect(theme.brightness, Brightness.dark);
      expect(theme.extension<MossColors>(), isNotNull);
      expect(theme.scaffoldBackgroundColor, const Color(0xFF0F1411));
    });

    testWidgets('ThemeData claro se construye correctamente', (
      WidgetTester tester,
    ) async {
      final theme = MossTheme.light();
      expect(theme.brightness, Brightness.light);
      expect(theme.extension<MossColors>(), isNotNull);
      expect(theme.scaffoldBackgroundColor, const Color(0xFFFFFFFF));
    });
  });

  group('Motion', () {
    testWidgets('Duraciones respetan MediaQuery.disableAnimations', (
      WidgetTester tester,
    ) async {
      await tester.pumpWidget(
        MediaQuery(
          data: const MediaQueryData(disableAnimations: true),
          child: Builder(
            builder: (context) {
              final motion = Motion.of(context);
              expect(motion.hoverDuration(context), Duration.zero);
              return const SizedBox();
            },
          ),
        ),
      );
    });

    testWidgets('Duraciones normales cuando animaciones están habilitadas', (
      WidgetTester tester,
    ) async {
      await tester.pumpWidget(
        MediaQuery(
          data: const MediaQueryData(disableAnimations: false),
          child: Builder(
            builder: (context) {
              final motion = Motion.of(context);
              expect(
                motion.hoverDuration(context),
                const Duration(milliseconds: 100),
              );
              return const SizedBox();
            },
          ),
        ),
      );
    });
  });

  group('Logo', () {
    testWidgets('MossLogo se dibuja sin errores', (WidgetTester tester) async {
      await tester.pumpWidget(
        const MaterialApp(home: Scaffold(body: MossLogo(size: 64))),
      );
      expect(find.byType(MossLogo), findsOneWidget);
    });

    testWidgets('MossLogo respeta el tamaño especificado', (
      WidgetTester tester,
    ) async {
      await tester.pumpWidget(
        const MaterialApp(home: Scaffold(body: MossLogo(size: 48))),
      );
      final logo = tester.widget<MossLogo>(find.byType(MossLogo));
      expect(logo.size, 48);
    });
  });
}
