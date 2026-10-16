cmake_minimum_required(VERSION 3.21)

# --- Surum: tek kaynak kokekteki VERSION dosyasi (0.99.x) --------------------
# Derleme sonunda build betikleri sayiyi 1 artirir; exe ozellikleri ve
# QCoreApplication::applicationVersion bundan beslenir.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" XVNCRM_VERSION_FILE)
string(STRIP "${XVNCRM_VERSION_FILE}" XVNCRM_VERSION_FILE)
string(REPLACE "." ";" _xvn_vers "${XVNCRM_VERSION_FILE}")
list(GET _xvn_vers 0 XVNCRM_VER_MAJOR)
list(GET _xvn_vers 1 XVNCRM_VER_MINOR)
list(GET _xvn_vers 2 XVNCRM_VER_PATCH)

project(xVNCManager
    VERSION ${XVNCRM_VER_MAJOR}.${XVNCRM_VER_MINOR}.${XVNCRM_VER_PATCH}
    LANGUAGES CXX
)

if(WIN32)
    enable_language(RC)   # resources/app.rc (ikon) + app_version.rc (surum)
endif()

# VERSION degisince derleme otomatik yeniden yapilandirilir.
set_property(DIRECTORY APPEND PROPERTY
             CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

# Exe surum bilgisi kaynagi (configure_file).
set(XVNCRM_RC_COMMA "${XVNCRM_VER_MAJOR},${XVNCRM_VER_MINOR},${XVNCRM_VER_PATCH},0")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/app_version.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/app_version.rc"
    @ONLY
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# --- libvncclient (vcpkg ile kurulan overlay port) ---------------------------
find_path(RFB_INCLUDE_DIR NAMES rfb/rfbclient.h)
find_library(RFBCLIENT_LIBRARY NAMES vncclient)
find_package(ZLIB REQUIRED)
find_package(JPEG REQUIRED)

if(NOT RFB_INCLUDE_DIR OR NOT RFBCLIENT_LIBRARY)
    message(FATAL_ERROR
        "libvncclient bulunamadi. Once vcpkg ile bagimliliklari kurun:\n"
        "  vcpkg install --triplet x64-windows --overlay-ports=overlay-ports")
endif()

# ---------------------------------------------------------------------------
set(SOURCES
    src/main.cpp

    src/ConnectionProfile.h

    src/SecretStore.h
    src/SecretStore.cpp

    src/ConnectionStore.h
    src/ConnectionStore.cpp

    src/KeysymMap.h
    src/KeysymMap.cpp

    src/VncSession.h
    src/VncSession.cpp

    src/VncView.h
    src/VncView.cpp

    src/ConnectionDialog.h
    src/ConnectionDialog.cpp

    src/ConnectionInfoDialog.h
    src/ConnectionInfoDialog.cpp

    src/FileTransferDialog.h
    src/FileTransferDialog.cpp
    src/FtProtocol.h

    src/MainWindow.h
    src/MainWindow.cpp

    # Arac cubugu ikonlari (AUTORCC)
    resources/resources.qrc

    # Uygulama ikonu (rc.exe app.ico'yu resources/ altindan bulsun)
    resources/app.rc

    # Exe surum bilgisi: VERSION dosyasindan uretilir (app_version.rc)
    ${CMAKE_CURRENT_BINARY_DIR}/app_version.rc
)

set_source_files_properties(resources/app.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico"
    COMPILE_OPTIONS "/I${CMAKE_CURRENT_SOURCE_DIR}/resources"
)

qt_add_executable(xVNCManager WIN32 ${SOURCES})

target_include_directories(xVNCManager PRIVATE src ${RFB_INCLUDE_DIR})

# Uygulama ici surum metni (QCoreApplication::applicationVersion)
target_compile_definitions(xVNCManager PRIVATE XVNCRM_VERSION="${PROJECT_VERSION}")

target_link_libraries(xVNCManager PRIVATE
    Qt6::Widgets
    ${RFBCLIENT_LIBRARY}
    ZLIB::ZLIB
    JPEG::JPEG
    crypt32
    ws2_32
    dbghelp
)

if(MSVC)
    target_compile_options(xVNCManager PRIVATE /W4 /utf-8 /permissive- /Zc:__cplusplus)
    target_compile_definitions(xVNCManager PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

# Statik Qt kurulumunda DLL dagitimi gerekmez (her sey exe'nin icinde).
get_target_property(XVNCRM_QT_CORE_TYPE Qt6::Core TYPE)
if(XVNCRM_QT_CORE_TYPE STREQUAL "STATIC_LIBRARY")
    message(STATUS "Statik Qt algilandi: ek dagitim adimi yok (tek exe).")
    # Statik Qt'de yalnizca platform eklentisi otomatik baglanir; ikon (qico,
    # qsvgicon) ve stil (qmodernwindowsstyle) eklentileri elle dahil edilir.
    qt_import_plugins(xVNCManager
        INCLUDE
            Qt6::QWindowsIntegrationPlugin
            Qt6::QICOPlugin
            Qt6::QSvgIconPlugin
            Qt6::QModernWindowsStylePlugin
    )
    return()
endif()

# Derleme sonrasi Qt DLL'lerini ciktinin yanina kopyalar (kucuk ayak izi):
#  - vc_redist.x64.exe (24 MB) yerine MSVC calisma zamani DLL'leri (~1.5 MB)
#  - ANGLE (dxcompiler/dxil/D3Dcompiler_47, ~18 MB) atlanir: yalnizca OpenGL ES
#    / Qt Quick icin gereklidir; bu uygulama Widgets ile raster cizim yapar.
#  - Qt6Network + generic/networkinformation/tls eklentileri (~2.2 MB) gereksiz:
#    ag trafiğini libvncclient'in soketleri tasir.
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/../../../bin")
if(WINDEPLOYQT_EXECUTABLE)
    # MSVC calisma zamani DLL'leri: once VC redist klasoru, yoksa arac dizini.
    set(XVNCRM_CRT_DLLS "")
    if(DEFINED ENV{VCToolsRedistDir})
        file(GLOB XVNCRM_CRT_DLLS "$ENV{VCToolsRedistDir}x64/Microsoft.VC14*.CRT/*.dll")
    endif()
    if(NOT XVNCRM_CRT_DLLS AND DEFINED ENV{VCToolsInstallDir})
        file(GLOB _crt_msvcp "$ENV{VCToolsInstallDir}bin/Hostx64/x64/msvcp140*.dll")
        file(GLOB _crt_vcrun "$ENV{VCToolsInstallDir}bin/Hostx64/x64/vcruntime140*.dll")
        set(XVNCRM_CRT_DLLS ${_crt_msvcp} ${_crt_vcrun})
    endif()
    if(XVNCRM_CRT_DLLS)
        set(XVNCRM_RT_FLAG --no-compiler-runtime)
    else()
        set(XVNCRM_RT_FLAG "")   # CRT bulunamazsa eski davranis (vc_redist) korunur
    endif()

    add_custom_command(TARGET xVNCManager POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                $<IF:$<CONFIG:Debug>,--debug,--release>
                --no-translations
                --skip-plugin-types generic,networkinformation,tls
                --exclude-plugins qgif,qjpeg,qsvg
                ${XVNCRM_RT_FLAG}
                $<TARGET_FILE:xVNCManager>
        # Onceki kurulumlardan kalabilecek gereksiz dosyalari da temizle.
        COMMAND "${CMAKE_COMMAND}" -E rm -f
                "$<TARGET_FILE_DIR:xVNCManager>/dxcompiler.dll"
                "$<TARGET_FILE_DIR:xVNCManager>/dxil.dll"
                "$<TARGET_FILE_DIR:xVNCManager>/D3Dcompiler_47.dll"
                "$<TARGET_FILE_DIR:xVNCManager>/vc_redist.x64.exe"
                "$<TARGET_FILE_DIR:xVNCManager>/Qt6Network.dll"
                "$<TARGET_FILE_DIR:xVNCManager>/generic/qtuiotouchplugin.dll"
                "$<TARGET_FILE_DIR:xVNCManager>/networkinformation/qnetworklistmanager.dll"
                "$<TARGET_FILE_DIR:xVNCManager>/tls/qcertonlybackend.dll"
                "$<TARGET_FILE_DIR:xVNCManager>/tls/qschannelbackend.dll"
                "$<TARGET_FILE_DIR:xVNCManager>/imageformats/qgif.dll"
                "$<TARGET_FILE_DIR:xVNCManager>/imageformats/qjpeg.dll"
                "$<TARGET_FILE_DIR:xVNCManager>/imageformats/qsvg.dll"
        COMMENT "Qt DLL dagitiliyor (windeployqt)"
    )
    if(XVNCRM_CRT_DLLS)
        add_custom_command(TARGET xVNCManager POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${XVNCRM_CRT_DLLS}
                    "$<TARGET_FILE_DIR:xVNCManager>"
            COMMENT "MSVC calisma zamani DLL'leri kopyalanıyor")
    endif()
endif()
