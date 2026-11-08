<div align="center">
  <img src="assets/logo.png" width="140" alt="TurkishDPI logo"/>

# TurkishDPI

**Akıllı DPI kalkanı – tek tıkla, VPN'siz, hız kaybı olmadan.**
</div>

[![Build](https://github.com/farmex441-temp/TurkishDPI/actions/workflows/build.yml/badge.svg)](https://github.com/farmex441-temp/TurkishDPI/actions/workflows/build.yml)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%207%2B-0078D6?logo=windows)](#)

**TurkishDPI**, Türkiye'deki internet servis sağlayıcılarının Derin Paket İnceleme (DPI) ve DNS engellerini **VPN'siz**, **hız kaybı olmadan** ve **tamamen otomatik** olarak aşmanızı sağlayan küçük bir Windows uygulamasıdır.

Sürüm 2.0'dan itibaren **kendi paket motorunu** kullanır: motorun IPv4 paket yolu ve yardımcıları açık kaynak [Shroudly](https://github.com/umutxyp/Shroudly) çekirdeğinden (MIT) alınmıştır; DNS yönlendirmesi, IPv6, ödeme koruması, otomatik profil seçimi ve arayüz TurkishDPI'ındır. (1.x sürümleri GoodbyeDPI tabanlı bir motor kullanıyordu; 2.0'da o koddan hiçbir şey kalmadı.)

## Öne çıkanlar

- **Kendi arayüzü:** Koyu temalı, tek pencerelik sade uygulama – büyük aç/kapa düğmesi, canlı durum, gecikme, test siteleri ve tepsi simgesi (yeşil: aktif, sarı: taranıyor, gri: duraklatıldı).
- **Akıllı motor:** Her ağı (modem/Wi-Fi) ayrı tanır ve o ağda hangi profilin ne kadar başarılı ve hızlı olduğunu **öğrenir**. Bilinen bir ağa bağlandığınızda doğru profili ilk denemede seçer; yeni bir ağda en hızlı çalışanı ölçerek bulur. ISS engeli değiştirirse eski bilgiyi yavaşça unutup yeniden öğrenir.
- **Site testi ve onarımı:** Açılmayan bir siteyi yazın; TurkishDPI önce engelin türünü ayırır (DNS engeli, SNI/TLS engeli, IP engeli ya da sunucu kapalı, alan adı yok). DPI ile aşılabilen bir engelse o siteyi açan profili kendisi bulur ve siteyi izleme listesine ekler; aşılamıyorsa bunu açıkça söyler.
- **Ödeme koruması:** 100'den fazla banka, kart, 3D Secure ve ödeme sistemi alan adının (Garanti, İş Bankası, Akbank, Ziraat, Papara, iyzico, PayTR, PayPal, Stripe, Visa, Mastercard…) trafiğine **hiç dokunulmaz**. Böylece ödeme ve 3D Secure adımları etkilenmez. İsterseniz `--exclude dosya.txt` ile kendi listenizi ekleyebilirsiniz.
- **Tüm siteler:** Motor, yalnızca belirli siteleri değil 80/443 portundaki tüm HTTP/HTTPS bağlantılarını işler. Tüm profillerde QUIC/HTTP3 engellenir, böylece YouTube/Google gibi QUIC kullanan siteler de TCP'ye düşüp atlatmadan geçer.
- **Sahte DNS filtresi:** Engel sayfasına, sinkhole'a veya özel ağ adreslerine yönlendiren sahte DNS cevapları veren sunucular otomatik elenir; yalnızca gerçek cevap veren DNS kullanılır.
- **Canlı istatistik:** İşlenen paket, HTTPS/HTTP/DNS işlemleri ve atlanan ödeme bağlantıları pencerede anlık görünür.
- **Otopilot kontrolleri (Shroudly'den):** Aynı anda çalışan başka bir DPI/VPN aracı (GoodbyeDPI, zapret, ByeDPI, Shroudly, WARP, v2rayN…) varsa uyarır; Windows proxy ayarı kapanmış bir programı gösteriyorsa (eski ByeDPI/proxy araçlarından kalan) tek tıkla onarır.
- **Güncelleme bildirimi:** Yeni sürüm çıktığında pencerede bildirir.
- **Temiz kaldırma:** *Ayarlar → Kaldır ve temizle* başlangıç görevini, Program Files kopyasını, ayarları, günlüğü ve WinDivert sürücü hizmetini siler.
- **Açıklanabilir kararlar:** Pencerede profil adının yanında neden seçildiği yazar (açılış, ağ değişimi, bekçi, uykudan dönüş…). Tek seferlik bir aksaklık yeniden tarama başlatmaz; aynı profil devam ediyorsa bağlantılar kesilmez. Bildirim yalnızca durum gerçekten değişince gelir.
- **Kendi kendini test:** `TurkishDPI.exe --selftest` tarar, ödeme sitelerinin dokunulmadan geçtiğini doğrular, sonucu günlüğe yazıp kapanır.

> **Not:** DPI atlatma; SNI/Host incelemesi ve DNS ile yapılan engelleri aşar. Bir sitenin **IP adresi** engellenmişse veya site Türkiye'ye kendisi hizmet vermiyorsa hiçbir DPI aracı (VPN olmadan) bunu aşamaz – TurkishDPI bu durumu site testinde size söyler.

### Uyumluluk

- **Windows 7 SP1, 8.1, 10, 11** – 64 bit (`x64` zip) ve 32 bit (`x86` zip). Windows 7'de WinDivert sürücüsünün imzası için KB4474419 (SHA-2), bağlantı testleri için KB3140245 (TLS 1.2) güncellemeleri gerekir; KB2533623 olmadan da çalışır.
- **ARM işlemcili Windows desteklenmez:** WinDivert'in ARM64 sürücüsü yoktur. Program bu durumda belirsiz bir sürücü hatası yerine bunu açıkça söyler.
- **Ekran okuyucu:** Tüm düğmeler, durum bilgisi, Ayrıntılar satırları, test siteleri ve site kutusu Ekran Okuyucusu/NVDA tarafından okunur (UI Automation ile doğrulandı); her şey Tab, Boşluk ve Enter ile kullanılabilir.
- **Discord:** Web sitesi, uygulama, ağ geçidi, CDN ve **güncelleyici** (`updates.discord.com`) engelli; TurkishDPI açıkken hepsi açılıyor (ölçüldü). Sesli görüşme sunucuları (`*.discord.media`) Türkiye'de şu an engelli değil, ses de çalışıyor.
- **Windows hizmeti:** TurkishDPI hizmet olarak değil, oturum açılınca Görev Zamanlayıcı ile başlar (tepside simgesiyle).
- **Linux/macOS:** Desteklenmez (WinDivert yalnızca Windows'ta vardır). Linux için [zapret](https://github.com/bol-van/zapret) kullanılabilir.

### Ölçüm sonuçları (v1.x motoru, Türk Telekom altyapılı bir ağ, `--selftest`)

| | Sonuç |
|---|---|
| Engelli test siteleri (discord, roblox, wattpad) | 3/3 açıldı |
| Bağlantı kurulum süresi (TCP + TLS) | ~150 ms |
| Seçilen temiz DNS | OpenDNS 42 ms (Yandex 96 ms idi) |
| TurkishDPI'ın eklediği gecikme | ~9 ms (motor açık 247 ms, kapalı 238 ms) |
| Ödeme siteleri (PayPal, Garanti, iyzico) | Açıldı, bağlantılara dokunulmadı |

## Neden TurkishDPI?

| | GoodbyeDPI-Turkey | Shroudly (masaüstü) | **TurkishDPI** |
|---|---|---|---|
| Kurulum | Doğru `.cmd` dosyasını deneme-yanılma ile bulmak | Electron uygulaması | **Tek `.exe`, çift tıkla** |
| Ayar seçimi | Elle (7 farklı betik) | Otomatik | **Otomatik** – 10 profili gerçek bağlantı testiyle dener |
| DNS | Sabit (Yandex:1253) | DoH | **Otomatik** – en hızlı temiz DNS'i ölçer (Yandex / AdGuard / OpenDNS), hibrit mod |
| Bozulunca | Elle yeniden deneme | Otomatik | **Otomatik** – ağ değişince, uyandıktan sonra ve her 10 dk'da kontrol |
| Öğrenme | Yok | Yok | **Ağ başına başarı/gecikme istatistiği** |
| Ödeme sistemleri | Etkilenebilir | Etkilenebilir | **Banka/ödeme alan adlarına dokunulmaz** |
| Bellek | ~2 MB | ~150–300 MB | **~4–5 MB** (pencere kapalıyken), saf C |
| Dosya boyutu | ~100 KB + betikler | ~100 MB | **< 1 MB** (+ WinDivert) |

## İndirme

[**Releases**](https://github.com/farmex441-temp/TurkishDPI/releases/latest) sayfasından:

- **`TurkishDPI-vX-x64.zip`** — 64-bit Windows (önerilen). Zip'i bir klasöre çıkarıp `TurkishDPI.exe`'yi çalıştırın; `WinDivert.dll` ve `WinDivert64.sys` exe ile aynı klasörde kalmalı.
- `TurkishDPI-vX-x86.zip` — 32-bit Windows.

> Exe'nin içine sürücü gömülmez ve program diske hiçbir sürücü bırakmaz: v1.0'daki tek dosyalık sürüm, içindeki sürücü yüzünden bazı antivirüslerce işaretleniyordu.

## Kullanım

1. `TurkishDPI.exe`'yi çalıştırın, yönetici iznini onaylayın.
2. Saatin yanındaki simgeye yerleşir ve çalışan profili kendisi bulur (genelde 5–20 saniye).
3. Pencereyi kapatınca uygulama saatin yanındaki simgede çalışmaya devam eder. Simgeye tıklayınca pencere açılır, sağ tıklayınca hızlı menü gelir.
4. **Ayarlar** menüsü: Otomatik mod, Windows ile başlat, Ödeme ve banka koruması, Günlük, Gelişmiş ayarlar.

"Windows ile başlat" seçeneği, TurkishDPI'ı (exe, WinDivert dosyaları ve lisanslar) korumalı `C:\Program Files\TurkishDPI` klasörüne kopyalar ve Görev Zamanlayıcı'ya **o kopyayı** çalıştıran yönetici yetkili bir oturum açma görevi ekler; her açılışta UAC sorusu çıkmaz. Görev hiçbir zaman zip'i çıkardığınız klasörü (ör. İndirilenler) çalıştırmaz: o klasöre başka programlar da yazabildiği için bu, onlara UAC'sız yönetici yetkisi verirdi. Yeni bir sürümü çalıştırdığınızda korumalı kopya kendiliğinden güncellenir.

### Nasıl çalışır?

1. **İnternet kontrolü** – Engelsiz sitelere (Google, Microsoft) erişim yoksa bekler, profil denemesi yapmaz.
2. **DNS seçimi** – Standart dışı portlardaki temiz DNS sunucularının (Yandex :1253, AdGuard :5353, OpenDNS :5353/:443) hepsini paralel ölçer ve sahte cevap vermeyen en hızlısını seçer. Hibrit modda her sorgu hem ağınızın DNS'ine hem bu temiz sunucuya gider; ilk güvenilir cevap kullanılır. (Gizlilik notu: bu modda DNS sorgularınız seçilen temiz DNS sağlayıcısına da gider; istemezseniz ini'de `DnsHybrid=0`.)
3. **Profil taraması** – Profilleri bu ağda öğrenilmiş başarı oranına ve hıza göre sıralar, motoru başlatır ve engelli test sitelerine (varsayılan: `discord.com`, `www.roblox.com`, `www.wattpad.com`) **gerçek TLS bağlantısı** kurar. Yeni bir ağda ilk çalışan profilden sonra iki aday daha ölçülür ve en hızlısı seçilir.
4. **Bekçi** – Ağ değiştiğinde, uykudan uyanınca ve belirli aralıklarla test eder. Bir test başarısız olursa 20 saniye sonra tekrar dener; ancak ikinci kez de başarısız olursa yeniden tarar. Motor çökerse yeniden başlatır.

### Motor nasıl atlatır?

Bir web bağlantısının ilk paketinde (TLS ClientHello ya da HTTP isteği) sitenin adı açıkça yazar; ISS'nin filtresi bu adı okuyup bağlantıyı keser. Motor bu paketi yakalar ve:

- önce **sahte bir kopyasını** gönderir: filtreye ulaşır ama sunucuya varmadan ölür (ağdaki uzaklık ölçülerek ayarlanan düşük TTL, bozuk TCP sağlama toplamı, adı değiştirilmiş);
- gerçek paketi, hiçbir parçada sitenin adı tam görünmeyecek şekilde **böler** (gerekirse ters sırada gönderir);
- filtrenin bağlantıyı kesmek için uydurduğu **RST paketlerini** düşürür;
- QUIC/HTTP3'ü engeller ki tarayıcı bu tekniklerin işlediği TCP'ye geçsin;
- DNS sorgularını temiz bir DNS'e de gönderir (hibrit yarış).

Banka ve ödeme bağlantılarına hiç dokunulmaz.

### Profiller

| Profil | Motor argümanları |
|---|---|
| Sahte + ters bölme (önerilen) | `--desync fakeddisorder --autottl 1` + DNS |
| Sahte paket + bölme | `--desync fake --split on --autottl 1` + DNS |
| TLS kaydı bölme (Superonline) | `--desync fake --split on --tlsrec 1 --autottl 1` + DNS |
| Sahte paket + ters sıralı bölme | `--desync fake --split disorder --autottl 1` + DNS |
| Sahte + bölünmüş | `--desync fakedsplit --autottl 1` + DNS |
| Sahte + ters bölme, sabit TTL 4 | `--desync fakeddisorder --autottl 0 --fake-ttl 4` + DNS |
| Yalnız bölme | `--desync none --split on` + DNS |
| … DNS yönlendirmesiz | aynı tekniklerin DNS'e dokunmayan varyantları |

Hepsi `--drop-rst 1` ile sahte RST'leri düşürür ve QUIC'i engeller. Türk Telekom hattında ölçüldü: sahte paket filtreyi geçecek kadar uzağa gitmeli (ölçülen TTL; sabit 3-5 yetmiyor) ve gerçek veri sahtenin ardından sırayla gitmemeli; aksi hâlde filtre gerçek ilk parçayı düşürür ve Windows onu 300 ms sonra yeniden gönderir. Bu yüzden ters sıralı profil ilk sıradadır.

### Ayarlar

`C:\ProgramData\TurkishDPI\TurkishDPI.ini`:

```ini
[TurkishDPI]
Auto=1                 ; 1 = otomatik mod
Preset=0               ; son çalışan profil
Dns=0                  ; son çalışan DNS
PaymentProtection=1    ; banka/ödeme sitelerine dokunma
DnsHybrid=1            ; DNS sorgularını hem ağın DNS'ine hem temiz DNS'e gönder
WatchdogMinutes=10     ; bekçi aralığı (1-240 dk)
CheckUpdates=1         ; günde bir kez GitHub'da yeni sürüm kontrolü
Targets=discord.com,www.roblox.com,www.wattpad.com   ; engelli test siteleri
Control=www.google.com,www.microsoft.com             ; engelsiz kontrol siteleri

[net_<ağ-kimliği>]     ; akıllı motorun her ağ için öğrendikleri
e0=12,11,84            ; profil 0: deneme, başarı, ortalama gecikme (ms)
```

### Komut satırı

```bat
TurkishDPI.exe                       :: tepsi uygulaması (otomatik)
TurkishDPI.exe --tray                :: sessiz başlat
TurkishDPI.exe --selftest            :: tara, ödeme korumasını doğrula, günlüğe yaz, çık
TurkishDPI.exe --engine --help       :: yalnızca motor ve seçenekleri
TurkishDPI.exe --engine --desync fakeddisorder --autottl 1 --dns-addr 208.67.222.222 --dns-port 5353 --dns-hybrid
```

## Gizlilik ve güvenlik

**Bilgisayarınızdan neler çıkar:**
- **DNS:** Hibrit DNS açıkken (varsayılan) her DNS sorgusu hem ağınızın DNS'ine hem de seçilen temiz DNS'e (Yandex, AdGuard veya OpenDNS; en hızlısı) gider. Bu sorgular şifresizdir. İsterseniz `DnsHybrid=0` ile yalnızca temiz DNS kullanılır.
- **Bağlantı testleri:** Profil seçimi ve bekçi için test sitelerine (varsayılan discord.com, roblox.com, wattpad.com) ve kontrol sitelerine (google.com, microsoft.com) yaklaşık 10 dakikada bir HTTPS bağlantısı kurulur; içerik indirilmez.
- **Güncelleme kontrolü:** Günde bir kez GitHub API'sine sürüm sorusu gider (`CheckUpdates=0` ile kapatılır).
- Bunların dışında hiçbir veri gönderilmez: telemetri, hesap veya analiz yoktur.

**Yerelde ne saklanır:** `C:\ProgramData\TurkishDPI` yalnızca Yöneticiler ve SYSTEM tarafından okunabilir. Ayarlar, öğrenilen ağ bilgisi (ağ geçidi kimliği ve bağdaştırıcı adı) ve günlük burada durur. Günlük, ziyaret ettiğiniz siteleri kaydetmez.

**Yönetici yetkisiyle çalışan bir program olarak alınan önlemler:**
- WinDivert DLL'i ve sürücüleri derleme anında sabitlenen SHA-256 ile doğrulanır; başka bir dosya yüklenmez veya kopyalanmaz.
- Sistem kitaplıkları (winhttp, iphlpapi, dnsapi, bcrypt, gdiplus) yalnızca System32'den yüklenir; exe'nin klasörüne bırakılan bir DLL yüklenmez.
- "Windows ile başlat" yalnızca yöneticiye ait `C:\Program Files\TurkishDPI` kopyasını çalıştırır; kopyalanamazsa görev hiç oluşturulmaz (ya da eskisi silinir).
- Veri klasörü yöneticiye aittir; başkasına ait dosya veya bağlantı (junction/symlink) bulunursa kaldırılır, kaldırılamazsa program başlamaz.
- Bağlantılar ve günlük, kullanıcının dosya ilişkilendirmeleri kullanılmadan doğrudan `explorer.exe`/`notepad.exe` ile açılır.

## Sorun giderme

- **Antivirüs:** TurkishDPI.exe sürücü taşımaz; yalnızca "Windows ile başlat" açıkken kendisini ve WinDivert dosyalarını Program Files'a kopyalar. Exe, yanındaki `WinDivert.dll` ve sürücü dosyalarının resmi sürümle aynı olduğunu (SHA-256) doğrulamadan onları yüklemez. Paketteki `WinDivert64.sys`, WinDivert'in resmi imzalı sürücüsüdür; bazı üreticiler (ör. Elastic, Kaspersky "not-a-virus", Lionic) **her** WinDivert sürücüsünü genel bir kuralla "WinDivert"/"RiskTool" olarak etiketler. Bu, WinDivert kullanan her araçta (zapret, Shroudly'nin gelişmiş modu…) aynıdır ve imzalı sürücü değiştirilemez. Windows Defender'ın işaretlediği şey ise WinDivert'in resmi zip arşivindeki örnek araçlardır; TurkishDPI bunları dağıtmaz. Başka bir antivirüs engellerse uygulamadaki *Ayarlar → Antivirüs sorunu yardımı* adım adım yol gösterir; TurkishDPI hiçbir zaman antivirüs ayarlarınızı kendisi değiştirmez.
- **Kaspersky:** WinDivert sürücüsünü engelleyebilir (hata 1275); gerekirse kaldırılması gerekir.
- **Eski GoodbyeDPI / zapret hizmeti:** TurkishDPI açılışta tespit eder ve kaldırmayı önerir.
- Ayrıntılı kayıt: menüden **Günlüğü aç**.

## Derleme

```bash
# MinGW-w64 (Linux/MSYS2) veya llvm-mingw
make ARCH=x86_64      # 64-bit
make ARCH=i686        # 32-bit
# WinDivert.dll ve WinDivert64.sys exe ile aynı klasöre konur
```

GitHub Actions her `v*` etiketi için x64/x86 sürümlerini derleyip Release'e yükler.

## Lisans ve teşekkür

Apache License 2.0 — ayrıntılar için [LICENSE](LICENSE) ve [NOTICE](NOTICE).

- [umutxyp/Shroudly](https://github.com/umutxyp/Shroudly) — motor çekirdeği: `src/engine/core/` ve `engine.c`'nin IPv4 paket yolu (MIT, metni [THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt))
- 1.x sürümleri [ValdikSS/GoodbyeDPI](https://github.com/ValdikSS/GoodbyeDPI) tabanlı bir motor kullanıyordu (Apache 2.0); 2.0'da o kod kaldırıldı.
- [WinDivert](https://reqrypt.org/windivert.html) — Basil Hussain (LGPL v3)

Logo ve simgeler bu projeye özeldir (`tools/make_icons.py` ile üretilir).

> Bu yazılım yalnızca erişim hakkınız olan içeriklere ulaşmak içindir; kullanımından doğan sorumluluk kullanıcıya aittir.
