/// Todos los textos propios de la GUI. Los que vienen de `moss-rpc` se
/// muestran tal cual.
class Strings {
  static const appName = 'moss';

  // Barra lateral
  static const newChat = 'Nuevo chat';
  static const newChatKey = 'Ctrl+N';
  static const chats = 'Chats';
  static const withWebSearch = 'Con búsqueda web';

  // Encabezado
  static const hideSidebar = 'Ocultar barra lateral (Ctrl+B)';
  static const showSidebar = 'Mostrar barra lateral (Ctrl+B)';
  static const export = 'Exportar';

  // Chat
  static const emptyPrompt = '¿En qué te ayudo?';
  static const searching = 'Buscando en la web…';
  static const thinking = 'Pensando…';
  static String thoughtSummary(String count, bool one) =>
      'Pensamiento · $count ${one ? 'palabra' : 'palabras'}';
  static const cancelled = '(cancelada)';
  static const incomplete = '(respuesta incompleta)';
  static const sources = 'Fuentes';
  static const copy = 'Copiar';
  static const copied = 'Copiado ✓';

  // Caja del mensaje
  static const message = 'Mensaje';
  static const messageHint = 'Escribe tu mensaje';
  static const send = 'Enviar';
  static const stop = 'Detener';
  static const help = 'Enter envía · Shift+Enter nueva línea · / comandos';
  static const modelPickerHint = 'Escribe /model <proveedor> <modelo>.';

  // moss-rpc
  static const rpcNotFound = 'No se encontró moss-rpc';
  static const rpcCannotStart = 'moss-rpc no arrancó';
  static const rpcOtherVersion = 'moss-rpc es de otra versión de Moss';
  static const rpcOtherVersionDetail =
      'Instala la app y moss-rpc de la misma versión.';
  static const rpcSearchedAt = 'Se buscó en';
  static const rpcStderr = 'Últimas líneas de moss-rpc';
  static const rpcDisconnected = 'moss-rpc se cerró.';
  static const retry = 'Reintentar';
}
