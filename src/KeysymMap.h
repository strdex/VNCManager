#pragma once

#include <Qt>

class QKeyEvent;

// QKeyEvent'i RFB istemcisinin bekledigi X11 keysym koduna cevirir.
// Tanimsiz bir tusa basilmissa 0 doner.
quint32 keysymForKeyEvent(const QKeyEvent* event);

// X11/keysymdef.h icindeki sabitlerden yaygin olanlar; menu/ardis tuslarini
// programatik olarak gonderirken kullanilir.
namespace X11Keysym {

constexpr quint32 Control_L = 0xffe3;
constexpr quint32 Control_R = 0xffe4;
constexpr quint32 Alt_L     = 0xffe9;
constexpr quint32 Alt_R     = 0xffea;
constexpr quint32 Super_L   = 0xffeb;
constexpr quint32 Super_R   = 0xffec;
constexpr quint32 Delete    = 0xffff;
constexpr quint32 End       = 0xff50;

} // namespace X11Keysym
