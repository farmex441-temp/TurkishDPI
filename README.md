<div align="center">
  <img src="assets/logo.png" width="140" alt="TurkishDPI logo"/>

# TurkishDPI

**Akıllı DPI kalkanı – tek tıkla, VPN'siz, hız kaybı olmadan.**
</div>

[![Build](https://github.com/farmex441-temp/TurkishDPI/actions/workflows/build.yml/badge.svg)](https://github.com/farmex441-temp/TurkishDPI/actions/workflows/build.yml)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%207%2B-0078D6?logo=windows)](#)

**TurkishDPI**, Türkiye'deki internet servis sağlayıcılarının Derin Paket İnceleme (DPI) ve DNS engellerini **VPN'siz**, **hız kaybı olmadan** ve **tamamen otomatik** olarak aşmanızı sağlayan küçük bir Windows uygulamasıdır.

[GoodbyeDPI](https://github.com/ValdikSS/GoodbyeDPI) ve [GoodbyeDPI-Turkey](https://github.com/cagritaskn/GoodbyeDPI-Turkey) motorunu temel alır; [Shroudly](https://github.com/umutxyp/Shroudly)'nin otomatik strateji bulma, bağlantı bekçisi ve SNI parçalama fikirlerini ekler.

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

### Ölçüm sonuçları (v1.1.0, Türk Telekom altyapılı bir ağ, `--selftest`)

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

### Profiller

| Profil | Motor argümanları |
|---|---|
| Türkiye Standart | `-5 -q --set-ttl 5` + DNS |
| Modern | `-9` + DNS (`-9` QUIC engelini içerir) |
| Auto-TTL | `-5 -q` + DNS |
| Sabit TTL 3 | `-q --set-ttl 3` + DNS |
| SNI Parçalama + Sahte SNI | `-9 --frag-by-sni --fake-with-sni www.google.com` + DNS |
| Agresif SNI | `-9 --frag-by-sni --fake-with-sni www.google.com --fake-resend 2 --auto-ttl` + DNS |
| Superonline (-5 / -9 / TTL 3 / SNI) | DNS yönlendirmesiz varyantlar |

Profillerin tamamı QUIC/HTTP3'ü (`-q`) engeller.

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
p0=12,11,84            ; deneme, başarı, ortalama gecikme (ms)
```

### Komut satırı (GoodbyeDPI uyumlu)

```bat
TurkishDPI.exe                       :: tepsi uygulaması (otomatik)
TurkishDPI.exe --tray                :: sessiz başlat
TurkishDPI.exe --engine -9 --dns-addr 77.88.8.8 --dns-port 1253
TurkishDPI.exe -5 --set-ttl 5 ...    :: goodbyedpi.exe ile birebir aynı argümanlar
```

Eski GoodbyeDPI `.cmd` betiklerinde `goodbyedpi.exe` yerine `TurkishDPI.exe` yazmanız yeterlidir; Windows hizmeti (`sc create`) olarak da çalışır.

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

- **Antivirüs:** TurkishDPI.exe sürücü taşımaz; yalnızca "Windows ile başlat" açıkken kendisini ve WinDivert dosyalarını Program Files'a kopyalar. Exe, yanındaki `WinDivert.dll` ve sürücü dosyalarının resmi sürümle aynı olduğunu (SHA-256) doğrulamadan onları yüklemez. Paketteki `WinDivert64.sys`, WinDivert'in resmi imzalı sürücüsüdür; bazı üreticiler (ör. Elastic, Kaspersky "not-a-virus", Lionic) **her** WinDivert sürücüsünü genel bir kuralla "WinDivert"/"RiskTool" olarak etiketler. Bu, GoodbyeDPI ve zapret'te de aynıdır ve imzalı sürücü değiştirilemez. Windows Defender'ın işaretlediği şey ise WinDivert'in resmi zip arşivindeki örnek araçlardır; TurkishDPI bunları dağıtmaz. Başka bir antivirüs engellerse uygulamadaki *Ayarlar → Antivirüs sorunu yardımı* adım adım yol gösterir; TurkishDPI hiçbir zaman antivirüs ayarlarınızı kendisi değiştirmez.
- **Kaspersky:** GoodbyeDPI'ı engeller; tamamen kaldırılması gerekir.
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

- [ValdikSS/GoodbyeDPI](https://github.com/ValdikSS/GoodbyeDPI) — paket motoru (Apache 2.0)
- [cagritaskn/GoodbyeDPI-Turkey](https://github.com/cagritaskn/GoodbyeDPI-Turkey) — Türkiye profilleri (Apache 2.0)
- [umutxyp/Shroudly](https://github.com/umutxyp/Shroudly) — otomatik tarama / bekçi / SNI fikirleri (MIT)
- [WinDivert](https://reqrypt.org/windivert.html) — Basil Hussain (LGPL v3)

Logo ve simgeler bu projeye özeldir (`tools/make_icons.py` ile üretilir).

> Bu yazılım yalnızca erişim hakkınız olan içeriklere ulaşmak içindir; kullanımından doğan sorumluluk kullanıcıya aittir.
