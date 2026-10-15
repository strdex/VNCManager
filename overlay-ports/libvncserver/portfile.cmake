vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO LibVNC/libvncserver
    REF    98aaee0560baa0332339b2ef464ea96ed9b9b28c
    SHA512 6943fbe56f4d3861d8cba516bc38f4168924f73c36072e489ac324b230a78a8ed1009e676ae9db55706f9bcc0a166a33a46b82eef72d9d1b5d7183d808eb17d4
    HEAD_REF master
    PATCHES
        # Guvenlik tipi seciminde uzanti (rfbClientProtocolExtension)
        # tarafindan saglanan tipler yerlesiklere tercih edilir; boylece
        # TightVNC sunucularinda tip 16 (Tight) secilip dosya transferi
        # (0xFC alt protokolu) aktif hale getirilebilir.
        prefer-extension-security-types.patch
)

# Kutuphane her zaman statik derleniyor: CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS
# degisken (data) sembollerini export edemedigi icin rfbClientLog gibi isimler
# DLL'de gorunmez kaliyor.
set(VCPKG_LIBRARY_LINKAGE static)

# Yalnizca istemci kutuphanesi (libvncclient) derleniyor; sunucu tarafina,
# orneklerine, testlerine ve diger opsiyonel bagimliliklara ihtiyacimiz yok.
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DWITH_LIBVNCSERVER=OFF
        -DWITH_LIBVNCCLIENT=ON
        -DWITH_ZLIB=ON
        -DWITH_JPEG=ON
        -DWITH_LZO=OFF
        -DWITH_PNG=OFF
        -DWITH_SDL=OFF
        -DWITH_GTK=OFF
        -DWITH_QT=OFF
        -DWITH_FFMPEG=OFF
        -DWITH_LIBSSHTUNNEL=OFF
        -DWITH_THREADS=ON
        -DWITH_GNUTLS=OFF
        -DWITH_OPENSSL=OFF
        -DWITH_GCRYPT=OFF
        -DWITH_SYSTEMD=OFF
        -DWITH_SASL=OFF
        -DWITH_TIGHTVNC_FILETRANSFER=OFF
        -DWITH_IPv6=ON
        -DWITH_WEBSOCKETS=OFF
        -DWITH_24BPP=ON
        -DWITH_EXAMPLES=OFF
        -DWITH_TESTS=OFF
        -DLIBVNCSERVER_INSTALL=ON
        # upstream CMakeLists bir yazim hatasi nedeniyle bu degiskeni bekliyor
        -DPARA_VERSION=0.9.15
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()

# Uyari susturuculari (upstream kurulum yapisi vcpkg kurallarina uymuyor)
set(VCPKG_POLICY_ALLOW_DEBUG_INCLUDE enabled)
set(VCPKG_POLICY_SKIP_MISPLACED_CMAKE_FILES_CHECK enabled)
set(VCPKG_POLICY_SKIP_LIB_CMAKE_MERGE_CHECK enabled)
set(VCPKG_POLICY_SKIP_ABSOLUTE_PATHS_CHECK enabled)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig"
    "${CURRENT_PACKAGES_DIR}/lib/pkgconfig"
)
