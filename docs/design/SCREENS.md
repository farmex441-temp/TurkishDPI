# TurkishDPI – Ekran önerileri (SCREENS) · TASLAK

Ölçüler mantıksal px. Hedef: istemci alanı **≤ 540 px yükseklik**
(1366×768 dizüstünde %125 ölçekte bile çalışma alanına sığar:
540 × 1.25 + başlık çubuğu ≈ 715 px < ~720 px).

Üç düzen önerisi, sonra önerilen düzenin ayrıntılı ekranları.

---

## Düzen A — Üç sekmeli sabit pencere (ÖNERİLEN)

400 × 520. Üstte sade başlık + sağda segment sekmeler; içerik değişir,
pencere boyutu değişmez.

```
┌────────────────────────────────────────────┐
│ [logo] TurkishDPI        [Durum|Ayrıntı|⚙] │ 56
├────────────────────────────────────────────┤
│ ⚠ Yeni sürüm hazır (v1.2.0)       İndir →  │ (yalnız uyarı varsa)
│                                            │
│                 ╭──────╮                   │
│                 │  ✓   │  120 px halka     │
│                 ╰──────╯                   │
│               Koruma açık                  │ display 24
│        Engelli siteler açılıyor.           │ body text-2
│                                            │
│   ┌──────────────────────────────────────┐ │
│   │ Ağ            Ev Wi-Fi               │ │ bilgi satırları
│   │ Hız           152 ms · iyi           │ │ (2 satır, 24 px)
│   └──────────────────────────────────────┘ │
│                                            │
│   [ Bir site açılmıyor mu? Kontrol et  → ] │ ikincil düğme
│                                            │
│ 🛡 Ödeme koruması açık                      │ caption, altta
└────────────────────────────────────────────┘
```

**Artılar**
- Sabit boyut: her ekranda sığar, `WM_DPICHANGED` mantığı değişmez.
- Basit görünümde teknik kelime yok; Ayrıntılar bir tık uzakta.
- Ayarlar gerçek bir panel olur (anahtarlar, açıklamalar, tehlikeli
  işlem ayrı bölümde).
- Sekmeler ve düğmeler sahip-çizimli (owner-draw) native `BUTTON` olunca
  Tab/Enter/Space ve ekran okuyucu adı bedavaya gelir (bkz. §Erişilebilirlik).

**Eksiler**
- Üç sayfanın düzeni ayrı ayrı bakım ister (~+300 satır `ui.c`).
- Ayrıntı isteyen kullanıcı bir tık fazla yapar.

## Düzen B — Tepsi açılır paneli (flyout)

360 × 420, başlık çubuğu yok, görev çubuğunun yanında açılır (Windows
"Hızlı ayarlar" gibi), odak kaybedince kapanır. Ayrıntılar ve Ayarlar aynı
panelde "‹ Geri" ile kayar.

**Artılar:** tepsi uygulaması için en doğal kalıp; en küçük alan; tek tık.
**Eksiler:** kenarlıksız pencerede taşınamaz; görev çubuğu konumu (alt/üst/
yan), çoklu monitör ve DPI için konumlama kodu (~150 satır, hata riski);
uygulama Başlat menüsünden açıldığında da tepsi köşesinde görünür; odak
kaybedince kapanması site testi sırasında can sıkıcı; ekran okuyucu ve
klavye ile "kapanan pencere" deneyimi zayıf.

## Düzen C — Tek sayfa + açılır "Ayrıntılar" bölümü

400 × 460 başlar; "Ayrıntılar ▾" açılınca pencere aşağı uzar (≤ çalışma
alanı), sığmazsa ayrıntı bölümü kaydırılır. Ayarlar native menüde kalır.

**Artılar:** en az yeni kod; bugünkü yapıya en yakın.
**Eksiler:** pencere boyu sıçrar; küçük ekranda kaydırma gerekir ve elle
çizilmiş kaydırma pahalı (kaydırma çubuğu, tekerlek, klavye); Ayarlar menüsü
sorunu (karışık, kaldır/çıkış komşu) çözülmez.

### Neden A
B'nin konumlama ve odak riskleri, tek pencerelik bu uygulamanın
basitliğine ters. C, 768 px ve ayarlar sorunlarını yarım çözüyor. A, sabit
ve küçük bir pencereyle hem basit hem ayrıntılı katmanı veriyor; maliyeti
yalnız `ui.c` içinde.

---

## A'nın ayrıntılı ekranları

### A1 · Durum (her durum)

Halka glifi + başlık + açıklama `kind`'den gelir (DESIGN-SYSTEM §6). Bilgi
kartı duruma göre:

| Durum | Bilgi kartı | Ana eylem |
|---|---|---|
| ACTIVE | Ağ · Hız (ort. ms, "iyi/orta/yavaş" eşikleri: <250/<600/≥600) | halka = Duraklat |
| PARTIAL | Ağ · "3 / 4 test sitesi açılıyor" | "Yeniden dene" (= Yeniden tara) |
| SCANNING | Adım metni (`status`) + "3 / 10" (`scan_pos/total`) | — (halka devre dışı değil, duraklatır) |
| STANDBY | Ağ | — |
| PAUSED | — | halka = Devam et |
| OFFLINE | — | — |
| ERROR | Hata kartı: ne yapmalı + "Nasıl düzeltilir" | sürücü hatası → Antivirüs yardımı; diğer → "Yeniden dene" |

Halkaya üzerine gelince ipucu: "Duraklat" / "Devam et". Tek uyarı bandı
kalır ama birden çok uyarı varsa "+1" çipi ile Ayrıntılar'a yönlendirir.

İlk açılış (öğrenilmiş veri yok, `learned == 0`, SCANNING): açıklama "İlk
kez bu ağda en iyi yöntem aranıyor. Bu bir dakika sürebilir." — ilerleme
`scan_pos/scan_total` ile ince bir çubuk.

### A2 · Ayrıntılar

```
│ Yöntem        SNI Parçalama + Sahte SNI   [Değiştir ▾] │  profil menüsü (native)
│ Mod           Otomatik                                  │
│ DNS           Hibrit · 208.67.222.222 · 42 ms            │
│ Bağlantı      152 ms (TCP+TLS ort.)                      │
│ Öğrenilen     3 / 10 yöntem bu ağda                      │
│─────────────── Test siteleri ───────────────────────────│
│ ✓ discord.com                                  148 ms   │  24 px satır, 8 satır
│ ✕ example.org                                 engelli   │  ikon + metin
│─────────────── Site kontrolü ───────────────────────────│
│ [ ör. discord.com              ] [ Kontrol et ]          │
│ son sonuç (2 satıra kadar)                               │
│ 1.234 paket · 56 HTTPS · 9 DNS · 6 ödeme atlandı         │  caption
│                         [ ⟳ Yeniden tara ]               │
```

Yükseklik kontrolü: başlık 56 + 5 satır×24 + başlık 24 + 8×24 + başlık 24
+ giriş 36 + sonuç 36 + sayaç 20 + düğme 36 + boşluklar ≈ 520 → sığar, ama
sınırda. 8'den az sitede boşluk kalır; daha fazla bilgi eklenirse ilk aday
"Öğrenilen" satırının kaldırılması.

### A3 · Ayarlar

```
│ Otomatik yöntem seçimi                       [●──] │ açıklama: önerilen
│ Windows açılışında başlat                    [●──] │
│ Ödeme koruması                               [●──] │ "Banka ve ödeme siteleri…"
│──────────────────────────────────────────────────│
│ Günlüğü aç  ·  Gelişmiş ayarlar  ·  Antivirüs yardımı  ·  GitHub │ bağlantılar
│──────────────────────────────────────────────────│
│ Sürüm 1.1.0                         Güncellemeleri denetle (bilgi) │
│──────────────────────────────────────────────────│
│ [ Kaldır ve temizle… ]  (kırmızı metin, ayrı bölüm)  [ Çıkış ] │
```

Anahtarlar bugünkü komutları çağırır (`CMD_AUTO`, `CMD_AUTOSTART`,
`set_payment_protect` + mevcut onay kutusu); yeni iş mantığı yok.

### A4 · Tepsi

- Simge T1: kalkan durum renginde (DESIGN-SYSTEM §10).
- İpucu: "TurkishDPI · Koruma açık · Ev Wi-Fi" (≤ 127 karakter).
- Sağ tık menüsü: durum (gri) · Aç · Duraklat/Devam et · Yeniden tara · Çıkış
  — bugünküyle aynı, sade; değişiklik yok.
- Bildirimler: bugünkü kurallar korunur (sessiz, tekrar yok, `--tray`
  açılışında sessiz). Metinler §9 kalıbına çekilir — bu metinler `app.c`'de;
  değiştirmek iş mantığı dosyasına dokunmak demek, ayrı onayla.

---

## Erişilebilirlik (A için maliyet)

- **Odaklanabilir öğeler native `BUTTON` + `BS_OWNERDRAW`** olur (sekmeler,
  halka, düğmeler, anahtarlar). Mesaj döngüsü zaten `IsDialogMessageW`
  kullanıyor (`app.c`), Tab/Shift+Tab/Enter/Space ek kodsuz çalışır.
  Ekran okuyucu düğmenin pencere metnini okur ("Duraklat", "Ödeme koruması:
  açık"). Çizim `WM_DRAWITEM`'da aynı GDI+ yardımcılarıyla.
- Maliyet: ~10 alt pencere (USER nesnesi, bellek etkisi önemsiz),
  `paint()`'ten düğme çizimlerini `WM_DRAWITEM`'a taşımak.
- Durum değişince `NotifyWinEvent(EVENT_OBJECT_NAMECHANGE)` ile ekran
  okuyucuya bildirilebilir (≈ 5 satır).
- Tam UI Automation sağlayıcısı (özel öğeler için) **önerilmez**: COM, birkaç
  yüz satır, kazanç düşük.

## Kararlar (2026-11-08, kullanıcı kararı bana bıraktı)

1. **Düzen A** (üç sekmeli sabit pencere). Gerekçe yukarıda.
2. **Tepsi T1:** kalkan durum renginde. 16 px'te durum bütün simgeden
   okunur; T2'de nokta ~6 px kalıyor ve kırmızı kalkan her durumda "hata"
   çağrıştırıyor.
3. **Açık tema ikinci aşamada.** Birinci aşamada renkler bir `palette_t`
   tablosuna taşınır (açık temayı ucuzlatır); açık tema düzen oturduktan
   sonra açılır.
4. **Bildirim metinleri bu oturumda değişmez.** `app.c` diğer oturumun
   alanı; UI kendi kısa başlıklarını `kind`'den türettiği için gerekli
   değil. Öneri olarak ayrı not edilir.
5. **Hız etiketi eşikleri** (iyi/orta/yavaş: <250 / <600 / ≥600 ms) benim
   seçimim; geliştiricinin ağında ölçülen ~150 ms "iyi" çıkar.
