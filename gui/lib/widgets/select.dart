import 'package:flutter/widgets.dart';

/// Como `ListenableBuilder`, pero reconstruye solo cuando cambia lo que
/// devuelve [select] (con `==`). Así un trozo de respuesta no reconstruye la
/// barra lateral ni la caja.
class Select<T> extends StatefulWidget {
  final Listenable listenable;
  final T Function() select;
  final Widget Function(BuildContext context, T value) builder;

  const Select({
    super.key,
    required this.listenable,
    required this.select,
    required this.builder,
  });

  @override
  State<Select<T>> createState() => _SelectState<T>();
}

class _SelectState<T> extends State<Select<T>> {
  late T _value = widget.select();

  @override
  void initState() {
    super.initState();
    widget.listenable.addListener(_changed);
  }

  @override
  void didUpdateWidget(Select<T> old) {
    super.didUpdateWidget(old);
    if (old.listenable != widget.listenable) {
      old.listenable.removeListener(_changed);
      widget.listenable.addListener(_changed);
    }
    _value = widget.select();
  }

  void _changed() {
    final v = widget.select();
    if (v != _value) setState(() => _value = v);
  }

  @override
  void dispose() {
    widget.listenable.removeListener(_changed);
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => widget.builder(context, _value);
}
