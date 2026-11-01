import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/conversation.dart';
import '../state/app_state.dart';
import '../strings.dart';
import '../theme/colors.dart';
import '../widgets/chat_entry.dart';
import '../widgets/chat_input_box.dart';
import '../widgets/logo.dart';

/// Pantalla principal de chat según DISENO.md
class ChatScreen extends StatefulWidget {
  const ChatScreen({super.key});

  @override
  State<ChatScreen> createState() => _ChatScreenState();
}

class _ChatScreenState extends State<ChatScreen> {
  final ScrollController _scrollController = ScrollController();
  String _currentText = '';

  @override
  void dispose() {
    _scrollController.dispose();
    super.dispose();
  }

  void _scrollToBottom() {
    if (_scrollController.hasClients) {
      _scrollController.animateTo(
        _scrollController.position.maxScrollExtent,
        duration: const Duration(milliseconds: 200),
        curve: Curves.easeOut,
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final colors = context.colors;
    final appState = context.watch<AppState>();
    final conversation = appState.conversation;

    return Scaffold(
      backgroundColor: colors.background,
      appBar: _buildAppBar(context, colors, appState),
      body: Column(
        children: [
          // Área de mensajes
          Expanded(
            child: conversation == null
                ? _buildEmptyState(context, colors)
                : _buildMessageList(context, conversation),
          ),

          // Indicador de fase
          if (conversation != null && conversation.phase != Phase.idle)
            _buildPhaseIndicator(context, colors, conversation.phase),

          // Error si hay
          if (appState.lastError != null)
            _buildErrorBanner(context, colors, appState),

          // Caja de entrada
          ChatInputBox(
            busy: appState.isBusy,
            onSend: () => _handleSend(appState),
            onStop: () => appState.cancelMessage(),
            onChanged: (text) => _currentText = text,
          ),
        ],
      ),
    );
  }

  PreferredSizeWidget _buildAppBar(
    BuildContext context,
    MossColors colors,
    AppState appState,
  ) {
    final conversation = appState.conversation;

    return AppBar(
      backgroundColor: colors.bar,
      elevation: 0,
      title: Row(
        children: [
          const MossLogo(size: 24),
          const SizedBox(width: 12),
          if (conversation != null)
            Expanded(
              child: Text(
                conversation.title,
                style: Theme.of(context).textTheme.titleMedium,
                overflow: TextOverflow.ellipsis,
              ),
            ),
        ],
      ),
      actions: [
        // Nuevo chat
        IconButton(
          icon: const Icon(Icons.add),
          tooltip: Strings.newChat,
          onPressed: appState.isBusy ? null : () => appState.newConversation(),
        ),
        // Exportar
        if (conversation != null && conversation.id != null)
          IconButton(
            icon: const Icon(Icons.download),
            tooltip: Strings.export,
            onPressed: appState.isBusy
                ? null
                : () => _handleExport(context, appState),
          ),
      ],
    );
  }

  Widget _buildEmptyState(BuildContext context, MossColors colors) {
    return Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const MossLogo(size: 64),
          const SizedBox(height: 24),
          Text(
            'Moss',
            style: Theme.of(context).textTheme.titleMedium
                ?.copyWith(fontSize: 24),
          ),
          const SizedBox(height: 8),
          Text(
            Strings.emptyConversation,
            style: Theme.of(context).textTheme.bodyMedium
                ?.copyWith(color: colors.textMuted),
          ),
        ],
      ),
    );
  }

  Widget _buildMessageList(BuildContext context, conversation) {
    WidgetsBinding.instance.addPostFrameCallback((_) => _scrollToBottom());

    return ListView.builder(
      controller: _scrollController,
      padding: const EdgeInsets.all(16),
      itemCount: conversation.entries.length,
      itemBuilder: (context, index) {
        return ChatEntry(entry: conversation.entries[index], index: index);
      },
    );
  }

  Widget _buildPhaseIndicator(
    BuildContext context,
    MossColors colors,
    Phase phase,
  ) {
    final text = switch (phase) {
      Phase.searching => Strings.phaseSearching,
      Phase.thinking => Strings.phaseThinking,
      Phase.streaming => '', // No mostramos nada en streaming
      Phase.idle => '',
    };

    if (text.isEmpty) return const SizedBox.shrink();

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      decoration: BoxDecoration(
        color: colors.field,
        border: Border(top: BorderSide(color: colors.border)),
      ),
      child: Row(
        children: [
          SizedBox(
            width: 16,
            height: 16,
            child: CircularProgressIndicator(
              strokeWidth: 2,
              valueColor: AlwaysStoppedAnimation(colors.textMuted),
            ),
          ),
          const SizedBox(width: 12),
          Text(text, style: Theme.of(context).textTheme.labelSmall),
        ],
      ),
    );
  }

  Widget _buildErrorBanner(
    BuildContext context,
    MossColors colors,
    AppState appState,
  ) {
    return Container(
      padding: const EdgeInsets.all(12),
      color: colors.errorBg,
      child: Row(
        children: [
          Icon(Icons.error_outline, color: colors.error, size: 20),
          const SizedBox(width: 12),
          Expanded(
            child: Text(
              appState.lastError!.fullMessage,
              style: Theme.of(context).textTheme.bodyMedium
                  ?.copyWith(color: colors.error),
            ),
          ),
          IconButton(
            icon: const Icon(Icons.close),
            iconSize: 20,
            color: colors.error,
            onPressed: () => appState.clearError(),
          ),
        ],
      ),
    );
  }

  void _handleSend(AppState appState) {
    final text = _currentText.trim();
    if (text.isEmpty) return;

    appState.sendMessage(text);
  }

  void _handleExport(BuildContext context, AppState appState) async {
    try {
      final result = await appState.exportConversation();
      if (context.mounted) {
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text(result.message)));
      }
    } catch (e) {
      if (context.mounted) {
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text('Error: $e')));
      }
    }
  }
}
