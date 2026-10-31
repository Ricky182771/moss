#ifndef CHATBOT_CLI_MODEL_PICKER_H
#define CHATBOT_CLI_MODEL_PICKER_H

#include "conversation_list.h"
#include "models_loader.h"
#include "provider_settings.h"
#include "session.h"

#include "chatbot/config.h"
#include "chatbot/result.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chatbot::cli {

/// Lo que la interfaz debe hacer después de una tecla o un clic en el selector.
struct ModelPickerAction {
    enum class Type {
        Pass,       ///< El selector no la usó: sigue el camino de siempre.
        Handled,    ///< La usó (cursor); nada más que hacer.
        ClearInput, ///< Cambió de paso: vaciar la caja (es el filtro).
        Closed,     ///< Esc en el primer paso: se cerró.
        Status,     ///< text va a la línea de estado (proveedor que no se puede elegir).
        Switch,     ///< Elegido: Session::switch_model(provider, text).
    };
    Type type = Type::Pass;
    std::string provider; ///< Switch: id del proveedor.
    std::string text;     ///< Status: el aviso. Switch: el modelo.
};

/// Una fila del selector, como la dibuja la vista.
struct ModelPickerRow {
    enum class Kind {
        Provider, ///< Paso de proveedor: choice.
        Model,    ///< Paso de modelo: text es el id.
        UseTyped, ///< "Usar «text»": el filtro como id escrito a mano.
        Notice,   ///< Sin cursor: text es el aviso (cargando, sin coincidencias).
    };
    Kind kind = Kind::Notice;
    std::string text;
    const ProviderChoice* choice = nullptr; ///< Provider: el de choices().
    bool current = false;                   ///< El proveedor o el modelo en uso.
};

/// Estado del selector de /model, sin FTXUI ni hilos propios: dos pasos
/// (proveedor y modelo), el filtro (el texto de la caja), el cursor, la
/// ventana de filas visibles y la lista de modelos, que pide con su propio
/// ModelsLoader (se cancela al cerrar o al regresar al paso de proveedor).
/// Se usa solo desde el hilo de la interfaz.
class ModelPicker {
public:
    /// Filas que se ven a la vez; con más, la ventana sigue al cursor.
    static constexpr std::size_t kVisibleRows = 8;
    static constexpr std::string_view kLoading = "Cargando modelos…";
    static constexpr std::string_view kNoProviders = "Sin proveedores que coincidan";
    static constexpr std::string_view kTypeModel = "Escribe el id del modelo";

    /// La configuración para pedir los modelos de un proveedor o, si no se
    /// puede elegir, el error con su texto (Session::models_config).
    using ModelsConfig = std::function<Result<Config>(std::string_view provider)>;

    ModelPicker(ModelsConfig models_config, ModelsLoader::TransportFactory make_transport,
                ModelsLoader::Post post);

    /// Abre en el paso de proveedor con choices (Session::provider_choices)
    /// o, con provider, directo en el de modelo de ese proveedor (Esc ahí
    /// cierra). current_model: el que se usa ahora (se marca en la lista de
    /// su proveedor). Devuelve Status si provider no se puede elegir (y no
    /// abre); si no, Handled.
    [[nodiscard]] ModelPickerAction open(std::vector<ProviderChoice> choices,
                                         const std::optional<std::string>& provider,
                                         std::string current_model);
    /// Cierra y cancela la lista en curso.
    void close();
    /// Tras Session::switch_model de un Switch: con Switched o Unchanged
    /// cierra (la interfaz vacía la caja). Devuelve el aviso para la línea de
    /// estado (Unchanged o el error; con un error sigue abierto), o nullopt.
    [[nodiscard]] std::optional<std::string> finish(const ModelSwitch& change);

    [[nodiscard]] bool is_open() const { return open_; }
    [[nodiscard]] bool model_step() const { return model_step_; }
    /// Esc en el paso de modelo regresa al de proveedor (se llegó desde ahí).
    [[nodiscard]] bool can_go_back() const { return model_step_ && from_providers_; }
    /// Nombre del proveedor del paso de modelo.
    [[nodiscard]] const std::string& provider_name() const { return provider_name_; }
    [[nodiscard]] ModelsState models_state() const { return models_state_; }
    /// El error de la lista (models_error_text), para la línea de estado; o "".
    [[nodiscard]] const std::string& models_error() const { return models_error_; }

    /// El texto de la caja es el filtro; se llama antes de cada tecla y al dibujar.
    void update(std::string_view text);

    /// Todas las filas del paso, filtradas, en orden.
    [[nodiscard]] std::vector<ModelPickerRow> rows() const;
    /// Fila del cursor en rows() (0 si no hay ninguna elegible).
    [[nodiscard]] std::size_t cursor() const { return cursor_; }
    /// Primera fila visible: [first(), first() + kVisibleRows) contiene al cursor.
    [[nodiscard]] std::size_t first() const { return first_; }

    /// Mueve el cursor a la fila row (si existe y tiene cursor). Para el ratón.
    void select(std::size_t row);
    /// Clic en la fila row: lo mismo que Enter con el cursor en ella.
    [[nodiscard]] ModelPickerAction choose(std::size_t row);
    /// Rueda: mueve el cursor delta filas, sin dar la vuelta.
    void scroll(int delta);

    /// Con el selector abierto (cerrado: Pass):
    /// - ↑/↓ mueven el cursor y dan la vuelta; Home/End y PgUp/PgDn van a
    ///   la primera o a la última fila.
    /// - Enter elige la fila del cursor.
    /// - Esc: en el paso de modelo, regresa al de proveedor si se llegó desde
    ///   ahí (ClearInput); si no, cierra (Closed).
    /// - Tab no hace nada (Handled); lo demás es Pass.
    [[nodiscard]] ModelPickerAction handle(ListKey key);

private:
    [[nodiscard]] ModelPickerAction enter_model_step(const ProviderChoice& choice);
    void back_to_providers();
    [[nodiscard]] static bool has_cursor(const ModelPickerRow& row);
    /// Deja el cursor en una fila con cursor y la ventana a su alrededor.
    void settle(std::size_t preferred);

    ModelsConfig models_config_;
    ModelsLoader loader_;

    bool open_ = false;
    bool model_step_ = false;
    bool from_providers_ = false; ///< Al paso de modelo se llegó desde el de proveedor.
    std::vector<ProviderChoice> choices_;
    std::string current_model_;
    std::string filter_;
    std::size_t cursor_ = 0;
    std::size_t first_ = 0;

    std::string provider_;      ///< Id del paso de modelo.
    std::string provider_name_; ///< Nombre del paso de modelo.
    bool provider_current_ = false;
    ModelsState models_state_ = ModelsState::Idle;
    std::vector<std::string> models_;
    std::string models_error_;
};

} // namespace chatbot::cli

#endif // CHATBOT_CLI_MODEL_PICKER_H
