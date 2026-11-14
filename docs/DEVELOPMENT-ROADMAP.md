> **TARİHSEL BELGE – sürüm 1.1.0'ın anlık görüntüsü.** Bu belge güncellenmemiştir; yazıldığı zamanın (v1.1.0) kodunu anlatır. Sürüm 2.0'dan beri motor TurkishDPI'ın kendi motorudur (Shroudly çekirdeği, `src/engine/`); aşağıda adı geçen `goodbyedpi.c`, `dnsredir.c` gibi dosyalar artık yoktur. Maddelerin durumu bu belgede izlenmez. Güncel durum için [CLAUDE.md](../CLAUDE.md) ve [README.md](../README.md) dosyalarına bakın.

# TurkishDPI – Geliştirme Yol Haritası

> Dayanak: [PROJECT-ANALYSIS.md](PROJECT-ANALYSIS.md), [SECURITY-REVIEW.md](SECURITY-REVIEW.md),
> [ARCHITECTURE.md](ARCHITECTURE.md). Her madde ayrı bir `feature/<konu>` branch'inde, küçük ve
> gözden geçirilebilir commit'lerle yapılır. `main`'e merge, etiket ve release proje sahibinin
> onayıyla olur. Hata düzeltme, antivirüs ve sürüm yayınlama diğer oturumun sorumluluğunda;
> çakışma riski olan maddeler (★) için önce koordinasyon gerekir.

## Uygulama durumu (branch `feature/hardening-autopilot`, merge edilmedi)

| Madde | Durum |
|---|---|
| 1 Güvenli kurulum + DLL sertleştirmesi | **Yapıldı:** `dnsredir.c` System32; `sysdll.c` (winhttp/iphlpapi çalışma zamanında, `SetDefaultDllDirectories`); `WM_COMMAND` UIPI istisnası kaldırıldı; WinDivert.dll SHA-256 sabitleme; otostart → `Program Files\TurkishDPI` kopyası ve eski görevlerin taşınması; kaldırma bu kopyayı da siliyor. **Test edilmedi:** Program Files kopyalama ve görev kaydı (yönetici gerekiyor). Exe klasörü ACL uyarısı yapılmadı. |
| 2 Sakin ve açıklanabilir Autopilot | **Yapıldı:** iki-vuruş bekçi, bildirim politikası, ağ değişimi debounce, aynı argümanlarla motoru yeniden başlatmama, tarama sırasında motoru açık tutma, engel IP öğrenimi 24 saatte bir, karar gerekçesi (log + pencere), log rotasyonu yazma anında, `WatchdogMinutes` 1–240, bekçi başarılarının öğrenmede saatte bir sayılması. **Yapılmadı:** UI'da son 20 olay listesi. STANDBY değişikliği gerekmedi (bkz. PROJECT-ANALYSIS §3 düzeltmesi). |
| 3 Tanılama | **Kısmen:** `diag.c` sınıflandırıcı + `probe_collect_facts` + site testinde kullanım (IP engeli ve alan adı yok için preset taraması yapılmıyor). **Yapılmadı:** ayrı tanılama ekranı, SNI'siz TLS denemesi ile kesin SNI kanıtı, bekçi/öğrenmede neden kaydı (madde 6). |
| 4 Ödeme koruması ölçümü | **Yapılmadı.** Yönetici + DoH açık tarayıcı ile ölçüm gerekiyor. |
| 5 Test + CI | **Yapıldı:** `diag_test`, `windivert_hash_test`, Linux'ta ASan/UBSan işi, Action'lar SHA'ya sabit, yazma izni yalnızca release işinde, statik import kontrolü, `REQUIRE_WINDIVERT_HASH=1`. **Yapılmadı:** `WinDivertHelperCompileFilter` testi, fuzzing, `-Werror`, köken kanıtı. |
| 16 Prob/gizlilik | **Yapıldı:** nötr User-Agent, bağlı DNS soketi, rastgele TXID, README'de hibrit DNS gizlilik notu. **Yapılmadı:** bekçide sistem DNS önbelleğini silmemek. |
| 17 Belge tutarlılığı | **Yapıldı:** README çelişkileri (#1–#4, #6, #7, #11), CLAUDE.md güncellendi. |

## Öncelik ölçütü

Değer (güvenlik > kullanıcıyı etkileyen kararlılık > yeni özellik) ÷ maliyet ve risk.
"Ölç" ile başlayan maddelerde kod, ölçüm sonucuna göre yazılır. Ölçüm yapılmadan özellik
iddia edilmez.

## İlk 5 (en yüksek değer)

### 1. Güvenli kurulum ve DLL sertleştirmesi
- **Ne işe yarar:** Yetkisiz bir sürecin TurkishDPI üzerinden UAC'sız yönetici olmasını
  engeller (S1, S2, S3).
- **Kapsam:**
  - `dnsredir.c` System32'den yükleme;
  - `SetDefaultDllDirectories`;
  - statik `winhttp`/`iphlpapi` importlarının çalışma zamanında yüklenmesi ya da bağımlılık
    bayrağı;
  - `WM_COMMAND` UIPI istisnasının kaldırılması;
  - "Windows ile başlat" seçilince `%ProgramFiles%\TurkishDPI`'a güvenli kopya;
  - exe klasörü ACL kontrolü;
  - `WinDivert.dll` SHA-256 doğrulaması (CI'daki sabit değer derleme zamanında gömülür).
- **Kullanıcıya faydası:** Görünmez ama kritik. Bilgisayardaki herhangi bir kötü amaçlı
  program TurkishDPI'ı yönetici olmak için kullanamaz.
- **Mimariye etkisi:** Küçük. Kurulum klasörü kavramı eklenir; servis mimarisine de zemin
  hazırlar.
- **Zorluk:** Orta (S/M). Statik importları ayıklamak ve kopyalama/güncelleme mantığı en uzun kısım.
- **Güvenlik/performans:** Güvenlik ++; performans etkisi yok. Antivirüs: Program Files'a
  kopyalama normal kurulum davranışıdır, ama ★ diğer oturumla VirusTotal'da kontrol edilmeli.
- **Öncelik:** P0.

### 2. Sakin ve açıklanabilir Autopilot
- **Ne işe yarar:** Gereksiz taramaları, koruma kesintilerini ve bildirim yağmurunu
  azaltır; her kararın gerekçesini kaydeder.
- **Kapsam:**
  - **İki-vuruş kuralı:** bekçi başarısız olursa önce yalnızca başarısız siteyi 30–60 s
    sonra yeniden dene, kontrol sitesiyle karşılaştır. Tam tarama ancak tekrarlanırsa.
  - **Bildirim politikası:** yalnızca anlamlı geçişlerde (ör. ERROR→ACTIVE,
    ACTIVE→PARTIAL, OFFLINE→ACTIVE yalnızca 5 dakikadan uzun kesintiden sonra) ve oran
    sınırıyla. Çökme sonrası yeniden başlatmada balon yok.
  - **Ağ değişimi debounce:** 4 s `Sleep` yerine bekleme süresi dolana kadar gelen olayları
    birleştir; komut kuyruğu bu sırada bloke olmasın.
  - **Engel IP öğrenimi önbelleği:** ağ başına 24 saat geçerli olsun. Her taramada motoru
    durdurmak gerekmesin; mümkün olduğunda **çalışan motor kapatılmadan** önce yeni aday
    test edilsin, kesinti yalnızca preset değiştiği an olsun.
  - **Karar günlüğü:** yapılandırılmış olaylar, ör.
    `scan reason=watchdog_fail site=wattpad.com cause=tls_timeout → preset 4 (182 ms)`.
    Log'a ve UI'ya (son 20 olay) gider.
  - **STANDBY güvenliği:** test siteleri motorsuz açılsa bile varsayılan davranış "hafif
    preset açık kalsın" olsun. STANDBY'ı kullanıcı seçebilsin. Gerekçe: üç sitenin engelden
    çıkması diğer sitelerin de çıktığını kanıtlamaz.
  - Log rotasyonu yazma anında (1 MB); `WatchdogMinutes` sınırları (1–240).
- **Kullanıcıya faydası:** Discord/oyun bağlantıları gereksiz yere kopmaz. "Neden?"
  sorusunun cevabı görünür. Balonlar rahatsız etmez.
- **Mimariye etkisi:** `policy` mantığının `app.c`'den ayrılması için ilk adım (ARCHITECTURE §8.3).
- **Zorluk:** Orta.
- **Güvenlik/performans:** Daha az prob trafiği ve motor yeniden başlatması → daha az CPU
  ve kesinti. Güvenlik nötr.
- **Öncelik:** P0.

### 3. Tanılama ve dürüst sınıflandırma
- **Ne işe yarar:** Bir site neden açılmıyor, adım adım ayırır:
  - internet;
  - sistem DNS cevabı ile temiz DNS cevabının karşılaştırması (zehirleme);
  - çözülen IP'lere TCP bağlantısı (IP engeli: SYN zaman aşımı/RST, kontrol siteleri
    açıkken);
  - TLS (gerçek SNI ile başarısız, nötr SNI ile aynı IP'ye başarılı → SNI engeli);
  - HTTP durumu (sunucu hatası);
  - yerel engeller: sürücü, yetki, çakışan araç, bozuk proxy.
- **Kullanıcıya faydası:**
  - "DPI ile aşılamaz, IP engeli: VPN gerekir" ya da "site kendisi çökmüş" gibi kesin
    cevaplar alır.
  - Gereksiz preset taraması yapılmaz: IP engelinde tarama anlamsız.
- **Mimariye etkisi:** `classify.c` (core). Site testi, bekçi ve `smart_record` aynı
  sınıflandırmayı kullanır, böylece öğrenmeye başarısızlık nedeni girer (madde 6).
- **Zorluk:** Orta. WinHTTP'de SNI'yi değiştirmek doğrudan desteklenmiyor. TLS testi için ya
  ham soket + elle ClientHello (yalnızca el sıkışmanın başlangıcı yeterli: ServerHello mi
  RST mi?) ya da `WINHTTP_OPTION_SERVER_CERT_CONTEXT` dışı bir yol gerekir. Ham
  ClientHello'nun en hafif ve tam kontrollü yol olduğu düşünülüyor.
- **Güvenlik/performans:** Tanılama yalnızca istek üzerine ya da bekçi başarısızlığında
  çalışır. Dışarı veri gönderilmez.
- **Öncelik:** P1.

### 4. Ödeme korumasının ölçülmesi ve güçlendirilmesi
- **Ne işe yarar:** DoH açık tarayıcılarda ve bölünmüş (post-quantum) ClientHello'da
  banka trafiğine dokunulmadığını garanti eder (S6).
- **Kapsam:** Önce ölç: DoH açık Chrome/Edge/Firefox ile 3 banka sitesi, motor `excluded`
  sayacı ve log. Delik doğrulanırsa motorda ilk 2–3 segment için ClientHello birleştirme
  (4'lü anahtar, kısa ömürlü tablo, sınırlı bellek). Birim testleri ve ASan/UBSan
  (`extract_sni` + birleştirme).
- **Kullanıcıya faydası:** 3D Secure ve ödeme hatası riski azalır. Ürünün en önemli
  güvence vaatlerinden biri gerçekten doğru olur.
- **Mimariye etkisi:** Motor paket döngüsünde küçük bir durum tablosu. Filtre değişmez
  (TLS kayıtları zaten yakalanıyor; devam segmentleri `16 03` ile başlamaz, bu yüzden
  filtrenin **genişletilmesi gerekebilir**. Her yakalanan paket iki bağlam geçişi demek,
  dikkatle ölçülmeli. Alternatif: sahte paketi bir sonraki segmente ertelemek yerine,
  ClientHello'nun `record length`'ine bakıp SNI'nin bir sonraki segmentte olduğu tahmin
  edildiğinde **hiçbir şey yapmamak** (güvenli taraf) ve bunu sayaçla izlemek.)
- **Zorluk:** Orta-Yüksek (motor).
- **Güvenlik/performans:** Güvenlik ++. Performans: filtre genişlerse ölçülmeli.
- **Öncelik:** P1 (ölçüm P0).

### 5. Test altyapısı ve CI sertleştirmesi
- **Ne işe yarar:** Sonraki büyük değişiklikleri (servis, Linux, motor) güvenli yapmayı sağlar.
- **Kapsam:**
  - Linux/MinGW üzerinde app-katmanı birim testleri: `split_hosts`, `normalize_host`,
    `build_args`, `xml_escape`, `version_newer`, `dns_answer_ok`, `smart` sıralaması.
  - CI'da ASan/UBSan (Linux'ta clang ile taşınabilir core kodu için kolay).
  - `WinDivertHelperCompileFilter` ile her presetin ürettiği filtrenin derlendiğinin
    kontrolü (yönetici gerekmez).
  - DNS ve SNI ayrıştırıcıları için fuzz hedefleri (libFuzzer).
  - Action'ları SHA'ya sabitleme; `contents: write` yalnızca release işinde.
  - `-Werror` (önce uyarıları temizleyerek).
  - Derleme köken kanıtı (`attest-build-provenance`).
- **Kullanıcıya faydası:** Dolaylı: daha az gerileme, daha güvenilir sürümler.
- **Mimariye etkisi:** core ayrımını teşvik eder.
- **Zorluk:** Küçük-Orta.
- **Güvenlik/performans:** Tedarik zinciri güvenliği +.
- **Öncelik:** P0/P1. ★ CI dosyası diğer oturumun release akışını etkiler; değişiklik
  önce onunla koordine edilmeli.

## Sonraki adaylar

| # | Ad | Ne işe yarar / kullanıcı faydası | Mimariye etkisi | Zorluk | Güvenlik / performans | Öncelik |
|---|---|---|---|---|---|---|
| 6 | **Öğrenme v2** | Ağ başına istatistikte başarısızlık **nedeni** (DNS/IP/SNI/TLS/sunucu), **zamana göre eskime** (yarı ömür ör. 14 gün), bekçi sonuçlarının ayrı ve düşük ağırlıkla tutulması, kısmi skorların (2/3) kaydı, ağ kimliğine ISS ipucu (genel IP'nin ilk /16'sı veya ASN, **yalnızca yerel**, dış servise sorulmadan; ör. ters DNS yerine yerel ağ geçidi + IPv6 önek), 90 gün kullanılmayan ağların silinmesi. Daha az deneme, daha doğru ilk tahmin. | `learn.c` (core); ini formatı geriye uyumlu genişler (`pN=trials,ok,ms` + yeni anahtarlar) | Orta | Nötr; daha az tarama | P1 |
| 7 | **Bağlantı kalitesi ölçümü** | Yalnızca "açıldı mı"ya değil, el sıkışma süresinin dağılımına (medyan, p90), hata oranına ve motor yükü farkına bakmak. Preset seçiminde hız ile kararlılık dengesi. | `policy`/`learn` | Küçük-Orta | Ölçüm yalnızca tarama/bekçide; ek trafik minimal | P2 |
| 8 | **Servis + yetkisiz tepsi** | S1/S3/S4/S5'i kökten çözer; oturum öncesi koruma; doğru kullanıcı bağlamı. | Büyük (ARCHITECTURE §4) | Yüksek | Güvenlik ++; antivirüs etkisi ★ ölçülmeli | P1 (1–5'ten sonra) |
| 9 | **Arayüz: sade/ayrıntılı görünüm** | Sade: tek cümlelik durum + "neden" satırı + büyük düğme. Ayrıntılı: karar günlüğü, tanılama sonuçları, DNS, öğrenilen ağlar, sayaçlar. Küçük ekranlar için kaydırılabilir/kompakt düzen; klavye erişimi. GDI/GDI+ korunur, yeni UI teknolojisi gerekmez. | `ui.c` bölünür (görünüm başına dosya) | Orta | Pencere kapalıyken maliyet değişmez | P1 |
| 10 | **Discord teşhisi + (ölçümle) güncelleyici relay'i** | Önce ayrı ayrı ölçüm: tarayıcıda discord.com; masaüstü ana uygulama (gateway WSS); güncelleyici (Stable/PTB/Canary ayrı); ses (UDP). Relay yalnızca güncelleyici sorunu doğrulanırsa ve TLS 1.3 sorunu (Win10 SChannel'da TLS 1.3 yok) çözülürse yapılır: Win11-only ya da mbedTLS. | Yetkisiz tepsi bileşeni olarak (8 ile iyi oturur) | Orta-Yüksek | Kullanıcının `settings.json`'una yazma: yalnızca onayla, geri alınabilir, kullanıcının kendi değerine dokunmadan | P2 |
| 11 | **Discord ses (UDP) denemesi** | Shroudly'nin yaklaşımı: ses çağrısının ilk IP-discovery (74 bayt) / STUN datagramlarından önce düşük TTL'li sahte datagram. Dar bir UDP filtresiyle (yalnızca bu imzalar) bağlam geçişi maliyeti düşük tutulur. **Önce Türk ISS'lerinin sesi gerçekten DPI ile kesip kesmediği ölçülmeli.** | Motor (yeni UDP yolu) | Orta | Filtre dar tutulmazsa performans riski | P3 (ölçüme bağlı) |
| 12 | **Oyun/uygulama tanılaması** | Roblox, Riot (League/VALORANT) vb. için alan adı setleriyle (tanılama 3'ü kullanır) DNS/IP/SNI/UDP/IPv6 ayrımı. Proxy tabanlı "Games mode" **gerekmiyor** (TurkishDPI paket seviyesinde). Riot Vanguard ve WinDivert birlikte çalışıyor mu, **test edilmeden iddia edilmemeli**. | `classify` + hazır profil listeleri (veri) | Küçük-Orta | Nötr | P2 |
| 13 | **İmzalı güncelleme** | İndirme + Ed25519/minisign imza doğrulaması + yönetici-only klasör + geri dönüş + kullanıcı onayı (S9). Kod imzalama sertifikası ayrıca sahibin kararı. | Servis ile daha temiz; servis olmadan da yapılabilir | Orta-Yüksek | Yanlış yapılırsa en büyük risk; S9 şartları zorunlu ★ | P2 |
| 14 | **Temiz DNS için DoH seçeneği** | Uygulama problarında DoH (WinHTTP) kolay. Motor seviyesinde DoH için motorun DNS cevabını DoH'tan üretmesi gerekir (Shroudly advanced mode böyle yapıyor), bu ağır. Önce yalnızca prob/tanılama tarafı, sonra motor için değerlendirme. | dnsmsg + probe | Orta (prob) / Yüksek (motor) | Gizlilik ve bütünlük + | P3 |
| 15 | **Linux daemon (`nfqws` adaptörü)** | Linux kullanıcılarına aynı akıllı katman. zapret MIT, NOTICE'e eklenir. | ARCHITECTURE §6; core ayrımı önkoşul | Yüksek | `CAP_NET_ADMIN/RAW` ile root'suz | P3 |
| 16 | **Prob ve gizlilik temizlikleri** | Nötr User-Agent, `connect()`'li DNS probu + rastgele TXID, bekçide sistem DNS önbelleğini silmek yerine `BYPASS_CACHE`, README'de hibrit DNS gizlilik notu. | Küçük | Küçük | Gizlilik + | P1 |
| 17 | **Belge tutarlılığı** | PROJECT-ANALYSIS §13'teki 11 çelişkinin düzeltilmesi (README ve CLAUDE.md). | – | Küçük | – | P1 ★ (README diğer oturumun release notlarıyla çakışabilir) |
| 18 | **macOS** | Ertelenir. İlk adım yalnızca tanılama/CLI; paket motoru yok. | – | – | – | P4 |

## Bilinçli olarak önerilmeyenler

- **Electron/WebView2 arayüzü:** Bellek ve boyut avantajını yok eder, yeni bir antivirüs
  yüzeyi açar. GDI/GDI+ ile sade/ayrıntılı görünüm yeterli.
- **Shroudly'nin "Games and Apps" ortam değişkeni proxy'si ve URL handler devralma:**
  TurkishDPI'da bu sorun yok; eklemek gereksiz karmaşıklık ve kullanıcı ortamını kirletir.
- **Antivirüs istisnası ekleme, packer, gizleme:** Pazarlıksız kurallara aykırı. Meşru
  yollar: kod imzalama, şüpheli davranışın kaldırılması, yanlış tespit bildirimi (diğer
  oturum).
- **Exe'ye sürücü gömme:** Daha önce denendi, tespitleri artırdı (CLAUDE.md).
- **Telemetri ile "topluluk öğrenmesi":** Kurallara aykırı. Bunun yerine *isteğe bağlı,
  elle dışa aktarılan* anonim bir tanılama raporu (kullanıcı kendisi paylaşır) düşünülebilir.

## Fikir kaynakları ve lisanslar

| Proje | Lisans | Alınabilecek fikir |
|---|---|---|
| GoodbyeDPI (ValdikSS) | Apache-2.0 | Zaten temel motor |
| GoodbyeDPI-Turkey (cagritaskn) | Apache-2.0 | Profiller (zaten alınmış) |
| Shroudly (umutxyp) | MIT | Discord güncelleyici relay'i, ses için UDP sahte datagram, IP-engeli dürüstlüğü, ~180 engel IP listesi fikri (liste veri olarak alınırsa NOTICE) |
| zapret (bol-van) | MIT | Linux `nfqws` motoru, `multisplit/seqovl/fakedsplit` teknikleri, blockcheck tarzı sistematik tanılama |
| ByeDPI (hufrea) | MIT | TLS kaydı bölme (`tlsrec`), OOB; proxy mimarisi bizim için gereksiz |
| SplitWire-Turkey (cagritaskn) | MIT | Discord için PTB kurma, Win7/8 uyumluluk modu gibi kullanıcı onarım adımları; WireSock/wgcf (VPN) yaklaşımı TurkishDPI vizyonu dışında |

Bu tablodaki lisanslar bu oturumda GitHub API/README üzerinden kontrol edildi. Kod alınırsa
o anki LICENSE dosyası yeniden doğrulanmalı.

## Ölçüm listesi (kod yazmadan önce, yönetici izni ve kullanıcı onayı gerekir)

1. `--selftest` (mevcut temel çizgi).
2. DoH açık tarayıcı + banka siteleri → `excluded` sayacı (madde 4).
3. Discord: tarayıcı / masaüstü ana uygulama / güncelleyici (Stable, PTB, Canary ayrı) /
   ses (madde 10, 11).
4. Motor başlatma süresi (CreateProcess → `Filter activated`) ve tam tarama süresi.
5. Uzun süreli çalışma: 24 saat sonunda çalışma kümesi, handle sayısı, log boyutu.
6. Process Monitor ile exe klasörü DLL aramaları (S2 doğrulaması).
7. Medium IL süreçten `WM_COMMAND IDM_EXIT` (S3 doğrulaması).
