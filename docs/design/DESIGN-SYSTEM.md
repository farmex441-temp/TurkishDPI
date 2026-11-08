# TurkishDPI – Tasarım dili (DESIGN-SYSTEM) · TASLAK

Durum: öneri, onay bekliyor. Bütün değerler mantıksal piksel (96 DPI) ve
kodda `S()`/`SF()` ile ölçeklenir. Kontrast oranları WCAG 2.x formülüyle
hesaplandı (bkz. UI-AUDIT §4.3).

## 1. İlkeler

1. **Tek soru:** Ana ekran önce "İnternetim korumalı ve çalışıyor mu?" sorusunu
   cevaplar. Diğer her şey bir tık uzakta.
2. **Durum = renk + ikon + metin.** Hiçbir durum yalnız renkle anlatılmaz.
3. **Sakin:** tek vurgu rengi aynı anda en fazla bir yerde. Animasyon yalnız
   bir iş sürerken (tarama).
4. **Dürüst sayı:** ekrandaki her sayı `g_ui`'deki gerçek bir alandan gelir;
   bilinmeyen değer "—" olarak gösterilir, tahmin edilmez.
5. **Ucuz çizim:** yeni GDI/GDI+ nesnesi aynı `paint()` içinde silinir; yazı
   tipleri DPI değişince yeniden yaratılır, başka zaman değil.

## 2. Izgara ve aralıklar

- Temel birim **4 px**; düzen aralıkları **8'in katları**: 8, 16, 24, 32.
- Pencere iç kenar boşluğu: **24 px** (bugün 20).
- Kart iç boşluğu: **16 px**. Kartlar arası: **8 px**. Bölümler arası: **24 px**.
- Satır yüksekliği (liste): **24 px** (bugün 15).
- Dokunma/tıklama hedefi en az **32 px** yükseklik (birincil düğme 40 px).

## 3. Tipografi

Yazı tipi: **Segoe UI** (Windows 10/11'de her zaman var). Win11'de
"Segoe UI Variable Text/Display" daha iyi görünür; varsa kullanılır, yoksa
Segoe UI'ye düşülür (`EnumFontFamiliesEx` bir kez, açılışta). Maliyet: ~20 satır.

| Rol | Boy / ağırlık | Kullanım |
|---|---|---|
| `display` | 24 / Semibold | Durum başlığı ("Koruma açık") |
| `title` | 16 / Semibold | Bölüm/sayfa başlığı, uygulama adı |
| `body` | 14 / Regular | Açıklamalar, düğme metni, liste satırı |
| `caption` | 12 / Regular | İkincil bilgi, ipuçları, sürüm |
| `label` | 12 / Semibold | Kart etiketleri (cümle düzeni, büyük harf **değil**) |

7 yazı tipinden 5'e iniyoruz. Büyük harfli 10 px etiketler kalkıyor: Türkçe
büyük harfte (İ, Ğ, Ş) ve 10 px'te okunmuyor, kontrastı da düşük.

## 4. Renkler

### 4.1 Koyu tema (varsayılan)

| Token | Değer | Not / kontrast |
|---|---|---|
| `bg` | `#11131A` | pencere zemini (aynı) |
| `surface` | `#1A1E29` | kart |
| `surface-hover` | `#232836` | üzerine gelince |
| `surface-sunken` | `#151821` | giriş kutusu |
| `border` | `#2A3040` | dekoratif ayraç |
| `text` | `#ECEEF3` | 14.3:1 kartta |
| `text-2` | `#9AA3B5` | 6.6:1 kartta (bugünkü #8A92A6 yerine) |
| `text-3` | `#838B9D` | 4.9:1 kartta (bugünkü #5C6375 = 2.8:1 yerine) |
| `focus` | `#60A5FA` | odak halkası, 6.6:1 |
| `brand` | `#E5293A` | **yalnız logo ve simge**; metin/düğme rengi değil |
| `accent` (birincil düğme) | `#C81E2E` | beyaz metin 5.7:1 (bugün 4.46) |

### 4.2 Durum renkleri (pencere, tepsi ve bildirimde **aynı**)

| Anlam | Koyu | Açık | Durumlar |
|---|---|---|---|
| `ok` | `#22C55E` (7.3:1) | `#15803D` (5.0:1) | ACTIVE |
| `warn` | `#F5B014` (8.8:1) | `#A16207` (4.9:1) | PARTIAL |
| `busy` | `#60A5FA` (6.6:1) | `#1D4ED8` (6.7:1) | STARTING, SCANNING |
| `idle` | `#94A3B8` (6.5:1) | `#475569` (7.6:1) | STANDBY, PAUSED, OFFLINE |
| `error` | `#F87171` (6.0:1) | `#B91C1C` (6.5:1) | ERROR |

Değişiklik: STANDBY artık ayrı açık mavi değil. "Engel algılanmadı" bir
başarı değil, bir bekleme hâli; `idle` + kendi ikonu yeterli. Böylece
paletteki durum rengi 7'den 5'e iner.

### 4.3 Açık tema (isteğe bağlı, ikinci aşama)

| Token | Değer |
|---|---|
| `bg` | `#F4F5F8` |
| `surface` | `#FFFFFF` |
| `surface-hover` | `#EEF0F4` |
| `border` | `#D5D9E2` |
| `text` / `text-2` / `text-3` | `#151821` (17.7) / `#4B5263` (7.8) / `#646B7B` (5.3) |
| `focus` | `#1D4ED8` |

Değerlendirme: Windows ayarı `HKCU\...\Themes\Personalize\AppsUseLightTheme`
okunur, `WM_SETTINGCHANGE` ("ImmersiveColorSet") ile izlenir. Maliyet:
renkleri `static const` yerine bir `palette_t` tablosundan okumak (~60 satır
değişiklik), DWM başlık rengi + `SetPreferredAppMode` değeri temaya göre.
Bellek etkisi yok. **Öneri:** renk tokenlarını birinci aşamada tabloya taşı
(zaten yeniden adlandırıyoruz), açık temayı ikinci aşamada aç. Dikkat:
yükseltilmiş süreç HKCU'yu doğru kullanıcıdan okur (aynı oturum), sorun yok.

## 5. Biçim

- Köşe yarıçapı: **8** (düğme, giriş), **12** (kart), **tam** (durum çipi).
- Kenarlık: 1 px `border`, yalnız kartlarda; düğmeler dolgu ile ayrılır.
- Gölge, parıltı, gradyan **yok** (bugünkü başlık parıltısı ve düğme gradyanı
  kalkar). Tek istisna: durum halkasının arkasındaki %10'luk durum rengi hâlesi.

## 6. Durum ikonları

Halka ortasında tek glif, 2.5 px çizgi, GDI+ ile çizilir (yazı tipi/kaynak yok):

| Durum | Glif | Başlık önerisi | Açıklama önerisi |
|---|---|---|---|
| ACTIVE | ✓ onay | Koruma açık | Engelli siteler açılıyor. |
| PARTIAL | ! ünlem | Kısmen çalışıyor | Bazı siteler açılmıyor olabilir. |
| SCANNING/STARTING | dönen yay | Hazırlanıyor… | `status`'tan kısa adım ("En hızlı bağlantı aranıyor") |
| STANDBY | kalkan çizgisi | Engel yok | Gerekirse kendiliğinden devreye girer. |
| PAUSED | ‖ duraklat | Duraklatıldı | Açmak için düğmeye basın. |
| OFFLINE | bağlantı yok (kesik dalga) | İnternet yok | Bağlantı gelince kendiliğinden başlar. |
| ERROR | ✕ | Çalışamıyor | Ne yapılacağı (tek cümle) + "Nasıl düzeltilir" bağlantısı |

Not: başlık/açıklama metinlerini bugün `app.c` üretiyor (`set_state`). İş
mantığına dokunmamak için **UI, `kind`'e göre kendi kısa başlığını gösterir**;
`status`/`detail` metinleri olduğu gibi Ayrıntılar'da ve tepsi ipucunda kalır.
Bu, `app.c`'de değişiklik gerektirmez.

## 7. Bileşenler

| Bileşen | Ölçü | Durumlar |
|---|---|---|
| Durum düğmesi (halka) | 120 px, halka 4 px | normal / hover (+%6 açık) / odak (focus halkası 2 px, 4 px boşluk) / basılı |
| Birincil düğme | yük. 40, yarıçap 8, `accent` | normal / hover / odak / devre dışı (%40) |
| İkincil düğme | yük. 36, `surface` + kenarlık | aynı |
| Metin düğmesi / bağlantı | `text-2`, altı çizgi hover'da | odak halkası |
| Bilgi satırı | etiket `label text-3` solda, değer `body text` sağda, 24 px | — |
| Çip | yük. 24, tam yarıçap, %15 durum rengi zemin + durum rengi metin | — |
| Bildirim bandı | yük. ≥ 32, 2 satıra kadar sarar, sol ikon, sağda eylem metni | bilgi / uyarı / hata |
| Anahtar (toggle) | 36×20 | açık / kapalı / odak |
| Giriş kutusu | yük. 36, `surface-sunken`, native EDIT | odakta `focus` kenarlık |

## 8. Hareket

- Tek animasyon: tarama sırasında halkada dönen yay. 30 fps → **20 fps**
  (50 ms) yeterli; yalnız pencere görünürken ve yalnız halkanın dikdörtgeni.
- Windows "animasyonları göster" kapalıysa (`SPI_GETCLIENTAREAANIMATION`)
  dönen yay yerine sabit yarım halka.
- Hover/odak geçişleri anlık (zamanlayıcı gerektiren yumuşatma yok).

## 9. Metin kuralları

- Cümle düzeni ("Koruma açık"), BÜYÜK HARF etiket yok.
- Başlık ≤ 22 karakter, açıklama ≤ 2 satır (≈ 90 karakter).
- Hata metni kalıbı: **ne oldu** (başlık) → **ne yapmalı** (açıklama) →
  **eylem** (düğme/bağlantı). Hata kodu yalnız Ayrıntılar'da.
- Suçlamayan dil: "Antivirüs sürücüyü engelledi" ✔, "Yanlış ayar yaptınız" ✘.
- Basit görünümde yasak kelimeler: DNS, SNI, TTL, TCP, TLS, paket, profil
  numarası, IP adresi.
- Bir kavram tek adla: "Ödeme koruması" (bugün "Ödeme ve banka koruması" da var).

## 10. Simgeler

- Uygulama simgesi: kalkan + hilal + ileri ok **korunur**; parlaklık ve
  gölge kaldırılır (düz kırmızı, 16 px'te daha keskin).
- Tepsi: iki seçenek (SCREENS.md'de karar):
  - **T1 (önerilen):** kalkanın kendisi durum rengine boyanır (yeşil/amber/
    mavi/gri/kırmızı), beyaz amblem aynı kalır. 16 px'te durum bütün
    simgeden okunur; marka şekli korunur.
  - **T2:** kırmızı kalkan kalır, nokta büyür (%45) ve ikon taşır.
- Kural değişmez: sıkıştırılmamış BMP kareler, en fazla 128 px.
