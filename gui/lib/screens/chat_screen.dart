import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../state/app_state.dart';
import '../strings.dart';
import '../theme/colors.dart';
import '../theme/motion.dart';
import '../theme/theme.dart';
import '../widgets/controls.dart';
import '../widgets/input_box.dart';
import '../widgets/logo.dart';
import '../widgets/message_list.dart';
import '../widgets/select.dart';
import '../widgets/sidebar.dart';
import '../widgets/toast.dart';

/// Por debajo de este ancho la barra se oculta sola y Ctrl+B la abre encima.
const kNarrowWidth = 900.0;

/// DISENO.md → Pantallas → Chat. Esqueleto fijo: barra lateral, encabezado y
/// caja del mensaje; solo se desplazan los mensajes y la lista de chats.
class ChatScreen extends StatefulWidget {
  final AppState app;
  const ChatScreen(this.app, {super.key});

  @override
  State<ChatScreen> createState() => _ChatScreenState();
}

class _ChatScreenState extends State<ChatScreen> {
  /// Barra abierta en una ventana ancha.
  bool _docked = true;

  /// Barra abierta encima del chat en una ventana angosta.
  bool _overlay = false;
  bool _narrow = false;

  AppState get app => widget.app;

  void _toggleSidebar() => setState(() {
    if (_narrow) {
      _overlay = !_overlay;
    } else {
      _docked = !_docked;
    }
  });

  void _closeOverlay() {
    if (_overlay) setState(() => _overlay = false);
  }

  void _escape() {
    if (_narrow && _overlay) {
      _closeOverlay();
    } else {
      app.cancel();
    }
  }

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    return CallbackShortcuts(
      bindings: {
        const SingleActivator(LogicalKeyboardKey.keyN, control: true): () {
          _closeOverlay();
          app.newChat();
        },
        const SingleActivator(LogicalKeyboardKey.keyB, control: true):
            _toggleSidebar,
        const SingleActivator(LogicalKeyboardKey.escape): _escape,
      },
      child: Material(
        color: c.background,
        child: LayoutBuilder(
          builder: (context, box) {
            _narrow = box.maxWidth < kNarrowWidth;
            // Al ensanchar, la barra encima se cierra (no reaparece sola).
            if (!_narrow) _overlay = false;
            // Solo con la lista (conversations.changed) o el chat abierto.
            final sidebar = Select(
              listenable: app,
              select: () => (app.summaries, app.conversation?.id),
              builder: (context, v) => Sidebar(
                summaries: v.$1,
                openId: v.$2,
                onNewChat: () {
                  _closeOverlay();
                  app.newChat();
                },
                onOpen: (id) {
                  _closeOverlay();
                  app.open(id);
                },
              ),
            );
            final overlayOpen = _narrow && _overlay;
            // El mismo árbol en ancha y angosta, para no perder el scroll.
            return Stack(
              children: [
                Row(
                  children: [
                    FocusTraversalGroup(
                      child: _SidebarSlot(
                        open: !_narrow && _docked,
                        child: sidebar,
                      ),
                    ),
                    Expanded(
                      child: FocusTraversalGroup(
                        child: _Main(
                          app: app,
                          sidebarOpen: _narrow ? _overlay : _docked,
                          onToggleSidebar: _toggleSidebar,
                        ),
                      ),
                    ),
                  ],
                ),
                if (overlayOpen)
                  Positioned.fill(
                    child: GestureDetector(
                      behavior: HitTestBehavior.opaque,
                      onTap: _closeOverlay,
                    ),
                  ),
                Positioned(
                  left: 0,
                  top: 0,
                  bottom: 0,
                  child: FocusTraversalGroup(
                    child: _SidebarSlot(open: overlayOpen, child: sidebar),
                  ),
                ),
              ],
            );
          },
        ),
      ),
    );
  }
}

/// La barra con el ancho animado (Ctrl+B); cerrada no ocupa ni recibe foco.
class _SidebarSlot extends StatelessWidget {
  final bool open;
  final Widget child;
  const _SidebarSlot({required this.open, required this.child});

  @override
  Widget build(BuildContext context) {
    return TweenAnimationBuilder<double>(
      tween: Tween(end: open ? 1 : 0),
      duration: open
          ? Motion.of(context).sidebar
          : Motion.of(context).sidebarClose,
      curve: Motion.enter,
      child: child,
      builder: (context, t, child) => t == 0
          ? const SizedBox.shrink()
          : SizedBox(
              width: kSidebarWidth * t,
              child: ClipRect(
                child: OverflowBox(
                  alignment: Alignment.centerRight,
                  minWidth: kSidebarWidth,
                  maxWidth: kSidebarWidth,
                  child: child,
                ),
              ),
            ),
    );
  }
}

class _Main extends StatelessWidget {
  final AppState app;
  final bool sidebarOpen;
  final VoidCallback onToggleSidebar;

  const _Main({
    required this.app,
    required this.sidebarOpen,
    required this.onToggleSidebar,
  });

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    // Cada parte escucha solo lo suyo: un trozo de respuesta reconstruye los
    // mensajes, no el encabezado ni la caja.
    return Column(
      children: [
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 14),
          decoration: BoxDecoration(
            border: Border(bottom: BorderSide(color: c.border)),
          ),
          child: Row(
            spacing: 12,
            children: [
              IconAction(
                label: sidebarOpen ? Strings.hideSidebar : Strings.showSidebar,
                labelBelow: true,
                onPressed: onToggleSidebar,
                icon: SidebarIcon(color: c.textMuted),
              ),
              Expanded(
                child: Select(
                  listenable: app,
                  select: () => app.conversation?.title ?? '',
                  builder: (context, title) => Semantics(
                    header: true,
                    child: Text(
                      title.isEmpty ? Strings.newChat : title,
                      style: s.title,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                    ),
                  ),
                ),
              ),
              Pressable(
                onPressed: app.export,
                minHeight: 40,
                padding: const EdgeInsets.symmetric(horizontal: 12),
                child: Row(
                  mainAxisSize: MainAxisSize.min,
                  spacing: 8,
                  children: [
                    Icon(Icons.download_rounded, size: 18, color: c.text),
                    Text(Strings.export, style: s.row),
                  ],
                ),
              ),
            ],
          ),
        ),
        Expanded(
          child: Stack(
            children: [
              Positioned.fill(
                child: Select(
                  listenable: app,
                  select: () => app.conversation,
                  builder: (context, conv) =>
                      conv == null || conv.entries.isEmpty
                      ? const _EmptyChat()
                      : MessageList(conv),
                ),
              ),
              Positioned(
                top: 12,
                left: 24,
                right: 24,
                child: Select(
                  listenable: app,
                  select: () => app.disconnected,
                  builder: (context, disconnected) => disconnected
                      ? Center(child: _Disconnected(app))
                      : const SizedBox.shrink(),
                ),
              ),
              Positioned(
                left: 24,
                right: 24,
                bottom: 12,
                child: Center(child: ToastHost(app.toast)),
              ),
            ],
          ),
        ),
        InputBox(app),
      ],
    );
  }
}

class _EmptyChat extends StatelessWidget {
  const _EmptyChat();

  @override
  Widget build(BuildContext context) => Center(
    child: Padding(
      padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 48),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        spacing: 16,
        children: [
          const MossLogo(size: 64),
          Text(
            Strings.emptyPrompt,
            style: context.styles.heading,
            textAlign: TextAlign.center,
          ),
        ],
      ),
    ),
  );
}

/// Aviso fijo arriba del chat cuando `moss-rpc` se cae a media sesión.
class _Disconnected extends StatelessWidget {
  final AppState app;
  const _Disconnected(this.app);

  @override
  Widget build(BuildContext context) {
    final c = context.colors;
    final s = context.styles;
    return Container(
      padding: const EdgeInsets.fromLTRB(14, 6, 6, 6),
      decoration: BoxDecoration(
        color: c.sidebar,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: c.border),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        spacing: 10,
        children: [
          Icon(Icons.error_outline_rounded, size: 18, color: c.error),
          Semantics(
            liveRegion: true,
            child: Text(Strings.rpcDisconnected, style: s.row),
          ),
          Pressable(
            onPressed: app.retry,
            minHeight: 36,
            padding: const EdgeInsets.symmetric(horizontal: 12),
            child: Center(
              widthFactor: 1,
              child: Text(
                Strings.retry,
                style: s.nav.copyWith(color: c.accent),
              ),
            ),
          ),
        ],
      ),
    );
  }
}
