> **TARİHSEL BELGE – sürüm 1.1.0 koduna göre yazılmış taslak.** Bu belge, v1.2.0'daki arayüz yenilemesinin taslağıdır ve v1.1.0 koduna göre yazılmıştır (440×734 px tek sayfa); bugünkü pencere 400×520 px ve üç sayfalıdır (Durum / Ayrıntılar / Ayarlar). Motor 2.0'dan beri TurkishDPI'ın kendi motorudur (Shroudly çekirdeği). Güncel durum için [CLAUDE.md](../../CLAUDE.md) ve [README.md](../../README.md) dosyalarına bakın.

# TurkishDPI – Arayüz denetimi (UI-AUDIT)

Kaynak: `src/app/ui.c` (main @ 55b6244), `src/app/app.h`, `src/app/app.c`
(`set_state` çağrıları), `tools/make_icons.py`. Ekran görüntüsü yok: önizleme
modu henüz yok, bu belge koddan çıkarıldı. Ölçümler (kontrast) palet
sabitlerinden hesaplandı.

## 1. Bugünkü ekran (mantıksal 440×734 px)

```
y=16   [logo 40] TurkishDPI                           ( OTOMATİK )   <- pill, tıklanmaz
       Akıllı DPI kalkanı · v1.1.0
y=66   [ uyarı bandı: güncelleme | proxy | çakışan program | antivirüs ]  (en fazla 1)
y=92            ( güç düğmesi 132 px, durum renginde halka )
y=246                  Durum başlığı (22 px kalın)
y=278          açıklama (2 satır, çoğu zaman profil adı)
y=318  [ AĞ        ] [ BAĞLANTI (TCP+TLS) ] [ ÖĞRENİLEN    ]
       [ Wi-Fi     ] [ 152 ms             ] [ 3 / 10 profil ]
y=394  [ TEST SİTELERİ               HİBRİT DNS 208.67.222.222 · 42 ms ]
       [ ● discord.com                                    148 ms ]
       [ ... 8 satıra kadar, satır aralığı 15 px                    ]
       [ 1234 paket · 56 HTTPS · 2 HTTP · 9 DNS · 6 ödeme atlandı  ]
y=580  [ site kutusu (EDIT)                      ] [ Test et ]
y=622  Açılmayan bir siteyi yazın; TurkishDPI çözümü kendisi arasın.
y=650  [ ⟳ Yeniden tara ] [ Profil ▾ ] [ Ayarlar ▾ ]
y=702  ● Ödeme koruması açık · 100+ banka ve ödeme alan adına dokunulmaz
```

## 2. Durumlar (`state_kind_t`) ve ekranda görünenler

| Durum | Halka/renk | Başlık (`status`) | Açıklama (`detail`) | Tepsi simgesi |
|---|---|---|---|---|
| STARTING | mavi #3B82F6, dönen yay | "Başlatılıyor…" | — | sarı nokta (scan) |
| SCANNING | mavi, dönen yay | "Bağlantı kontrol ediliyor…", "En hızlı DNS ölçülüyor…", "Profil n/m deneniyor", "Site için çözüm aranıyor…" | profil adı veya site | sarı nokta |
| ACTIVE | yeşil #22C55E | "Koruma aktif" | **profil adı** (ör. "SNI Parçalama + Sahte SNI (DNS)") | yeşil nokta |
| PARTIAL | amber #F5B014 | "Kısmen aktif" / "Çalışan profil bulunamadı" | profil adı | sarı nokta |
| STANDBY | açık mavi #38BDF8 | "Engel algılanmadı" | "Bekleme modunda – engel çıkarsa devreye girer" | yeşil nokta |
| PAUSED | gri #64748B, glif gri | "Duraklatıldı" | "Açmak için düğmeye basın" | gri nokta |
| OFFLINE | açık gri #94A3B8 | "İnternet bağlantısı yok" | "Bağlantı gelince otomatik başlayacak" (bazen boş) | gri nokta |
| ERROR | kırmızı #EF4444 | "Sürücü başlatılamadı (5)", "Başlatılamadı", "Motor sürekli kapanıyor", "Çalışan profil bulunamadı" | sürücü hata metni / profil / "Günlük dosyasına bakın" | **koyu lacivert nokta** |

Hiçbir durumda halka dışında bir ikon yok: durumlar yalnızca renkle ve başlık
metniyle ayrılıyor.

## 3. Güçlü yanlar (korunmalı)

1. **Hafiflik doğru kurulmuş.** Pencere kapalıyken zamanlayıcı yok
   (`WM_CLOSE` → `KillTimer`, `SetProcessWorkingSetSize`), animasyon yalnız
   tarama sırasında ve yalnız `r_power` geçersiz kılınıyor. Bu mimari olduğu
   gibi kalmalı.
2. **Sözleşme temiz.** UI yalnızca `g_ui` kopyasını kilit altında alıp kilit
   dışında çiziyor; eylemler `post_cmd`/`post_site_test` ile gidiyor. Yeni
   tasarım bu sınırın içinde kalabilir.
3. **DPI temeli var.** `S()`/`SF()`, `WM_DPICHANGED`, `AdjustWindowRectExForDpi`.
   Sabit koordinatlar her yerde ölçekleniyor.
4. **Tek ana eylem fikri doğru.** Büyük güç düğmesi + tek satır başlık
   "korumalı mıyım?" sorusunu cevaplamaya yakın.
5. **Güvenlik dili dürüst.** Ödeme koruması kapatılırken uyarı, kaldırmada
   ne silineceği tek tek yazıyor, antivirüs yardımı adım adım.
6. **Koyu başlık çubuğu ve koyu menüler** pencereyle uyumlu.

## 4. Zayıf yanlar

### 4.1 Bilgi mimarisi
- **Her şey tek ekranda.** Durum, ağ, gecikme, öğrenme, 8 test sitesi, DNS
  sunucusu IP'si, paket sayaçları, site testi, profil seçimi, ayarlar ve ödeme
  satırı aynı anda görünüyor. Kullanıcının asıl sorusu ("çalışıyor mu?") ekranın
  üst üçte birinde, geri kalan üçte ikisi teknik ayrıntı.
- **Teknik terimler ana ekranda:** "TCP+TLS", "HİBRİT DNS 208.67.222.222",
  "paket · HTTPS · HTTP · DNS", profil adları ("-5, TTL 5", "SNI Parçalama").
  `detail` satırı ACTIVE'de profil adını gösteriyor: basit kullanıcı için anlamsız.
- **"ÖĞRENİLEN 3 / 10 profil"** iç bir sayaç; kullanıcıya bir karar sunmuyor.
- **"OTOMATİK" pill bir düğmeye benziyor ama tıklanmıyor.** Aynı ayar hem
  Profil hem Ayarlar menüsünde ("Otomatik (önerilen)" / "Otomatik mod") iki kez var.
- **Ayarlar bir açılır menü:** onay işaretli seçenekler, dosya açma, yardım,
  GitHub, kaldırma ve çıkış karışık. "Kaldır ve temizle…" ile "Çıkış" yan yana:
  yanlış tıklama riski (onay kutusu var, ama yine de).
- **Uyarı bandı tek yuva:** aynı anda proxy bozuk + güncelleme varsa yalnız
  biri görünüyor; çakışan program bandı tıklanmıyor (HIT_BANNER 3'ü dışlıyor)
  ama diğerleri gibi görünüyor.

### 4.2 Boyut ve düzen
- **Pencere 768 px ekrana sığmıyor.** İstemci alanı 734 px + başlık çubuğu
  (~31 px) ≈ 765 px; 1366×768 bir dizüstünde çalışma alanı ~728 px (görev
  çubuğu 40 px). %100 ölçekte bile alt düğmeler ve ödeme satırı ekran dışında
  kalabilir; %125'te pencere 956 px olur.
- **8 test sitesinde taşma:** satır aralığı 15 px, son satır y=533–551,
  sayaç satırı y=549'da başlıyor → 2 px çakışma; 12 px yazı için 15 px satır
  da sıkışık (önerilen ≥ 20 px).
- **Izgara tutarsız:** kenar 20 px, kart arası 8 px, düğme arası 8, bölüm
  arası 16/14/32/12 karışık; yarıçaplar 8/10/12/24 (pill).
- **Tipografi 7 boy/ağırlık:** 20B, 12, 22B, 13, 12, 10B, 14SB. 10 px kalın
  büyük harf etiketler küçük ve Türkçe büyük harfte (İ, Ğ) sıkışık.

### 4.3 Renk ve kontrast (WCAG, hesaplanmış)
| Çift | Oran | AA (4.5) |
|---|---|---|
| Ana metin #ECEEF3 / zemin | 15.98 | ✔ |
| İkincil #8A92A6 / kart | 5.34 | ✔ |
| **Soluk etiket #5C6375 / kart** (AĞ, BAĞLANTI, TEST SİTELERİ, DNS, sayaçlar) | **2.77** | ✘ |
| **"engelli" kırmızısı #EF4444 / kart** | **4.42** | ✘ (sınırda) |
| **"Test et" beyaz / marka kırmızısı #E5293A** | **4.46** | ✘ (sınırda) |
| Kart kenarı #272D3B / kart | 1.21 | dekoratif, sorun değil |
| Bant metinleri (beyaz / lacivert, kahve, bordo) | 9–10 | ✔ |

- **Durum renkleri tutarsız:** SCANNING pencerede mavi, tepside sarı;
  STANDBY pencerede açık mavi, tepside yeşil; ERROR pencerede kırmızı, tepside
  koyu lacivert (koyu görev çubuğunda neredeyse görünmez).
- **Durum yalnız renkle:** halkada ikon yok; renk körü kullanıcı ACTIVE
  (yeşil) ile PARTIAL (amber) ayrımını yalnız başlıktan yapabiliyor (başlık
  var, bu yüzden "kısmen" kabul edilebilir, ama tepside hiç metin yok).
- **Marka kırmızısı her yerde:** başlık parıltısı, birincil düğme, logo.
  Kırmızı aynı zamanda hata rengi; "Test et" düğmesi ve hata durumu aynı aileden.

### 4.4 Etkileşim ve erişilebilirlik
- **Klavye ile yalnızca site kutusuna ulaşılıyor.** Güç düğmesi, Yeniden tara,
  Profil, Ayarlar, bant: Tab ile odaklanamıyor, odak göstergesi yok.
- **Ekran okuyucu hiçbir düğmeyi görmüyor** (elle çizim, alt pencere yok,
  `WM_GETOBJECT` yok). Pencere metni yalnızca "TurkishDPI".
- **Güç düğmesinin anlamı belirsiz:** ACTIVE'de tıklamak duraklatır, ERROR'da
  da duraklatır; ipucu (tooltip) veya etiket yok.
- **Site testi sonucu** tek satır, kutunun altında, kısaltılıyor (ellipsis).

### 4.5 Metin dili
- Hata başlıkları teknik: "Sürücü başlatılamadı (5)" — kod önde, ne
  yapılacağı açıklamada. "Günlük dosyasına bakın" kullanıcıyı dosyaya yolluyor.
- Uyarılarda "–" ile birleştirilmiş uzun tek satırlar 400 px'te kesiliyor
  (ör. proxy metni ~70 karakter).
- Yer yer ton farkı: "TurkishDPI çözümü kendisi arasın" (konuşma dili) /
  "Hiçbir profil tüm test sitelerini açamadı" (teknik).

### 4.6 Simge ve tepsi
- Tepside 4 renkli nokta **kırmızı kalkanın üstüne** biniyor; 16 px'te kalkan
  kırmızısı her durumda baskın, nokta ~6 px. Kırmızı = hata çağrışımı.
- Simgede parlaklık (gloss) ve gölge var: 16 px'te bulanıklaşıyor.
- Tepsi ipucu "TurkishDPI – Koruma aktif"; ağ/hız bilgisi yok (128 karakter yeter).
- Bildirim tonu iyi (sessiz, `NIIF_NOSOUND`, tekrar engelleme var).

### 4.7 Teknik notlar (tasarımı etkileyen)
- `paint()` her karede bütün ekranı çiziyor; animasyon sırasında yalnız
  `r_power` geçersiz, BitBlt kırpılıyor ama hesaplama tam. 30 fps'te ölçülmeli.
- `g_banner_kind` `paint()` içinde atanıyor ve `hit()` onu kullanıyor: ilk
  çizimden önce tıklama mümkün değil, sorun yok, ama durum çizimden türetilmiş.
- `apply_dpi` fontları yeniden yaratıyor; eskiler siliniyor (sızıntı yok).
- Açık tema yok; `dark_menus` uygulama genelinde koyu menü zorluyor.

## 5. Öncelik sırası (tasarım açısından)

1. 768 px ekrana sığmama (kullanıcı alt düğmeleri göremiyor).
2. Ana ekranın teknik ayrıntıyla dolu olması (asıl soru kayboluyor).
3. AA altı kontrast (soluk etiketler) ve renk tutarsızlığı (tepsi ≠ pencere).
4. Klavye ile erişilemeyen düğmeler.
5. Ayarlar menüsünün karışıklığı, kaldır/çıkış komşuluğu.
6. Tepsi simgesinin 16 px'te okunabilirliği.
