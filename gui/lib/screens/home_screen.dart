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
  late final _opacity = CurvedAnimation(parent: _fade, curve: Motion.enter);

  /// El foco del chat al entrar; regresa ahí al salir.
  FocusNode? _chatFocus;

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
      _chatFocus ??= FocusManager.instance.primaryFocus;
      setState(() => _page = page);
      _fade.forward();
    } else {
      // Configuración se desvanece; el chat recibe el foco (el de antes) sin
      // esperar el fundido.
      setState(() {});
      _fade.reverse().whenComplete(() {
        if (mounted && widget.app.settingsOpen.value == null) {
          setState(() => _page = null);
        }
      });
      final focus = _chatFocus;
      _chatFocus = null;
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (focus != null && focus.context != null) focus.requestFocus();
      });
    }
  }

  @override
  void dispose() {
    widget.app.settingsOpen.removeListener(_changed);
    _opacity.dispose();
    _fade.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final open = widget.app.settingsOpen.value != null;
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
            child: IgnorePointer(
              ignoring: !open,
              child: ExcludeFocus(
                excluding: !open,
                child: ExcludeSemantics(
                  excluding: !open,
                  child: FadeTransition(
                    opacity: _opacity,
                    child: SettingsScreen(widget.app, page: page),
                  ),
                ),
              ),
            ),
          ),
      ],
    );
  }
}
