# Dosya transferi — TightVNC 2.x kablosal protokolu (0xFC alt protokolu)

**Amaç:** bu belge, xVNCManager'in gerçekte konuştuğu dosya transferi protokolünü
belgeler: TightVNC 2.x sunucularına karşı çalışan, libvncclient
üzerine kurulmuş istemci tarafı uygulama.

**Kaynak ağaç (salt okunur referans; bütün alıntılar buna atıftır):**

```
C:\Users\sahinakin\AppData\Local\Temp\opencode\tightvnc-src\tightvnc-2.8.63-gpl
```

Alıntılar `path:line` biçimindedir. Davranış iddiaları bu ağaçla veya uygulama
koduyla doğrulanır; doğrulanamayanlar **DOĞRULANMADI** olarak işaretlenir.

**Uygulama kaynakları:**

| Dosya | Rol |
|---|---|
| `src/FtProtocol.h` | mesaj kimlikleri, istek yapıcıları, cevap ayrıştırıcıları, yol dönüşümleri |
| `src/VncSession.cpp/.h` | uzantı kaydı, 0xFC dağıtımı, durum makinesi (`FtState`), yetenek tespiti |
| `src/FileTransferDialog.cpp/.h` | çift panel UI, gezinme, hata çevirileri, silme onayları |
| `overlay-ports/libvncserver/portfile.cmake` | yamalı libvncclient (uzantı güvenlik tipleri tercihi) |

**Eski belge:** tip-7 (UltraVNC/LibVNCServer) protokolüne dair ayrıntılı araştırma
`docs/filetransfer-protocol-type7-legacy.md` dosyasında saklanmıştır. **xVNCManager o
protokolü KULLANMAZ** — TightVNC 2.x ile açıkça uyumsuzdur
(`examples\server\filetransfer.c:4`: *"TightVNC-2.x uses a different, incompatible file
transfer protocol"*).

---

## 0. Hızlı referans

### Mesaj kimlikleri (4 bayt, BE; `ft-common/FTMessage.h:125-234`)

| ID | Yön | Ad | Gövde özeti |
|---|---|---|---|
| `0xFC000100` | c→s | CompressionSupportRequest | yok |
| `0xFC000101` | s→c | CompressionSupportReply | `u8` (1=destekliyor) |
| `0xFC000102` | c→s | FileListRequest | `u8 seviye` + `StringUTF8 yol` |
| `0xFC000103` | s→c | FileListReply | CompressedBlock (liste) |
| `0xFC000104` | c→s | MD5Request | `StringUTF8 yol` + `u64 offset` + `u64 boyut` — **uygulamada kullanılmıyor** |
| `0xFC000105` | s→c | MD5Reply | `u8[16]` — kullanılmıyor |
| `0xFC000106` | c→s | UploadStartRequest | `StringUTF8 yol` + `u8 bayrak` + `u64 ofset` |
| `0xFC000107` | s→c | UploadStartReply | yok |
| `0xFC000108` | c→s | UploadDataRequest | `u8 seviye` + `u32 sıkı` + `u32 ham` + veri |
| `0xFC000109` | s→c | UploadDataReply | yok (ack) |
| `0xFC00010A` | c→s | UploadEndRequest | `u16 dosyaBayrakları` + `u64 modZaman(ms)` |
| `0xFC00010B` | s→c | UploadEndReply | yok |
| `0xFC00010C` | c→s | DownloadStartRequest | `StringUTF8 yol` + `u64 ofset` |
| `0xFC00010D` | s→c | DownloadStartReply | yok |
| `0xFC00010E` | c→s | DownloadDataRequest | `u8 seviye` + `u32 istenenBoyut` |
| `0xFC00010F` | s→c | DownloadDataReply | CompressedBlock (dosya verisi) |
| `0xFC000110` | s→c | DownloadEndReply | `u8 dosyaBayrakları` + `u64 modZaman(ms)` |
| `0xFC000111` | c→s | MkdirRequest | `StringUTF8 yol` |
| `0xFC000112` | s→c | MkdirReply | yok |
| `0xFC000113` | c→s | RemoveRequest | `StringUTF8 yol` |
| `0xFC000114` | s→c | RemoveReply | yok |
| `0xFC000115` | c→s | RenameRequest | `StringUTF8 eskiYol` + `StringUTF8 yeniYol` |
| `0xFC000116` | s→c | RenameReply | yok |
| `0xFC000117` | c→s | DirSizeRequest | `StringUTF8 yol` |
| `0xFC000118` | s→c | DirSizeReply | `u64 klasörBoyutu` |
| `0xFC000119` | s→c | LastRequestFailedReply | `StringUTF8 açıklama` — beklenecek cevabı **yerine** gelir |

### Sabitler (uygulama: `src/FtProtocol.h`)

| Sabit | Değer | Not |
|---|---|---|
| `Ft::FileFlags.FlagDirectory` | `0x1` | klasör |
| `Ft::FileFlags.FlagExe` | `0x2` | çalıştırılabilir (`0x1` ile birlikte set edilmez, `FTMessage.h:81-85`) |
| `Ft::ChunkSize` | `65536` | indirme isteği ve yükleme okuma tamponu (her iki yön) |
| `Ft::MaxBlockBytes` | 64 MiB | akışı bozmamak için okuma tavanı |
| `Ft::MaxErrorBytes` | 64 KiB | hata metni tavanı |
| Son yanıt bekleme süresi | 30000 ms | `VncSession.cpp:62` (`m_ftDeadline`) |

### Dosya bayrakları ve zaman

- `fileFlags`: `u16` (bkz. §3 `FileListReply` uyumluluk notu); `0x1` klasör, `0x2` exe.
- Değişim zamanı: Unix epoch'dan itibaren **milisaniye** (`FTMessage.h:76-77`).

---

## 1. Çerçeveleme ve bayt düzeni

### 1.1 0xFC mesaj kimliği kuralı

`FTMessage.h:40-45` (readme):

```
c→s: UINT32 messageId; UINT8[] messageBody
s→c: UINT32 messageId; UINT8[] messageBody
@remark rfb dispatcher genelde ilk bayti (UINT8) mesaj kimliği olarak okur;
ama ilk bayit 0xFC ise bu bir TightVNC uzantisidir ve UINT32 kimligi icin
kalan 3 bayt okunmalidir.
```

⇒ Hem istemci hem sunucu: ilk bayt `0xFC` (252) görülürse kalan 3 bayt okunup
**büyük sonlandirimli UINT32** kimlik oluşturulur. Kimlik değerleri hep
`0xFC0001xx` aralığındadır.

libvncclient'ın bilinmeyen tip davranışı: `HandleRFBServerMessage` yalnızca
1 bayt okuyup `default` dalına düşer; uzantı hiç ele almazsa kütüphane 256 bayt
"boşaltma" yapar ve olay döngüsünü `FALSE` ile bitirir (öldürür). Bu yüzden
`0xFC` uzantısı **zorunludur** (bkz. eski belge §4.1, aynı mekanizma).

### 1.2 Uygulamadaki dağıtım (`src/VncSession.cpp`)

```cpp
// satir ~87-94
rfbClientProtocolExtension g_ftExtension = {
    nullptr,                // encodings (pseudo-encoding yok)
    nullptr,                // handleEncoding
    ftMessageCallback,      // handleMessage — 0xFC'yi iddia eder
    nullptr,                // next
    g_tightSecurityTypes,   // securityTypes = { 16 }
    tightAuthCallback,      // handleAuthentication (Tight el sıkışması)
};
```

- Kayıt: `std::call_once(... rfbClientRegisterExtension(&g_ftExtension) ...)`
  (`VncSession.cpp:212`) — **`rfbInitClient`'dan önce**, global LIFO listede bir kez.
- `ftMessageCallback` (`VncSession.cpp:98-116`): `message->type != 0xFC` → `FALSE`
  (diğer tipler normal akışa). `0xFC` ise kalan **3 bayt** okunur, BE UINT32 kimlik
  oluşturulur, `handleTightFtMessage(client, messageId)` çağrılır. Okuma hatasında
  bile `TRUE` döner (`FALSE` dönmek çağrımcının olay döngüsünü öldürür).
- Gövde okumaları `handleTightFtMessage` içinde, kimliğe göre `ReadFromRFBServer`
  ile tam olarak yapılır; gönderimler tek `WriteToRFBServer` çağrısıyla
  (`writeFt`) — ara boşluk kalmaz.

### 1.3 Bayt düzeni

Tüm çok baytlı alanlar **büyük sonlandirimli** (RFB ağ sırası). Sunucu tarafı
`io-lib/DataOutputStream.cpp` `writeUIntXX` + `DataInputStream.cpp`
`readUIntXX` kullanır; uygulama `Ft::putU*` / `Ft::getBe*` (`FtProtocol.h:80-123`)
ile aynı düzeni üretir/okur. Uygulamada tek istisnasız kural: **tek baytlık alanlar
hamdir** (`u8 seviye`, `u8 bayrak`).

### 1.4 StringUTF8 ve **NUL sonlandırıcı tuhaflığı** (kritik)

Tanım (`FTMessage.h:56-58`):

```
struct StringUTF8 {
  UINT32 size;        // buffer bayt cinsinden boyutu
  UINT8 buffer[size]; // UTF-8 veri
}
```

**Gerçek:** sunucu (ve resmi istemci) yazımlarında sonlandırıcı NUL'ı da `size`
içine katar:

- `io-lib/DataOutputStream.cpp:120-127` → `writeUTF8` → `Utf8StringStorage`
  nesnesinin `getSize()` değerini yazar;
- `util/Utf8StringStorage.cpp:65-68` → `getSize() = m_buffer.size()`
  (`getLength() = size - 1`, yani NUL'suz uzunluk **kullanılmaz**).

Yani **sunucudan gelen her string (dosya adı, yol, hata açıklaması) sonunda bir
`\0` baytı taşır.** Sonuçları ve gereken davranış:

1. **Ayrıştırıcıda NUL temizliği ZORUNLUDUR.** Yapılmazsa:
   - Dosya adı görüntüde garbi karakterle bozulur (ör. `PLC\0` + UI'nin eklediği
     `/` → `PLC<byte>/`);
   - UI yol birleştirmede gömülü NUL, sunucuda C-string semantiğiyle **klasör adını
     keser**: `"/C:/PROJE\0/PLC\0"` isteği sunucuda `"/C:/PROJE"` olarak görülür →
     gezinme **her tıklamada aynı dizine** düşer (bu belge yazılırken giderilen hata).
   Uygulamada: `Ft::parseFileListEntries` (`FtProtocol.h:248-258`) ve `Ft::parseUtf8`
   (`FtProtocol.h:278-282`) sondaki NUL'ları uçurur; ayrıca hata metni için
   `VncSession.cpp` `LastRequestFailed` dalı da temizler; UI tarafında
   `FileTransferDialog::navigateRemote` gömülü NUL ve sondaki noktaları da arındırır.
2. **Bizim gönderdiğimiz stringler NUL'suzdur ve bu güvenlidir.** Sunucu
   `DataInputStream::readUTF8` (`io-lib/DataInputStream.cpp:129-140`) tamponu
   olduğu gibi okur; `Utf8StringStorage::toStringStorage`
   (`util/Utf8StringStorage.cpp:104-123`) `getSize()` = tam boyut üzerinden
   dönüştürür ("kaynak string sonlandırıcısız olabilir" notu ile). C-string kurucusu
   NUL'da durur; NUL yoksa tüm veriyi alır ⇒ son karakter kaybolmaz.
3. NUL strip edilirken akış doğruluğu korunur: uzunluk alanına göre **ham baytlar**
   tüketilir, yalnızca ad/metin değeri temizlenir.

### 1.5 Yol biçimi (`FTMessage.h:62-71`)

- Unix tarzı: `"/C:/klasor/alt"`, ayraç `/`, kök `/`.
- Klasör yollarında **sondaki `/` yok** (Windows disk C → `"/C:"`).
- Windows'ta `"/"` listesi **sürücülerdir** (klasör bayrağıyla döner).
- Uygulama tarafı görünüm `C:/klasor` (başsız `/`); dönüşümler
  `Ft::toWirePath` / `Ft::fromWirePath` (`FtProtocol.h:286-303`).

---

## 2. Bağlantı önkoşulları

### 2.1 Güvenlik tipi 16 (Tight) ve yamalı libvncclient

- TightVNC 2, uzantı üzerinden `securityTypes` sunar; yerleşik tip listesinde 16
  yoktur. Uzantının `securityTypes = {16}` alanı + libvncclient yaması
  (`prefer-extension-security-types.patch`, uygulanıyor:
  `overlay-ports/libvncserver/portfile.cmake`) bu tipleri **tercih ettirir**.
- `handleAuthentication = tightAuthCallback` (`VncSession.cpp:83, 93`) Tight el
  sıkışmasını yürütür; `SecurityResult`'ı kütüphane okur.
- Başarılı bağlantıda debug log `authScheme=16` üretir
  (`build\xvncmanager.log`).

### 2.2 FT yetenek tespiti (etkileşim yetenekleri)

TightVNC 2, ServerInit'i takiben **etkileşim yetenekleri** listesi gönderir
(`VncSession.cpp:1066-1070` yorumu): 8 baytlık başlık
(`u16 sunucuMesajları, u16 istemciMesajları, u16 encoding, u16 pad`) + her liste
için 16'şar baytlık kayıtlar (`u32 kod + 4 bayt vendor + 8 bayt imza`). Dosya
transferi yetenek kodları `0xFC0001xx` burada duyurulur; **sunucu FT kapalıysa bu
kodlar listelenmez.**

Uygulama: `VncSession::readTightInteractionCaps` (`VncSession.cpp:1071-1099`)
tüm kayıtlarda `Ft::FileListReply` kodunu arar → `m_ftSupported`; log satırı
`Etkilesim yetenekleri: srv=… cl=… enc=… ft=var/yok`. `MainWindow` bu bayrağa
bakarak Dosya transferi menüsünü gösterir/gizler.

---

## 3. Mesaj katalogu

Tüm gövdeler §1.3 BE düzenindedir. `CompressedBlock` tanımı için §5'e bakınız.

### c→s istekler (yapıcılar: `src/FtProtocol.h`)

| Mesaj | Alanlar (sırayla) | Yapıcı |
|---|---|---|
| CompressionSupportRequest | gövde yok | (uygulamada tanımli değil — gönderilmiyor, §6) |
| FileListRequest | `u8 compressionLevel` (her zaman `0`), `StringUTF8 yol` | `Ft::fileListRequest` |
| DownloadStartRequest | `StringUTF8 yol`, `u64 ofset` (her zaman `0`) | `Ft::downloadStartRequest` |
| DownloadDataRequest | `u8 compressionLevel` (`0`), `u32 dataSize` (`Ft::ChunkSize`) | `Ft::downloadDataRequest` |
| UploadStartRequest | `StringUTF8 yol`, `u8 bayrak` (`0x1` = varsa üstüne yaz/truncate), `u64 ofset` (`0`) | `Ft::uploadStartRequest` |
| UploadDataRequest | `u8 compressionLevel` (`0`), `u32 compressedSize`, `u32 uncompressedSize`, `u8 veri[…]` — hamda iki boyut **eşit** | `Ft::uploadDataRequest` |
| UploadEndRequest | `u16 fileFlags`, `u64 modZaman(ms)` | `Ft::uploadEndRequest` |
| MkdirRequest | `StringUTF8 yol` | `Ft::mkdirRequest` |
| RemoveRequest | `StringUTF8 yol` | `Ft::removeRequest` |
| RenameRequest | `StringUTF8 eskiYol`, `StringUTF8 yeniYol` | `Ft::renameRequest` |
| DirSizeRequest | `StringUTF8 yol` | `Ft::dirSizeRequest` |
| MD5Request | `StringUTF8 yol`, `u64 offset`, `u64 dataSize` | **yok — kullanılmıyor** |

Resmi istemciyle birebir doğrulanmış alan sıraları:
`ft-client-lib/FileTransferRequestSender.cpp:50-233`
(özellikle `UPLOAD_DATA_REQUEST`: `:201-203` — resmi istemci de iki boyuta
**aynı değeri** yazar).

### s→c cevaplar

| Mesaj | Gövde | Kaynak |
|---|---|---|
| CompressionSupportReply | `u8 isCompressionSupported` — TightVNC sunucusu her zaman `1` | `FileTransferRequestHandler.cpp:201-209` |
| FileListReply | **CompressedBlock** içeriği: `u32 filesCount`; kayıt başına `u64 fileSize` (klasörde 0), `u64 modTime` (ms), **`u16 fileFlags`**, `StringUTF8 relFilename` | `:262-267` (+ sarma `:293-297`) |
| DownloadStartReply | gövde yok (dosya açıldı/seek edildi) | `:735` |
| DownloadDataReply | **CompressedBlock** (okunan dosya verisi) | `:842-845` |
| DownloadEndReply | `u8 fileFlags`, `u64 modTime` (ms) | `:803-805` |
| UploadStartReply | gövde yok | `:566` |
| UploadDataReply | gövde yok (parça ack'sı — akış kontrolü) | `:616` |
| UploadEndReply | gövde yok | `:668` |
| MkdirReply | gövde yok | `:338` |
| RemoveReply | gövde yok | `:369` |
| RenameReply | gövde yok | `:399` |
| DirSizeReply | `u64 klasörBoyutu` | `:427-428` |
| MD5Reply | `u8[16]` (kullanılmıyor) | `FTMessage.h:180-184` |
| LastRequestFailedReply | `StringUTF8 açıklama` | `:870-871` |

> **UYUMLULUK NOTU — fileFlags genişliği:** `FTMessage.h:157` yorumu listedeki
> `fileFlags` alanını `UINT8` olarak **belirtir**, ancak gerçek sunucu
> `writeUInt16(files[i].getFlags())` yazar (`FileTransferRequestHandler.cpp:266`).
> Liste kaydı `u16` olarak **okunmalıdır**; `u8` okumak tüm kayıtları 1 bayt
> kaydırıp akışı bozar. Uygulama `Ft::parseFileListEntries` (`FtProtocol.h:232-265`)
> `u16` okur. (`UploadEndRequest`/`DownloadEndReply` uçlarında da genişlikler
> sırasıyla `u16`/`u8` — sunucu koduyla eşleşir.)

### LastRequestFailedReply — hata modeli

- Sunucu işleyicisi herhangi bir istisna fırlattığında (`catch (Exception &someEx)`
  `FileTransferRequestHandler.cpp:178-180`), beklenecek normal cevap **yerine**
  `LastRequestFailedReply` gelir. Akış senkronu bozulmaz: istek başına tek cevap.
- `SystemException` açıklamaları Windows FormatMessage tabanlıdır ve hata kodu
  içerir (ör. dolu klasör silme → `Error 145: The directory is not empty.`
  — `win-system/SystemException.cpp`). `FileTransferException` mesajları İngilizce
  sabit metindir (`"Directory already exists"`, `"No active upload at the moment"`,
  `"Cannot change last write file time"` …).
- Uygulama: metin `Ft::parseUtf8` + NUL temizliği ile okunur, `ftFinished`/
  sinyalleriyle UI'a düşer; UI katmanı bilinen Windows kodlarını Türkçeye çevirir
  (`FileTransferDialog::translateSystemError`: 145 → *"Klasör boş değil; önce
  içeriğini silin."*, 2/3, 5, 17, 32, 80/183 …); bilmeyen kodlar olduğu gibi
  gösterilir.

---

## 4. Akış dizileri

Genel kurallar:

- **Oturum başına tek işlem.** Sunucuda tek yükleme/indirme slotu vardır
  (`m_uploadFile`, `m_downloadFile`); istemcide tek durum makinesi
  `FtState` (`VncSession.h:122-132`: `Idle, Drives, Listing, DlWaitDirSize,
  DlWaitStart, DlWaitData, UlWaitStart, UlWaitData, UlWaitEnd, FsCommand`).
  Bir isteğin cevabı gelmeden sonraki istek ** yazılmaz** (FT komutları
  `drainFtCommands` kuyruğundan sırayla akar).
- **Her veri parçası ayrı istek/cevaptır** (ping-pong): yüklemede ack gelmeden
  sonraki parça göndermez; indirmede her `DownloadDataReply`'den sonra yeni
  `DownloadDataRequest` yazılır.
- **Zaman aşımı:** her bekleyişin 30 sn son sınırı vardır (`VncSession.cpp:62`);
  cevap gelmezse işlem kullanıcıya hata olarak biter.
- Framebuffer mesajları (Update/CutText/Bell) arasına FT cevapları serpişebilir;
  hepsi `handleMessage` kancasından geçer.

### 4.1 Dizin listeleme (+ sürücüler)

```
istemci                                 sunucu
  |  FileListRequest(0, "/")               |
  |--------------------------------------->|  sürücü listesi
  |  FileListReply { count; "C:", "E:" … } |   (hepsi klasör bayrağıyla)
  |<---------------------------------------|
  |  FileListRequest(0, "/C:/klasor")      |
  |--------------------------------------->|
  |  FileListReply { count; {size,mod,flg,ad}… }   veya  LastRequestFailedReply
  |<---------------------------------------|
```

- Liste içeriği kayıtları: ad **yalnızca dosya adıdır** (`FTMessage.h:158`:
  `"test.txt"`, `"/test.txt"` değil).
- `"/"` isteği sürucü listesini verir.
- UI `..` satırını kendisi ekler; sunucudan gelen `.` / `..` kayıtları
  (NUL strip sonrası birebir eşleşen) `FileTransferDialog::fillPane` içinde
  filtrelenir.
- Cevap CompressedBlock olduğundan indirme/açma hatası ayrı bir yol değil,
  `LastRequestFailedReply`'dir.

### 4.2 İndirme

```
  |  DirSizeRequest(yol)                    |  (isteğe bağlı: ilerleme toplamı)
  |  DirSizeReply { u64 boyut }             |
  |  DownloadStartRequest(yol, 0)           |
  |  DownloadStartReply { }                 |  veya LastRequestFailed
  |  DownloadDataRequest(0, 65536)          |
  |  DownloadDataReply { CompressedBlock }  |  <- tekrar: her parça ayrı istek
  |  DownloadDataRequest(0, 65536)          |
  |        …                                |
  |  DownloadDataRequest(0, 65536)          |
  |  DownloadEndReply { u8 flg, u64 mod }   |  <- dosya sonu (EOFException)
```

- EOF sunucuda şöyle üretilir: `DownloadDataRequest` içindeki okuma
  `EOFException` fırlatır → `DownloadEndReply` gönderilir ve indirme slotu
  kapanır (`FileTransferRequestHandler.cpp:790-819`). **Ayrı bir "son" isteği
  yoktur**; istemci, `DownloadEndReply` gelene kadar parça istemeye devam eder
  (`VncSession.cpp:855-857`).
- İstemci toplam boyutu `DirSizeReply`'den alıp ilerleme çubuğunu ona göre
  gösterir (`DlWaitDirSize` → `DlWaitStart` → `DlWaitData`).
- Dosya açılamazsa `DownloadStartReply` gelmez, `LastRequestFailedReply` gelir.

### 4.3 Yükleme

```
  |  UploadStartRequest(yol, 0x1, 0)        |
  |  UploadStartReply { }                   |  veya LastRequestFailed
  |  UploadDataRequest(0, n, n, ham)        |
  |  UploadDataReply { }                    |  <- ack: sonraki parça bundan sonra
  |        … (tek tek, Ft::ChunkSize) …     |
  |  UploadEndRequest(flags, modTimeMs)     |
  |  UploadEndReply { }                     |  veya LastRequestFailed
```

- `bayrak = 0x1` mevcut dosyayı keser (truncate) — `FileTransferRequestHandler.cpp:547`.
- Parçalar ham gönderilir (`compressedSize == uncompressedSize`); resmi istemci
  de aynısını yapar.
- `UploadEndReply` dosyanın kapanışını ve `modificationTime` uygulanmasını
  onaylar; saat ayarı başarısızsa (`setLastModified`) `LastRequestFailedReply`
  ("Cannot change last write file time") gelir.
- Hata çıkarsa sunucu bir sonraki `UploadDataRequest`'te
  `"No active upload at the moment"` fırlatır (slot kapanmıştır) — istemci
  bunu işlemin iptali olarak raporlar.

### 4.4 Klasör oluştur / sil / yeniden adlandır

| İşlem | İstek | Cevap | Sık gelen hatalar |
|---|---|---|---|
| Klasör oluştur | `MkdirRequest(yol)` | `MkdirReply` | `"Directory already exists"` (183/80'e çevrilir), erişim reddi (5) |
| Sil | `RemoveRequest(yol)` | `RemoveReply` | **145 = klasör dolu**, 2/3 bulunamadı, 5 yetki |
| Yeniden adlandır | `RenameRequest(eski, yeni)` | `RenameReply` | 17 / 80 / 183 hedef var, 2 kaynak yok |

- UI'da klasör silmede **önceden onay** alınır
  (`FileTransferDialog::removeRemote`: *"Klasör silinecek: … Klasör boş değilse
  sunucu reddedecek. Devam edilsin mi?"*); dolu klasör sunucu tarafından reddedilir
  (özyinelemeli silme protokolde **yoktur** — RecursiveList/RecursiveSize bu
  sürümde tanımsızdır, resmi istemci de kullanmaz).

---

## 5. Sıkıştırma (CompressedBlock)

`FTMessage.h:100-107` (readme):

```
CompressedBlock:
  UINT8  compressionLevel   // 0 - sikistirilmemis, [1..9] zlib
  UINT32 compressedSize     // sikistirilmis veri boyutu
  UINT32 uncompressedSize   // ham veri boyutu
  UINT8  data[compressedSize]
  @note compressionLevel 0 ise compressedSize == uncompressedSize olup
        data sikistirilmamistir.
```

- **Kapsar:** `FileListReply` içeriği ve `DownloadDataReply` verisi
  (`FileTransferRequestHandler.cpp:293-297`, `:842-845`).
- `compressionLevel` istemci **tercihidir**; sunucu isteğe bağlı davranır
  (`FTMessage.h:94-98`). Seviye **0 istenirse sunucu asla sıkıştırmaz**
  ("istemci desteklemiyor" sayılır) — `fileListRequested` içinde deflate yalnızca
  `compressionLevel != 0` ise çalışır (`:279-284`).
- **xVNCManager her istekte seviye 0 gönderir** ⇒ gelen veri hamdır;
  zlib bağlantısı gerekmez, `DownloadDataReply`'de `readFtCompressedBlock`
  yalnızca ham (seviye 0) blok kabul eder (`VncSession.cpp:839-847`; seviye >0
  gelirse güvenli biçimde hata üretir).
- `CompressionSupportRequest/Reply` bu yüzden kullanılmaz; istemcide yapıcı
  bile tanımlı değildir. Sormak istenirse gövdesiz istek + `u8` cevap yeterlidir
  (sunucu her zaman `1` döner).

---

## 6. Uygulama haritası

```
FtProtocol.h
  MessageId / FileFlags / Entry / ChunkSize
  putU8..putU64, putUtf8, getBe16/32/64        — BE bayt düzeni
  fileListRequest, downloadStartRequest, …      — istek yapıcıları (tek WriteToRFBServer)
  parseFileListEntries, parseUtf8               — cevap ayrıştırıcıları (+ NUL temizliği)
  toWirePath / fromWirePath                     — "C:/x" <-> "/C:/x"
  messageIdName                                 — loglar için

VncSession.cpp
  g_ftExtension + ftMessageCallback             — 0xFC uzantısı (§1.2)
  g_tightSecurityTypes = {16} + tightAuthCallback — Tight güvenlik tipi
  readTightInteractionCaps                      — ft=var/yok tespiti (§2.2)
  handleTightFtMessage                          — kimliğe göre gövde okuma + FtState
  requestFtDownloadChunk / sendFtUploadChunk    — 64 KiB ping-pong (§4.2/§4.3)
  drainFtCommands                              — istek kuyruğu (tek işlemsizlik)
  m_ftDeadline                                  — 30 sn son sınır

FileTransferDialog.cpp
  navigateRemote / navigateLocal / fillPane     — gezinme, ".." ve "." filtresi
  onRemoteActivated                             — çift tık: klasör → gez, dosya → indir
  translateSystemError / lastSystemErrorText    — Windows kodu → Türkçe (§3)
  removeRemote/removeLocal                      — silme onayları
```

Bağlantı menüsü/oturum tarafı: `MainWindow` yalnızca `m_ftSupported` bayrağını
gözlemler; FT menüsü ona göre görünür.

---

## 7. Tuzaklar ve bilinen farklar

| Konu | Durum |
|---|---|
| `fileFlags` genişliği | Şartname (`FTMessage.h:157`) `UINT8` der; **gerçek sunucu liste kaydında `u16` yazar** (`:266`) → `u16` oku (uygulama okur) |
| StringUTF8 NUL sonlandırıcı | Sunucu cevaplarına NUL **eklenir** (`Utf8StringStorage.cpp:65-68`) → ayrıştırmada strip **zorunlu**; strip edilmezse ad bozulur ve gezinme aynı dizine düşer (giderildi) |
| Bizim yazdığımız stringler | NUL'suz ve güvenli (`toStringStorage` tam boyut okur, §1.4) |
| `compressionLevel = 0` | "İstemci sıkıştırma desteklemiyor" demektir; istemcimiz hep 0 gönderir → sunucu ham gönderir, zlib gerekmez |
| `UploadDataRequest` iki boyutu | Hamda eşit; resmi istemci de eşit gönderir (`FileTransferRequestSender.cpp:201-203`) |
| MD5 (`0xFC000104/105`) | Protokolde tanımlı, sunucu destekliyor; uygulama **kullanmıyor** (delta/doğrulama gerekmedi) |
| Özyinelemeli silme / RecursiveList | Yok (resmi istemcide de yok); dolu klasör 145 ile reddedilir |
| Bir anda tek işlem | Sunucuda tek slot, istemcide tek `FtState` — paralel transfer desteklenmez |
| FT sunucuda kapalıysa | Etkileşim yeteneklerinde `0xFC0001xx` yok → `ft=var` alınmaz, menü gizlenir; istek gönderilmişsa `LastRequestFailedReply` (erişim/hata metni) gelir |
| Bilinmeyen/bozuk kayıt | Ayrıştırıcı byte kayması yaşamamak için tavanlara (`MaxBlockBytes`, `MaxErrorBytes`) uyar; ad boşsa kayıt atlanır (`FtProtocol.h:257-258`) |

**Uçtan uca test izi** (test sunucusu, `build\xvncmanager.log`):
`authScheme=16` → `Etkilesim yetenekleri: … ft=var` → `FileListRequest/Reply`
kimlikleri; transfer sırasında `DownloadStartRequest → DownloadDataReply … →
DownloadEndReply` akışı görünür.

---

## Appendix — Eski belge

Tip-7 (UltraVNC/LibVNCServer) protokolünün tam araştırması (kaynak satır
alıntılarıyla) `docs/filetransfer-protocol-type7-legacy.md` dosyasındadır.
xVNCManager'in bu protokolle hiçbir teması yoktur; belge yalnızca referans
amacıyla korunur.
