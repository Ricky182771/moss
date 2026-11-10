#include "flutter_window.h"

#include <dwmapi.h>
#include <flutter/standard_method_codec.h>

#include <optional>
#include <variant>

#include "flutter/generated_plugin_registrant.h"

// Por si el SDK es anterior a 10.0.22000.0, igual que en win32_window.cpp.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace {

// Si no se puede leer, encendidos: es lo que hace la app sin este ajuste.
bool SystemAnimationsEnabled() {
  BOOL enabled = TRUE;
  return !SystemParametersInfo(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) ||
         enabled != FALSE;
}

}  // namespace

FlutterWindow::FlutterWindow(const flutter::DartProject& project)
    : project_(project) {}

FlutterWindow::~FlutterWindow() {}

bool FlutterWindow::OnCreate() {
  if (!Win32Window::OnCreate()) {
    return false;
  }

  RECT frame = GetClientArea();

  // The size here must match the window dimensions to avoid unnecessary surface
  // creation / destruction in the startup path.
  flutter_controller_ = std::make_unique<flutter::FlutterViewController>(
      frame.right - frame.left, frame.bottom - frame.top, project_);
  // Ensure that basic setup of the controller was successful.
  if (!flutter_controller_->engine() || !flutter_controller_->view()) {
    return false;
  }
  RegisterPlugins(flutter_controller_->engine());
  SetChildContent(flutter_controller_->view()->GetNativeWindow());

  window_channel_ = std::make_unique<flutter::MethodChannel<>>(
      flutter_controller_->engine()->messenger(), "moss/window",
      &flutter::StandardMethodCodec::GetInstance());
  window_channel_->SetMethodCallHandler(
      [this](const flutter::MethodCall<>& call,
             std::unique_ptr<flutter::MethodResult<>> result) {
        if (call.method_name() == "animationsEnabled") {
          animations_enabled_ = SystemAnimationsEnabled();
          result->Success(flutter::EncodableValue(animations_enabled_));
          return;
        }
        const bool* dark = call.arguments() != nullptr
                               ? std::get_if<bool>(call.arguments())
                               : nullptr;
        if (call.method_name() != "setDark" || dark == nullptr) {
          result->NotImplemented();
          return;
        }
        SetDarkTitleBar(*dark);
        result->Success();
      });

  flutter_controller_->engine()->SetNextFrameCallback([&]() {
    this->Show();
  });

  // Flutter can complete the first frame before the "show window" callback is
  // registered. The following call ensures a frame is pending to ensure the
  // window is shown. It is a no-op if the first frame hasn't completed yet.
  flutter_controller_->ForceRedraw();

  return true;
}

void FlutterWindow::SetDarkTitleBar(bool dark) {
  const HWND hwnd = GetHandle();
  if (hwnd == nullptr) {
    return;
  }
  app_sets_theme_ = true;
  const BOOL value = dark ? TRUE : FALSE;
  // Antes de Windows 10 20H1 no existe: devuelve un error y la barra se queda.
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value,
                        sizeof(value));
  // Windows 10 no repinta la barra hasta que cambia la activación.
  if (IsWindowVisible(hwnd)) {
    const bool active = GetForegroundWindow() == hwnd;
    DefWindowProc(hwnd, WM_NCACTIVATE, !active, 0);
    DefWindowProc(hwnd, WM_NCACTIVATE, active, 0);
  }
}

void FlutterWindow::OnDestroy() {
  window_channel_ = nullptr;
  if (flutter_controller_) {
    flutter_controller_ = nullptr;
  }

  Win32Window::OnDestroy();
}

LRESULT
FlutterWindow::MessageHandler(HWND hwnd, UINT const message,
                              WPARAM const wparam,
                              LPARAM const lparam) noexcept {
  // Give Flutter, including plugins, an opportunity to handle window messages.
  if (flutter_controller_) {
    std::optional<LRESULT> result =
        flutter_controller_->HandleTopLevelWindowProc(hwnd, message, wparam,
                                                      lparam);
    if (result) {
      return *result;
    }
  }

  switch (message) {
    case WM_FONTCHANGE:
      flutter_controller_->engine()->ReloadSystemFonts();
      break;
    case WM_SETTINGCHANGE:
      if (window_channel_ &&
          animations_enabled_ != SystemAnimationsEnabled()) {
        animations_enabled_ = !animations_enabled_;
        window_channel_->InvokeMethod(
            "animationsEnabled",
            std::make_unique<flutter::EncodableValue>(animations_enabled_));
      }
      break;
    case WM_DWMCOLORIZATIONCOLORCHANGED:
      // Win32Window pondría el tema del sistema; manda el de la app.
      if (app_sets_theme_) {
        return 0;
      }
      break;
    case WM_GETMINMAXINFO: {
      // El mínimo es del contenido y en píxeles lógicos; Windows lo pide de la
      // ventana entera (con marco y barra) y en físicos, con el DPI del monitor
      // donde está: al cambiar de monitor, WM_DPICHANGED la mide otra vez.
      const UINT window_dpi = GetDpiForWindow(hwnd);  // 0 si hwnd no es válido.
      const UINT dpi = window_dpi != 0 ? window_dpi : USER_DEFAULT_SCREEN_DPI;
      RECT frame{0, 0, MulDiv(720, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI),
                 MulDiv(520, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI)};
      AdjustWindowRectExForDpi(
          &frame, static_cast<DWORD>(GetWindowLong(hwnd, GWL_STYLE)), FALSE,
          static_cast<DWORD>(GetWindowLong(hwnd, GWL_EXSTYLE)), dpi);
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      info->ptMinTrackSize.x = frame.right - frame.left;
      info->ptMinTrackSize.y = frame.bottom - frame.top;
      return 0;
    }
  }

  return Win32Window::MessageHandler(hwnd, message, wparam, lparam);
}
