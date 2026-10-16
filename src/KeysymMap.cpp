#include "KeysymMap.h"

#include <QKeyEvent>
#include <QString>

#include <rfb/keysym.h>

namespace {

// Windows tuş fiziksel konumundan sag/sol ayrimi yapar.
bool isRightModifier(const QKeyEvent* event)
{
    // Windows tuş basma olaylarinda nativeScanCode sol icin 0xXX, sag icin 0xE0XX verir.
    return (event->nativeScanCode() & 0xE000u) != 0;
}

quint32 modifierKeysym(const QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Shift:    return isRightModifier(event) ? XK_Shift_R : XK_Shift_L;
    case Qt::Key_Control:  return isRightModifier(event) ? XK_Control_R : XK_Control_L;
    case Qt::Key_Alt:      return isRightModifier(event) ? XK_Alt_R : XK_Alt_L;
    case Qt::Key_Meta:     return isRightModifier(event) ? XK_Super_R : XK_Super_L;
    default:               return 0;
    }
}

quint32 specialKeysym(const QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Escape:     return XK_Escape;
    case Qt::Key_Tab:
        return event->modifiers().testFlag(Qt::ShiftModifier) ? XK_ISO_Left_Tab : XK_Tab;
    case Qt::Key_Backspace:  return XK_BackSpace;
    case Qt::Key_Return:
    case Qt::Key_Enter:      return XK_Return;
    case Qt::Key_Insert:     return XK_Insert;
    case Qt::Key_Delete:     return XK_Delete;
    case Qt::Key_Home:       return XK_Home;
    case Qt::Key_End:        return XK_End;
    case Qt::Key_PageUp:     return XK_Prior;
    case Qt::Key_PageDown:   return XK_Next;
    case Qt::Key_Left:       return XK_Left;
    case Qt::Key_Right:      return XK_Right;
    case Qt::Key_Up:         return XK_Up;
    case Qt::Key_Down:       return XK_Down;
    case Qt::Key_CapsLock:   return XK_Caps_Lock;
    case Qt::Key_NumLock:    return XK_Num_Lock;
    case Qt::Key_ScrollLock: return XK_Scroll_Lock;
    case Qt::Key_Print:      return XK_Print;
    case Qt::Key_Pause:      return XK_Pause;
    case Qt::Key_Menu:       return XK_Menu;
    default:                 return 0;
    }
}

quint32 functionKeysym(const QKeyEvent* event)
{
    const int key = event->key();
    if (key >= Qt::Key_F1 && key <= Qt::Key_F35)
        return XK_F1 + static_cast<quint32>(key - Qt::Key_F1);
    return 0;
}

} // namespace

quint32 keysymForKeyEvent(const QKeyEvent* event)
{
    if (!event)
        return 0;

    if (quint32 keysym = modifierKeysym(event))
        return keysym;
    if (quint32 keysym = specialKeysym(event))
        return keysym;
    if (quint32 keysym = functionKeysym(event))
        return keysym;

    // Yazilabilir karakterler: Unicode kod noktasi dogrudan keysym olur.
    const QString text = event->text();
    if (text.isEmpty())
        return 0;

    const QList<uint> ucs4 = text.toUcs4();
    if (ucs4.isEmpty())
        return 0;

    const uint codePoint = ucs4.first();
    if (codePoint < 0x20 || codePoint == 0x7F) {
        // Ctrl+<tus> kombinasyonlarinda text() kontrol karakteri doner
        // (orn. Ctrl+C -> 0x03). Ctrl tusu ayri bir keysym olarak
        // gonderildigi icin temel tusun keysym'i iletilmelidir; aksi halde
        // Ctrl+C, Ctrl+V, Ctrl+X, Ctrl+A, Ctrl+Z gibi kombinasyonlar
        // sunucuya hic ulasmaz.
        const int key = event->key();
        if (key >= Qt::Key_A && key <= Qt::Key_Z)
            return static_cast<quint32>(key - Qt::Key_A) + 'a'; // kucuk harf keysym
        if (key >= 0x20 && key <= 0x7E)
            return static_cast<quint32>(key);                   // rakam/noktalama/bosluk
        return 0;
    }

    if (codePoint <= 0xFF)
        return codePoint;        // Latin-1 keysym'leri ayni degerdedir
    return 0x01000000u | codePoint; // Unicode keysym
}
