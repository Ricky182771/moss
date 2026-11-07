import 'package:flutter/material.dart';

import '../theme/colors.dart';

/// Logo de Moss: la "m" con el cursor de bloque, del SVG de DISENO.md
/// (64×64, trazo 7, puntas redondas).
class MossLogo extends StatelessWidget {
  final double size;

  const MossLogo({super.key, this.size = 28});

  @override
  Widget build(BuildContext context) {
    return ExcludeSemantics(
      child: CustomPaint(
        size: Size.square(size),
        painter: _LogoPainter(context.colors.accent),
      ),
    );
  }
}

class _LogoPainter extends CustomPainter {
  final Color color;

  _LogoPainter(this.color);

  @override
  void paint(Canvas canvas, Size size) {
    canvas.scale(size.width / 64);
    final stroke = Paint()
      ..color = color
      ..style = PaintingStyle.stroke
      ..strokeWidth = 7
      ..strokeCap = StrokeCap.round
      ..strokeJoin = StrokeJoin.round;
    // M8 48V30a9 9 0 0 1 18 0v18M26 30a9 9 0 0 1 18 0v18 (sweep 1 = horario).
    final m = Path()
      ..moveTo(8, 48)
      ..lineTo(8, 30)
      ..arcToPoint(const Offset(26, 30), radius: const Radius.circular(9))
      ..lineTo(26, 48)
      ..moveTo(26, 30)
      ..arcToPoint(const Offset(44, 30), radius: const Radius.circular(9))
      ..lineTo(44, 48);
    canvas.drawPath(m, stroke);
    canvas.drawRRect(
      RRect.fromRectAndRadius(
        const Rect.fromLTWH(49, 39, 10, 10),
        const Radius.circular(1.5),
      ),
      Paint()..color = color,
    );
  }

  @override
  bool shouldRepaint(_LogoPainter old) => old.color != color;
}
