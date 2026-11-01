import 'package:flutter/material.dart';

/// Logo de Moss: "m" con cursor de bloque
class MossLogo extends StatelessWidget {
  final double size;
  final Color? color;

  const MossLogo({super.key, this.size = 28, this.color});

  @override
  Widget build(BuildContext context) {
    final logoColor = color ?? const Color(0xFF8DBF5F);
    return CustomPaint(
      size: Size(size, size),
      painter: _MossLogoPainter(logoColor),
    );
  }
}

class _MossLogoPainter extends CustomPainter {
  final Color color;

  _MossLogoPainter(this.color);

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..color = color
      ..style = PaintingStyle.stroke
      ..strokeWidth = size.width * (7.0 / 64.0)
      ..strokeCap = StrokeCap.round
      ..strokeJoin = StrokeJoin.round;

    final scale = size.width / 64.0;

    // Primera "m": M8 48V30a9 9 0 0 1 18 0v18
    final path1 = Path()
      ..moveTo(8 * scale, 48 * scale)
      ..lineTo(8 * scale, 30 * scale)
      ..arcToPoint(
        Offset(26 * scale, 30 * scale),
        radius: Radius.circular(9 * scale),
        clockwise: true,
      )
      ..lineTo(26 * scale, 48 * scale);

    // Segunda "m": M26 30a9 9 0 0 1 18 0v18
    final path2 = Path()
      ..moveTo(26 * scale, 30 * scale)
      ..arcToPoint(
        Offset(44 * scale, 30 * scale),
        radius: Radius.circular(9 * scale),
        clockwise: true,
      )
      ..lineTo(44 * scale, 48 * scale);

    canvas.drawPath(path1, paint);
    canvas.drawPath(path2, paint);

    // Cursor: rect x=49 y=39 width=10 height=10 rx=1.5
    final rectPaint = Paint()
      ..color = color
      ..style = PaintingStyle.fill;

    final rect = RRect.fromRectAndRadius(
      Rect.fromLTWH(49 * scale, 39 * scale, 10 * scale, 10 * scale),
      Radius.circular(1.5 * scale),
    );

    canvas.drawRRect(rect, rectPaint);
  }

  @override
  bool shouldRepaint(_MossLogoPainter oldDelegate) {
    return oldDelegate.color != color;
  }
}
