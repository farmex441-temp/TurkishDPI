# TurkishDPI – Proje Analizi (v1.1.0, `main` @ 55b6244)

> Kapsam: kaynak kodun tamamı (`src/app`, `src/engine`, `tests`, CI, Makefile) okunarak hazırlandı.
> Mimari ve kuralların ana kaynağı [`CLAUDE.md`](../CLAUDE.md); burada tekrar edilmez, yalnızca
> koda karşı doğrulanır. Güvenlik bulguları ayrıntılı olarak [SECURITY-REVIEW.md](SECURITY-REVIEW.md),
> mimari [ARCHITECTURE.md](ARCHITECTURE.md), öncelikler [DEVELOPMENT-ROADMAP.md](DEVELOPMENT-ROADMAP.md).
>
> **Bu analizde hiçbir test çalıştırılmadı** (bu makinede derleyici yok: `gcc`, `clang`,
> `x86_64-w64-mingw32-gcc` PATH'te bulunamadı; ağ/sürücü testleri yönetici izni ister).
> "Ölçülmeli" diye işaretlenen her şey varsayımdır, doğrulanmış değildir.

## 0. Özet

TurkishDPI küçük, disiplinli ve bilinçli yazılmış bir C/Win32 projesi. Güçlü yanları:
dar WinDivert filtresi, hibrit DNS, ödeme koruması, yükseltilmiş kod için alınmış dikkatli
önlemler ve dürüst belgeler. Zayıf yanları üç başlıkta toplanıyor:

1. **Güven sınırı:** Tüm uygulama (GDI arayüzü dahil) yönetici yetkisiyle çalışıyor. Exe ise
   kullanıcının zip'i açtığı, büyük olasılıkla kullanıcının yazabildiği bir klasörde duruyor.
   Görev Zamanlayıcı da bu exe'yi UAC sormadan yönetici olarak başlatıyor. Bu, projedeki en
   önemli açık (SECURITY-REVIEW S1).
2. **Otopilotun sesi ve kesintileri:** Her yeniden başlatmada balon bildirimi çıkıyor. Her
   taramada motor durduruluyor (koruma kısa süre kesiliyor). Geçici bir hata yüzünden tam
   tarama başlıyor. Kararların gerekçesi kullanıcıya gösterilmiyor.
3. **Test ve ayrıştırılabilirlik:** Uygulama mantığı (tarama politikası, öğrenme, DNS seçimi)
   Win32 çağrılarıyla iç içe; birim testi yok. CI yalnızca iki motor testini çalıştırıyor.

## 1. Genel mimari ve modül ilişkileri

Gerçek yapı (kodla doğrulandı):

```
main() ─┬─ "--engine ARGS" / "-x…" ─────────> gdpi_main()   (motor, ayrı süreç)
        ├─ "--selftest" ──> app_main(quiet)  ─> controller thread + selftest_finish()
        └─ (yok) / "--tray" ─> app_main()
              ├─ sys_secure_dir()  (ProgramData klasörü, DACL)
              ├─ check_conflicts() (rakip servisler, MessageBox)
              ├─ ui_init()         (UI thread = ana thread, mesaj döngüsü)
              ├─ controller()      (tek kontrolcü thread: tarama, bekçi, komutlar)
              │     ├─ probe.c   (WinHTTP + ham UDP DNS, DnsQuery_A)
              │     ├─ smart.c   (ini tabanlı öğrenme)
              │     ├─ system.c  (rakip araç, proxy, güncelleme, klasör, kaldırma)
              │     └─ engine_start/stop (CreateProcess + Job + pipe okuyucu thread)
              └─ update_thread()   (günde bir GitHub API)
```

**Güçlü yanlar**
- Tek exe, iki rol: dağıtım basit ve GoodbyeDPI `.cmd`/`sc create` uyumluluğu korunmuş.
- Motor ayrı süreçte; çökerse uygulama ayakta kalıyor ve Job nesnesi
  (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`) yetim motor bırakmıyor.
- UI ile kontrolcü yalnızca kilitli bir komut kuyruğu (`post_cmd`, 16 slot) ve `g_ui`
  anlık görüntüsü üzerinden konuşuyor; çizim hiçbir zaman kontrolcüyü beklemiyor.
- Bağımlılık yok: GDI+ flat API elle bildirilmiş, WinHTTP ve iphlpapi sistemden geliyor.

**Zayıf yanlar**
- `app.c` (1393 satır) her şeyi yapıyor: config, log, süreç yönetimi, tarama politikası,
  site testi, otostart XML, çakışma kontrolü, giriş noktası. Politika ile mekanizma ayrı değil.
- Paylaşılan durum küresel değişkenlerde (`g_auto`, `g_preset`, `g_paused`, `g_ui`…).
  Bazı alanlar kilitsiz yazılıyor/okunuyor: `g_ui.started_tick`, `g_ui.learned`,
  `g_ui.driver_err` (`announce`), `g_ui.kind` (kontrolcü), `g_ui.st_*` (selftest).
  Kontrolcü tek yazar olduğu için x86'da pratikte zararsız ama C açısından veri yarışı.
- Platformdan bağımsız mantık yok. `smart.c` sıralama formülü, `auto_scan` politikası,
  `pick_dns` histerezisi ve `dns_answer_ok` saf mantık olduğu halde Win32 ile karışık;
  Linux/macOS'ta yeniden kullanılamaz ve birim testi yazılamaz.

## 2. Uygulama ↔ motor iletişimi

Protokol: motor stdout satırları → pipe → `engine_reader` thread. Anlamlı satırlar:
`Filter activated`, `Error opening filter: N` ve 2 saniyede bir (değiştiyse)
`STATS packets=… https=… http=… excluded=… dns=…`.

**Güçlü yanlar**
- Basit, sağlam, satır uzunluğu sınırlı (514 bayt; uzun satır bölünüyor, taşma yok).
- Motor `CREATE_SUSPENDED` ile başlatılıp Job'a eklendikten sonra devam ettiriliyor;
  arada yetim süreç kalamıyor.
- Başlatma hatasında okuyucunun pipe'ı bitirmesi bekleniyor, böylece hata kodu kaybolmuyor.
- WinDivert hata kodları (2, 5, 87, 126, 577, 654, 1275, 1753) Türkçe açıklamalara
  eşlenmiş.

**Zayıf yanlar**
- Durdurma `TerminateProcess` ile yapılıyor. WinDivert tutamaçları çekirdekte kapanıyor, bu
  güvenli; ama motorun temiz kapanma yolu (`deinit_all`) hiç çalışmıyor. Ayrıca her preset
  denemesi tam bir süreç başlatma demek: tarama başına 10'a kadar `CreateProcess` +
  sürücü filtresi açma. Ölçülmeli: motor başlatma süresi (`Filter activated`'a kadar geçen
  süre) logda yok.
- Protokol tek yönlü. Uygulama motora "argümanları değiştir", "istatistik ver" ya da
  "temiz kapan" diyemiyor; her değişiklik yeniden başlatma gerektiriyor.
- Motor uyarıları ve hataları (ör. `WARNING: can't load exclude list`) log'a gidiyor ama
  UI'da görünmüyor.
- Paket döngüsünde `WinDivertRecv` hatası → döngüden çıkış → `exit 1` → uygulama bunu çökme
  sayıyor (10 dakikada 3 deneme, sonra tam tarama). Uykudan dönüşte bu yol tetiklenebilir;
  log'da ayırt edilmiyor.

## 3. Otomatik strateji seçimi ve öğrenme

Kodla doğrulanan akış (`auto_scan`, `app.c:736`):
1. `engine_stop()` → **koruma kapanır**, DNS önbelleği temizlenir, ağ kimliği okunur,
   rakip/proxy kontrolü.
2. Kontrol siteleri (google, microsoft) açılmıyorsa → OFFLINE (30 s'de bir tekrar).
3. `pick_dns`: 6 temiz DNS paralel, ikişer sorgu; kayıtlı olan %20'den fazla yavaş
   değilse korunur (histerezis).
4. Hibrit açıksa `learn_block_ips` (motor kapalıyken, 5 kanarya alan adı).
5. Presetler `smart_order` sırasıyla denenir; her deneme: motor başlat → 3 hedef sitede
   TLS el sıkışması (5 s zaman aşımı, 1 tekrar).
6. Yeni ağda ilk başarılı presetten sonra en fazla 2 aday daha ölçülür; %20 daha hızlı olan
   kazanır. Bilinen ağda kanıtlanmış preset ilk denemede çalışırsa tarama durur.
7. Hiçbiri tam çalışmazsa en iyi kısmi skor; o da yoksa motorsuz test → STANDBY;
   o da yoksa son preset + PARTIAL/ERROR.

**Güçlü yanlar**
- İnternet yokken presetler suçlanmıyor (kontrol siteleri önce test ediliyor).
- Gecikme ölçümü dürüst: yalnızca TCP+TLS kurulum süresi (WinHTTP durum geri çağrısı ile).
  DNS ve sunucu süresi ölçüme dahil değil.
- Laplace düzeltmeli başarı oranı + hız bonusu + elle belirlenmiş sıra cezası, küçük ve
  anlaşılır bir sıralama.
- PARTIAL modda yalnızca durum kötüleşirse yeniden tarama yapılıyor (sonsuz tarama yok).

**Zayıf yanlar / riskler**
- **Her taramada koruma kesiliyor.** Bekçi ya da ağ değişimi tetiklediğinde motor durur,
  DNS ölçülür, engel IP'leri öğrenilir, presetler denenir. Kullanıcı bu sırada
  (5–60 s, en kötü durumda 3 dakikadan fazla) engelsiz kalır; Discord gibi uzun ömürlü
  bağlantılar kopar.
- **Tek başarısızlık tam tarama demek.** `health_ok` başarısız olursa (ör. wattpad.com o an
  çöktüyse) kontrol sitesi testi sonrası doğrudan `auto_scan` çalışıyor. "İki kez üst üste
  başarısızlık" ya da "yalnızca o siteyi yeniden dene" kuralı yok.
- **Başarısızlığın nedeni kaydedilmiyor.** `smart_record(p, ok, ms)` yalnızca 0/1 tutuyor.
  DNS zehirlenmesi, RST, TLS zaman aşımı, sunucunun çökmesi ve IP engeli aynı "başarısız"
  sayılıyor; hedef sitenin kendi arızası presetin hanesine yazılıyor.
- **Bekçi kayıtları istatistiği bozuyor.** Her 10 dakikada bir `smart_record(running, s, 0)`
  çağrılıyor (saatte 6 deneme). 20 denemede yarıya indirme ile çalışan preset hızla
  ~%100'e yaklaşıyor. Hiç denenmemiş bir alternatifin önceliği ise 0,5'te kalıyor, yani
  "zengin daha zengin" etkisi oluşuyor. Unutma (yarıya indirme) zamana değil deneme
  sayısına bağlı: 3 ay kullanılmayan bir ağın bilgisi aynen kalıyor.
- ~~**STANDBY riski.**~~ **Düzeltme (kod aşamasında yeniden incelendi):** İlk analizde
  "test siteleri motorsuz açılırsa motor kapanır" demiştim; bu **yanlıştı**. Test siteleri
  engelsizse her preset tam skor alır ve bir preset aktif kalır. STANDBY'a yalnızca hiçbir
  preset hiçbir test sitesini açamıyor ama motorsuz hepsi açılıyorsa, yani sorunun
  kaynağı motorun kendisiyse girilir. Bu doğru bir davranış. Asıl kalan risk şu: test
  siteleri temsilî değilse (ör. hepsi engelden çıkarsa) bekçi diğer sitelerdeki bozulmayı
  fark etmez. Bunu ancak kullanıcının eklediği sitelerin izlenmesi ölçer.
- **Ağ kimliği zayıf.** Kimlik yalnızca IPv4 ağ geçidinin MAC adresi:
  - IPv6-only ağlarda ya da MAC alınamayınca `unknown`/`gwXXXXXXXX`;
  - aynı modemde ISS değişirse (yedek hat, mobil yedekleme) aynı ağ sayılıyor;
  - telefon hotspot'larında MAC değişebilir (ölçülmeli);
  - ini'de ağ başına bölüm sınırsız birikiyor.
- **Tarama süresi üst sınırı yok.** 10 preset × (≤10 s motor başlatma + 0,3 s + iki deneme
  × 5 s) olunca hiçbir şey çalışmadığında birkaç dakika sürebilir.
- **Bildirim yağmuru riski.** `announce()` her başarılı başlatmada balon gösteriyor. Bu
  durumlar dahil: motor çökmesi sonrası yeniden başlatma (10 dakikada 3'e kadar),
  `CMD_RESTART`, bekçi ve ağ değişimi taramaları. `g_quiet_start` yalnızca açılıştaki ilk
  sonuca kadar geçerli. Sürücü hatası balonları tekrarlamıyor (`g_last_fatal`), ama
  "TurkishDPI aktif" balonu tekrarlıyor.
- Ağ değişiminde kontrolcü `Sleep(4000)` ile bloke oluyor; arka arkaya gelen adres
  değişiklikleri (VPN, Hyper-V/WSL vEthernet, Docker) her biri için 4 s bekleme ve
  bir sağlık testi demek. Debounce yok.

## 4. DNS sistemi

**Güçlü yanlar**
- Hibrit yarış (`dns_handle_incoming_hybrid`): her sorgu hem sistem çözücüsüne hem temiz
  çözücüye gidiyor. İlk güvenilir cevap kazanıyor; ISS'nin sahte/hata cevapları düşüyor;
  CDN-yerel cevaplar korunuyor. Birim testleri var (`tests/dns_test.c`).
- Temiz çözücünün cevapları tam `ip.SrcAddr == X and udp.SrcPort == P` ile yakalanıyor
  (mDNS/QUIC karışmıyor).
- Engel sayfası IP öğrenimi: bir IP'nin ≥2 farklı kanarya alan adı için dönmesi ve temiz
  çözücünün o IP'yi hiç vermemesi gerekiyor. Mantık sağlam. Değerler motor komut satırına
  gitmeden önce `inet_pton` ile doğrulanıyor.
- `-q` QUIC filtresi yalnızca QUIC v1 Initial paketlerini düşürüyor (≥1200 bayt, uzun
  başlık). OpenDNS'in UDP 443 portunu yanlışlıkla kesmiyor.

**Zayıf yanlar**
- **Temiz DNS düz UDP.** İçerik bütünlüğü yok; ISS standart dışı portları da sahteleyebilir.
  Hibrit modda kullanıcının **tüm** sorguları hem ISS'ye hem yabancı bir çözücüye (Yandex,
  AdGuard, OpenDNS) gidiyor; bu gizlilik etkisi README'de söylenmiyor.
- `probe_dns_ms` bağlanmamış UDP soketi kullanıyor (cevabın kaynak adresine bakılmıyor) ve
  TXID tahmin edilebilir (`GetTickCount ^ ThreadId`). Etkisi düşük (bkz. güvenlik S8).
- Bilinmeyen bir engel IP'si öğrenilene kadar ISS'nin daha hızlı sahte cevabı yarışı
  kazanır. Öğrenme yalnızca tarama sırasında ve motor kapalıyken yapılıyor.
- `forged_v4` CGNAT (100.64/10) ve 198.18/15 bloklarını sahte saymıyor (küçük eksik).
  "Boş NOERROR" (NODATA) cevaplarına güveniliyor; ISS AAAA için boş cevap verirse IPv6
  yolu sessizce kaybolur (ölçülmeli).
- Tarayıcıların DoH'u (Chrome "Güvenli DNS", Firefox TRR) sistem DNS'ini tamamen atlıyor.
  Bu durumda hibrit DNS devre dışı kalıyor, ama asıl önemlisi **ödeme korumasının IP
  öğrenimi de devre dışı kalıyor** (bkz. güvenlik S6).

## 5. Bekçi, ağ değişimi, uykudan dönüş, motor çökmesi

- Bekçi: `WatchdogMinutes` (vars. 10; OFFLINE'da 30 s). Rakip araç/proxy kontrolü +
  `health_ok`.
- Ağ değişimi: `NotifyAddrChange` (yalnızca IPv4 adres değişimi). Uykudan dönüş:
  `WM_POWERBROADCAST/PBT_APMRESUMEAUTOMATIC` → ayrı olay; doğru ayrılmış.
- Çökme: 10 dakika içinde 3'e kadar aynı presetle yeniden başlatma, sonra tam tarama
  (otomatik modda).

Zayıflar: §3'teki tek-hata-tam-tarama ve bildirim konuları; IPv6 adres değişimleri
(`NotifyIpInterfaceChange` kullanılmıyor) kaçabilir; `WatchdogMinutes` için üst sınır yok
(`g_watchdog_min * 60000u` ~71.582 dakikanın üstünde taşar). Bekçi her çalışmada
`DnsFlushResolverCache` çağırıyor ve kullanıcının tüm sistem DNS önbelleğini 10 dakikada
bir siliyor. Küçük ama gereksiz bir gecikme maliyeti; yalnızca test edilen adlar için
`DNS_QUERY_BYPASS_CACHE` yeterli olur.

## 6. UI ve kullanıcı etkileşimi

**Güçlü yanlar**
- Pencere kapalıyken neredeyse sıfır maliyet (çalışma kümesi kırpılıyor, zamanlayıcı yok).
- Çift tamponlu çizim, per-monitor DPI v2, koyu başlık/menü, Explorer yeniden başlayınca
  tepsi simgesi geri geliyor (UIPI izni).
- Riskli işlemler onay istiyor (ödeme korumasını kapatmak, kaldırma, proxy onarımı).

**Zayıf yanlar**
- **Karar gerekçesi yok.** Kullanıcı "neden bu profil", "neden yeniden taradı", "hangi
  site neden açılmadı" sorularının cevabını göremiyor. Log satırları da teknik ve kısa.
- **Tanılama ekranı yok.** Site testi tek satırlık sonuç veriyor ve "IP/sunucu engeli
  olabilir" diyerek tahmin yürütüyor; DNS/TCP/TLS ayrımı yapılmıyor.
- "ÖĞRENİLEN 3/10 profil" basit kullanıcıya bir şey ifade etmiyor; ileri kullanıcıya da
  yetersiz.
- Sabit 440×734 DIP pencere: 1366×768 ekranda %125 ölçekle 917 px yükseklik çalışma
  alanına sığmaz (alt düğmeler ekran dışında kalır). Kaydırma ya da kompakt düzen yok.
- Elle çizilmiş düğmeler gerçek kontrol değil: klavye ile gezinme (Tab) ve ekran okuyucu
  erişilebilirliği yok.
- Tek banner slotu: proxy, rakip, güncelleme ve antivirüs aynı anda olursa yalnızca biri
  görünüyor.

## 7. Başlangıç, yetkiler, servis eksikliği

- Manifest `requireAdministrator`. UI, kontrolcü, prob, güncelleme kontrolü ve ini/log
  editörü (Notepad) hepsi yükseltilmiş çalışıyor.
- Otostart: Görev Zamanlayıcı XML, `HighestAvailable`, `InteractiveToken`,
  `Delay PT10S`. XML ProgramData'ya yazılıp siliniyor; içerikler XML-escape ediliyor (iyi).
  Kullanıcı `USERDOMAIN\USERNAME` ortam değişkeni + `GetUserNameW` ile alınıyor. **Standart
  kullanıcı başka bir yönetici hesabının parolasıyla yükselttiğinde (OTS elevation) görev
  yönetici hesabına kaydediliyor**. Proxy kontrolü de o hesabın HKCU'sunu okuyor.
- Gerçek bir Windows servisi yok. Motorun upstream servis modu (`service.c`) yalnızca
  GoodbyeDPI uyumluluğu için var ve uygulamanın akıllı katmanını içermiyor.
- **Exe'nin bulunduğu klasörün güvenliği hiç kontrol edilmiyor.** Ayrıntı: SECURITY-REVIEW S1/S2.

## 8. Güvenlik ve gizlilik (özet)

Ayrıntı [SECURITY-REVIEW.md](SECURITY-REVIEW.md). En önemli üç bulgu:
**S1** kullanıcının yazabildiği klasörden yükseltilmiş otostart (sessiz yetki yükseltme),
**S2** DLL arama sırası (statik importlar ve `LoadLibrary("dnsapi.dll")`),
**S3** `WM_COMMAND` için UIPI istisnası (yetkisiz süreç korumayı kapatabiliyor).
Gizlilik tarafında telemetri yok. Dışarı giden trafik: GitHub API (sürüm), test/kontrol
siteleri, temiz DNS çözücüler. Prob User-Agent'ı (`TurkishDPI-probe`) hedef sitelere
aracın kullanıldığını söylüyor.

## 9. Performans ve bellek

- Motor filtresi dar (yalnızca HTTP istekleri ve TLS el sıkışma kayıtları + gelen
  SYN-ACK/HTTP yanıtları + DNS). Upstream'e göre bağlam geçişi azaltımı belgelenmiş ve
  kodla tutarlı.
- Motor: tek thread'li paket döngüsü + 2 s'de bir uyanan istatistik thread'i.
  `exclude_check_ip` her TLS el sıkışmasında 768 girişlik doğrusal tarama yapıyor
  (önemsiz). DNS conntrack 30 s'de temizleniyor; sınırsız büyüme yok, ancak temizlik
  yalnızca DNS paketi geldiğinde tetikleniyor.
- Uygulama: sürekli thread'ler kontrolcü, UI, güncelleme ve motor okuyucusu; problar
  geçici thread'ler. Bellek iddiası (~4–5 MB) bu oturumda **ölçülmedi**.
- **Log rotasyonu yalnızca açılışta** (`log_rotate` yalnızca `app_main`'de çağrılıyor).
  Aylarca açık kalan bir örnekte log 1 MB sınırını aşarak büyümeye devam eder. Yavaş ama
  sınırsız.
- ini: ağ başına bölüm + `bN` engel IP anahtarları birikiyor; temizleme yok.
- Disk yazımı: her `smart_record` çağrısı `WritePrivateProfileString` ile tüm ini'yi
  yeniden yazıyor (saatte ~6 kez + taramalar). Sorun değil ama SSD için gereksiz.

## 10. Hata yönetimi ve kararlılık

- Ciddi bir kilitlenme (deadlock) yolu bulunmadı: `g_lock` hiçbir zaman süreç beklemesi
  veya MessageBox sırasında tutulmuyor (CLAUDE.md kuralı kodla tutarlı).
- `app_exit` UI thread'inden çağrılıyor ve 5 s'ye kadar kontrolcüyü bekliyor. Uzun bir
  prob sürerken çıkışta pencere 5 s donabilir.
- `check_conflicts` UI oluşmadan önce modal MessageBox gösteriyor. `--tray` açılışında
  sahipsiz bir diyalog çıkabiliyor.
- Mutex `Global\TurkishDPI_app` zaten varsa ve pencere bulunamazsa süreç **hiçbir mesaj
  vermeden** çıkıyor.
- `autostart_set` hata durumunda kontrolcü thread'inden `MessageBoxW(ui_hwnd())`
  çağırıyor. Bu, kontrolcüyü kullanıcı kutuyu kapatana kadar bloke ediyor (bekçi durur).

## 11. Test kapsamı, derleme, CI

- Birim testleri: `exclude_test.c` (11 vaka), `dns_test.c` (sahte cevap tespiti + hibrit
  yarış). `probe_test.c` manuel. **Uygulama katmanı için hiç test yok**: `split_hosts`,
  `normalize_host`, `build_args`, `xml_escape`, `version_newer`, `dns_answer_ok`,
  `smart` sıralaması, tarama politikası.
- CI sanitizer kullanmıyor (CLAUDE.md'deki ASan/UBSan komutları yalnızca yerel), `-Werror`
  ve statik analiz yok.
- Filtre sözdizimi CI'da doğrulanmıyor. `WinDivertHelperCompileFilter` sürücü veya yönetici
  gerektirmeden kullanıcı modunda çalışır; motorun ürettiği filtre dizgileri CI'da
  derlenebilir.
- Tedarik zinciri: `softprops/action-gh-release@v2` ve `actions/*@v4` commit SHA'sına
  sabitlenmemiş. `permissions: contents: write` PR derlemeleri dahil tüm işlere verilmiş.
  MinGW sürümü apt'tan sabitlenmeden geliyor (tekrarlanabilir derleme yok). WinDivert
  zip'i SHA-256 ile sabitlenmiş (iyi).
- Yerel ortam: bu makinede derleyici yok. Kod aşamasında CI'a güvenilecek ya da
  llvm-mingw kurulacak.

## 12. Platform bağımlılıkları ve teknik borç

- Motor WinDivert'e, uygulama Win32/WinHTTP/iphlpapi/GDI+/Task Scheduler'a sıkı bağlı.
  Taşınabilir parçalar: `exclude.c` (`GetTickCount64` dışında), `dnsredir.c`'nin DNS
  ayrıştırma kısmı, `smart.c`'nin sıralama formülü, `probe.c`'nin DNS kodlama/çözme kısmı.
- GoodbyeDPI upstream kodu (`goodbyedpi.c` ana döngü, `fakepackets.c`, `ttltrack.c`,
  `blackwhitelist.c`, `service.c`) ile TurkishDPI eklemeleri aynı dosyada karışık;
  eklemeler yorumlarla ("TurkishDPI:") işaretli ama upstream'den yeni sürüm almak zor.
  Upstream hangi commit'ten çatallandı, repoda kayıtlı değil (`GOODBYEDPI_VERSION
  "v0.2.3rc3-turkishdpi"` dışında).
- Upstream'in `die()` makrosu yorumda "20 saniye" diyor, kodda 3 saniye (zararsız).

## 13. CLAUDE.md / README / kod çelişkileri

| # | Kaynak | İddia | Kodda gerçek |
|---|---|---|---|
| 1 | README "Nasıl çalışır 2" | DNS: "yanıt veren **ilkini** kullanır" | `pick_dns` 6 sunucuyu paralel ölçüp **en hızlısını** seçiyor (%20 histerezis) |
| 2 | README tablo | DNS "Yandex / AdGuard" | Listede OpenDNS (5353 ve 443) de var; ölçümde OpenDNS seçilmiş |
| 3 | README profil tablosu | `-5 --set-ttl 5` vb. | Kodda `-q` da var (`-9` zaten `-q` içeriyor); tablo eksik |
| 4 | README "Neden" tablosu | "Dosya boyutu ~250 KB" | rc2 exe 381 KB; yerel derleme 764 KB. CI GCC çıktısı bu oturumda ölçülmedi |
| 5 | README rozet | "Windows 7+" | Manifest Win7 GUID içeriyor, ama Win7 + WinDivert 2.2 imza desteği (SHA-2) bu oturumda doğrulanmadı; ana hedef Win10/11 |
| 6 | CLAUDE.md "Known limits" ve README "Sorun giderme" | VirusTotal: Elastic, **Arctic Wolf** | CLAUDE.md "Status" bölümü ve master prompt: Elastic, **Kaspersky, Lionic** (3/68). İki bölüm birbiriyle çelişiyor |
| 7 | README | "Kaldır ve temizle … WinDivert sürücüsünü siler" | Sürücü **servisini** durdurup siliyor; `.sys` dosyası exe'nin yanında kalıyor. Ayrıca `WinDivert` servisini koşulsuz siliyor, başka bir WinDivert aracı bozulabilir |
| 8 | CLAUDE.md | "QUIC is blocked (-q) in every preset" | Doğru; yalnızca QUIC **v1** Initial. QUIC v2 (RFC 9369) engellenmiyor (şu an pratikte nadir) |
| 9 | CLAUDE.md "Status" | "v1.1.0 (rc2 published as pre-release)" | Etiketler: v1.1.0-rc1..rc3 ve v1.1.0 mevcut; bölüm tarih sırası karışık, sadeleşmeli |
| 10 | CLAUDE.md "Elevated code must not trust user-writable paths … WinDivert.dll only from the exe's own folder" | Kural, exe klasörünün güvenilir olduğunu varsayıyor | Exe klasörü genelde kullanıcı yazılabilir; kural bu haliyle yeterli değil (S1) |
| 11 | README ayarlar | `[TurkishDPI]` anahtarları | `DnsHybrid` ve `CheckUpdates` README'de yok |

## 14. CLAUDE.md'ye eklenmesi önerilenler (kopyalamadan)

- Exe klasörü güven varsayımı ve kurulum konumu kuralı (S1 düzeltildiğinde).
- `WM_COMMAND` UIPI istisnasının kaldırıldığı ve neden kaldırıldığı (S3 düzeltildiğinde).
- Log rotasyonunun nerede yapıldığı.
- Bildirim politikası (hangi durum geçişleri balon üretir).
- VirusTotal satırının tek ve güncel hali (çelişki #6).
- "Bu makinede derleyici yok" türü geçici bilgiler CLAUDE.md'ye **konmamalı**.
