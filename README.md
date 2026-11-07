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
- **Site testi ve onarımı:** Açılmayan bir siteyi yazın; TurkishDPI önce test eder, engelliyse o siteyi açan profili kendisi bulur ve siteyi izleme listesine ekler.
- **Ödeme koruması:** 100'den fazla banka, kart, 3D Secure ve ödeme sistemi alan adının (Garanti, İş Bankası, Akbank, Ziraat, Papara, iyzico, PayTR, PayPal, Stripe, Visa, Mastercard…) trafiğine **hiç dokunulmaz**. Böylece ödeme ve 3D Secure adımları etkilenmez. İsterseniz `--exclude dosya.txt` ile kendi listenizi ekleyebilirsiniz.
- **Tüm siteler:** Motor, yalnızca belirli siteleri değil 80/443 portundaki tüm HTTP/HTTPS bağlantılarını işler. Tüm profillerde QUIC/HTTP3 engellenir, böylece YouTube/Google gibi QUIC kullanan siteler de TCP'ye düşüp atlatmadan geçer.
- **Sahte DNS filtresi:** Engel sayfasına, sinkhole'a veya özel ağ adreslerine yönlendiren sahte DNS cevapları veren sunucular otomatik elenir; yalnızca gerçek cevap veren DNS kullanılır.
- **Canlı istatistik:** İşlenen paket, HTTPS/HTTP/DNS işlemleri ve atlanan ödeme bağlantıları pencerede anlık görünür.
- **Otopilot kontrolleri (Shroudly'den):** Aynı anda çalışan başka bir DPI/VPN aracı (GoodbyeDPI, zapret, ByeDPI, Shroudly, WARP, v2rayN…) varsa uyarır; Windows proxy ayarı kapanmış bir programı gösteriyorsa (eski ByeDPI/proxy araçlarından kalan) tek tıkla onarır.
- **Güncelleme bildirimi:** Yeni sürüm çıktığında pencerede bildirir.
- **Temiz kaldırma:** *Ayarlar → Kaldır ve temizle* başlangıç görevini, ayarları, günlüğü ve WinDivert sürücüsünü siler.
- **Kendi kendini test:** `TurkishDPI.exe --selftest` tarar, ödeme sitelerinin dokunulmadan geçtiğini doğrular, sonucu günlüğe yazıp kapanır.

> **Not:** DPI atlatma; SNI/Host incelemesi ve DNS ile yapılan engelleri aşar. Bir sitenin **IP adresi** engellenmişse veya site Türkiye'ye kendisi hizmet vermiyorsa hiçbir DPI aracı (VPN olmadan) bunu aşamaz – TurkishDPI bu durumu site testinde size söyler.

## Neden TurkishDPI?

| | GoodbyeDPI-Turkey | Shroudly (masaüstü) | **TurkishDPI** |
|---|---|---|---|
| Kurulum | Doğru `.cmd` dosyasını deneme-yanılma ile bulmak | Electron uygulaması | **Tek `.exe`, çift tıkla** |
| Ayar seçimi | Elle (7 farklı betik) | Otomatik | **Otomatik** – 10 profili gerçek bağlantı testiyle dener |
| DNS | Sabit (Yandex:1253) | DoH | **Otomatik** – erişilebilir DNS'i bulur (Yandex / AdGuard) |
| Bozulunca | Elle yeniden deneme | Otomatik | **Otomatik** – ağ değişince, uyandıktan sonra ve her 10 dk'da kontrol |
| Öğrenme | Yok | Yok | **Ağ başına başarı/gecikme istatistiği** |
| Ödeme sistemleri | Etkilenebilir | Etkilenebilir | **Banka/ödeme alan adlarına dokunulmaz** |
| Bellek | ~2 MB | ~150–300 MB | **~4–5 MB** (pencere kapalıyken), saf C |
| Dosya boyutu | ~100 KB + betikler | ~100 MB | **~250 KB** (+ WinDivert) |

## İndirme

[**Releases**](https://github.com/farmex441-temp/TurkishDPI/releases/latest) sayfasından:

- **`TurkishDPI.exe`** — tek dosyalık 64-bit sürüm (WinDivert içine gömülü). Önerilen.
- `TurkishDPI-x86.exe` — 32-bit Windows için tek dosya.
- `TurkishDPI-vX-x64.zip` / `-x86.zip` — klasik paket (exe + WinDivert dosyaları yan yana).

## Kullanım

1. `TurkishDPI.exe`'yi çalıştırın, yönetici iznini onaylayın.
2. Saatin yanındaki simgeye yerleşir ve çalışan profili kendisi bulur (genelde 5–20 saniye).
3. Pencereyi kapatınca uygulama saatin yanındaki simgede çalışmaya devam eder. Simgeye tıklayınca pencere açılır, sağ tıklayınca hızlı menü gelir.
4. **Ayarlar** menüsü: Otomatik mod, Windows ile başlat, Ödeme ve banka koruması, Günlük, Gelişmiş ayarlar.

"Windows ile başlat" seçeneği, Görev Zamanlayıcı'ya yönetici yetkili bir oturum açma görevi ekler; her açılışta UAC sorusu çıkmaz.

### Nasıl çalışır?

1. **İnternet kontrolü** – Engelsiz sitelere (Google, Microsoft) erişim yoksa bekler, profil denemesi yapmaz.
2. **DNS seçimi** – Standart dışı portlardaki DNS sunucularına (Yandex 77.88.8.8:1253, 77.88.8.1:1253, AdGuard 94.140.14.140:5353) sorgu atar ve yanıt veren ilkini kullanır.
3. **Profil taraması** – Profilleri bu ağda öğrenilmiş başarı oranına ve hıza göre sıralar, motoru başlatır ve engelli test sitelerine (varsayılan: `discord.com`, `www.roblox.com`, `www.wattpad.com`) **gerçek TLS bağlantısı** kurar. Yeni bir ağda ilk çalışan profilden sonra iki aday daha ölçülür ve en hızlısı seçilir.
4. **Bekçi** – Ağ değiştiğinde, uykudan uyanınca ve belirli aralıklarla test eder; çalışmıyorsa yeniden tarar. Motor çökerse yeniden başlatır.

### Profiller

| Profil | Motor argümanları |
|---|---|
| Türkiye Standart | `-5 --set-ttl 5` + DNS |
| Modern | `-9` + DNS |
| Auto-TTL | `-5` + DNS |
| Sabit TTL 3 | `--set-ttl 3` + DNS |
| SNI Parçalama + Sahte SNI | `-9 --frag-by-sni --fake-with-sni www.google.com` + DNS |
| Agresif SNI | `-9 --frag-by-sni --fake-with-sni www.google.com --fake-resend 2 --auto-ttl` + DNS |
| Superonline (-5 / -9 / TTL 3 / SNI) | DNS yönlendirmesiz varyantlar |

### Ayarlar

`C:\ProgramData\TurkishDPI\TurkishDPI.ini`:

```ini
[TurkishDPI]
Auto=1                 ; 1 = otomatik mod
Preset=0               ; son çalışan profil
Dns=0                  ; son çalışan DNS
PaymentProtection=1    ; banka/ödeme sitelerine dokunma
WatchdogMinutes=10     ; bekçi aralığı
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

## Sorun giderme

- **Antivirüs:** Windows Defender'ın işaretlediği şey WinDivert'in resmi zip arşivindeki örnek araçlardır; TurkishDPI bu arşivi dağıtmaz, yalnızca imzalı `WinDivert.dll` ve sürücüyü içerir (Defender taramasında temiz). Başka bir antivirüs engellerse uygulamadaki *Ayarlar → Antivirüs sorunu yardımı* adım adım yol gösterir; TurkishDPI hiçbir zaman antivirüs ayarlarınızı kendisi değiştirmez.
- **Kaspersky:** GoodbyeDPI'ı engeller; tamamen kaldırılması gerekir.
- **Eski GoodbyeDPI / zapret hizmeti:** TurkishDPI açılışta tespit eder ve kaldırmayı önerir.
- Ayrıntılı kayıt: menüden **Günlüğü aç**.

## Derleme

```bash
# MinGW-w64 (Linux/MSYS2) veya llvm-mingw
make ARCH=x86_64 WINDIVERT=WinDivert-2.2.2-A           # exe + yanına WinDivert dosyaları
make ARCH=x86_64 WINDIVERT=WinDivert-2.2.2-A EMBED=1   # tek dosya
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
