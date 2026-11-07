import 'package:flutter/material.dart';

import '../state/app_state.dart';
import '../state/settings_state.dart';
import '../theme/motion.dart';
import 'chat_screen.dart';
import 'settings_screen.dart';

/// El chat y, encima, Configuración con un fundido (DISENO.md: Chat ↔
/// Configuración, 160 ms, sin deslizar). El chat se queda montado debajo para
/// no perder su scroll ni lo escrito en la caja.
class HomeScreen extends StatefulWidget {
  final AppState app;
  const HomeScreen(this.app, {super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen>
    with SingleTickerProviderStateMixin {
  late final _fade = AnimationController(vsync: this);

  /// La sección que se ve; se queda mientras se desvanece al salir.
  SettingsPage? _page;

  @override
  void initState() {
    super.initState();
    widget.app.settingsOpen.addListener(_changed);
  }

  void _changed() {
    final page = widget.app.settingsOpen.value;
    _fade.duration = Motion.of(context).screenFade;
    if (page != null) {
      setState(() => _page = page);
      _fade.forward();
    } else {
      _fade.reverse().whenComplete(() {
        if (mounted && widget.app.settingsOpen.value == null) {
          setState(() => _page = null);
        }
      });
    }
  }

  @override
  void dispose() {
    widget.app.settingsOpen.removeListener(_changed);
    _fade.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final open = _page != null;
    return Stack(
      children: [
        Positioned.fill(
          child: ExcludeFocus(
            excluding: open,
            child: ExcludeSemantics(
              excluding: open,
              child: ChatScreen(widget.app),
            ),
          ),
        ),
        if (_page case final page?)
          Positioned.fill(
            child: FadeTransition(
              opacity: CurvedAnimation(parent: _fade, curve: Motion.enter),
              child: SettingsScreen(widget.app, page: page),
            ),
          ),
      ],
    );
  }
}
