# libcurl. Linux: la del sistema. Windows: siempre por FetchContent, estática y
# mínima (AGENTS.md §4, guias/04-stack.md): solo HTTP y HTTPS, TLS de Windows
# (Schannel, con el almacén de certificados del sistema), sin OpenSSL, HTTP/2,
# compresión, IDN, PSL ni SSH. Así moss.exe y moss-rpc.exe no llevan DLL de
# terceros. Los proxys de HTTP(S)_PROXY siguen activos, como con la del sistema.
# Estas variables solo valen dentro de core/ (y de curl, que se agrega aquí).
if(WIN32)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_STATIC_LIBS ON)
    set(BUILD_CURL_EXE OFF)
    set(BUILD_TESTING OFF)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_LIBCURL_DOCS OFF)
    set(BUILD_MISC_DOCS OFF)
    set(ENABLE_CURL_MANUAL OFF)
    set(CURL_DISABLE_INSTALL ON)
    set(CURL_USE_PKGCONFIG OFF)
    set(CURL_USE_SCHANNEL ON)
    set(CURL_USE_OPENSSL OFF)
    set(USE_NGHTTP2 OFF)
    # CURL_ZLIB, CURL_BROTLI y CURL_ZSTD son set(... CACHE STRING "AUTO") en curl,
    # que pide CMake 3.18: sin CMP0126 en NEW, ese set borra estos OFF y curl
    # enlaza las DLL de MSYS2 que encuentre.
    set(CMAKE_POLICY_DEFAULT_CMP0126 NEW)
    set(CURL_ZLIB OFF)
    set(CURL_BROTLI OFF)
    set(CURL_ZSTD OFF)
    set(USE_LIBIDN2 OFF)
    set(CURL_USE_LIBPSL OFF)
    set(CURL_USE_LIBSSH2 OFF)
    # curl 8.23 ya no tiene HTTP_ONLY: se apaga cada protocolo que no es HTTP(S).
    foreach(protocol DICT FILE FTP GOPHER IMAP IPFS LDAP LDAPS MQTT POP3 RTSP SMTP TELNET TFTP
                     WEBSOCKETS)
        set(CURL_DISABLE_${protocol} ON)
    endforeach()
    include(FetchContent)
    FetchContent_Declare(
        curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG curl-8_23_0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(curl)
else()
    find_package(CURL REQUIRED)
endif()
find_package(nlohmann_json 3.9 REQUIRED)

# Capa de plataforma (chatbot/platform.h): CMake elige la implementación.
if(WIN32)
    set(CHATBOT_PLATFORM_SOURCE src/platform_windows.cpp)
else()
    set(CHATBOT_PLATFORM_SOURCE src/platform_posix.cpp)
endif()

add_library(chatbot_core STATIC
    ${CHATBOT_PLATFORM_SOURCE}
    src/platform_common.cpp
    src/cancel_token.cpp
    src/credentials.cpp
    src/error.cpp
    src/error_body.cpp
    src/history.cpp
    src/model_params.cpp
    src/providers.cpp
    src/published_date.cpp
    src/redirect.cpp
    src/search_context.cpp
    src/config.cpp
    src/sleeper.cpp
    src/sse.cpp
    src/system_prompt.cpp
    src/curl_transport.cpp
    src/chat_client.cpp
    src/url.cpp
    src/utf8.cpp
    src/web_search.cpp
    src/tavily_search.cpp
    src/trimmed_completion.cpp)

add_library(chatbot::core ALIAS chatbot_core)

target_compile_features(chatbot_core PUBLIC cxx_std_20)

target_include_directories(chatbot_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

chatbot_set_warnings(chatbot_core)

# libcurl y nlohmann/json son detalles de implementación: no aparecen en los
# headers públicos, así que se enlazan en PRIVATE.
target_link_libraries(chatbot_core PRIVATE CURL::libcurl nlohmann_json::nlohmann_json)
# Windows: SHGetKnownFolderPath (shell32) y CoTaskMemFree (ole32). Son
# bibliotecas del sistema, no dependencias nuevas.
if(WIN32)
    target_link_libraries(chatbot_core PRIVATE shell32 ole32)
endif()
