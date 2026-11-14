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

- **Kendi arayüzü:** Koyu temalı, tek pencerelik sade uygulama – büyük aç/kapa düğmesi, canlı durum, gecikme, test siteleri ve tepsi simgesi (yeşil: aktif, mavi: taranıyor, gri: duraklatıldı / beklemede / bağlantı yok, turuncu "!": kısmen çalışıyor, kırmızı "x": hata).
- **Akıllı motor:** Her ağı (modem/Wi-Fi) ayrı tanır ve o ağda hangi profilin ne kadar başarılı ve hızlı olduğunu **öğrenir**. Bilinen bir ağa bağlandığınızda doğru profili ilk denemede seçer; yeni bir ağda en hızlı çalışanı ölçerek bulur. ISS engeli değiştirirse eski bilgiyi yavaşça unutup yeniden öğrenir.
- **Site testi ve onarımı:** Açılmayan bir siteyi yazın; TurkishDPI önce engelin türünü ayırır (DNS engeli, SNI/TLS engeli, IP engeli ya da sunucu kapalı, alan adı yok). DPI ile aşılabilen bir engelse o siteyi açan profili kendisi bulur ve siteyi izleme listesine ekler; aşılamıyorsa bunu açıkça söyler.
- **Ödeme koruması:** 100'den fazla banka, kart, 3D Secure ve ödeme sistemi alan adının (Garanti, İş Bankası, Akbank, Ziraat, Papara, iyzico, PayTR, PayPal, Stripe, Visa, Mastercard…) trafiğine **hiç dokunulmaz**. Böylece ödeme ve 3D Secure adımları etkilenmez. Karar, bağlantıda görünen sunucu adına göre verilir; DNS cevaplarından öğrenilen bir adres, yalnızca pakette sunucu adı yoksa ya da ad bilinen bir ECH genel adıysa (`cloudflare-ech.com`) belirleyicidir. Böylece ödeme sitesiyle aynı CDN adresini paylaşan engelli bir site (eskiden 2 saate kadar) dokunulmadan kalmaz. Motoru elle çalıştırırsanız (`TurkishDPI.exe --engine --exclude dosya.txt`; dosyada her satırda bir alan adı, `#` ile başlayan kısım yorumdur; satır sonu LF, CRLF ya da CR, başta UTF-8 BOM olabilir; NUL içeren ya da çok uzun satırlar atlanır) kendi listenizi de ekleyebilirsiniz; liste yerleşik listeden sonra yüklenir; uygulamanın kendisi bu seçeneği kullanmaz.
- **Tüm siteler:** Motor, yalnızca belirli siteleri değil 80/443 portundaki tüm HTTP/HTTPS bağlantılarını işler. Tüm profillerde QUIC/HTTP3 engellenir, böylece YouTube/Google gibi QUIC kullanan siteler de TCP'ye düşüp atlatmadan geçer.
- **Sahte DNS filtresi:** Engel sayfasına, sinkhole'a veya geçersiz adreslere (`0.0.0.0`, loopback, çok noktaya yayın) yönlendiren sahte DNS cevapları ve ISS'nin döndürdüğü hata cevapları (NXDOMAIN, SERVFAIL, REFUSED) elenir; ilk güvenilir cevap kullanılır. Soru, kendisi yerel olan bir DNS'e (modem, VPN, bu bilgisayar) gittiyse o sunucunun verdiği özel ağ adresleri (`10.x`, `172.16–31.x`, `192.168.x`, `169.254.x`) kabul edilir; böylece modem sayfası, NAS, şirket içi adlar çalışır. Operatörün CGNAT aralığındaki (`100.64.0.0/10`) bir DNS bu istisnadan yararlanmaz. Sınırlama: ev modemi ISS'nin cevaplarını olduğu gibi ilettiğinden, bir ISS özel bir adresle sahte cevap verirse ve o adres engel listesinde (yerleşik, `--dns-block-ip` ya da ağda öğrenilen) değilse geçer; Türkiye'deki engel sayfaları genel adreslerdir.
- **Canlı istatistik:** İşlenen paket, HTTPS/HTTP/DNS işlemleri ve atlanan ödeme bağlantıları pencerede anlık görünür.
- **Otopilot kontrolleri (Shroudly'den):** Aynı anda çalışan başka bir DPI/VPN aracı (GoodbyeDPI, zapret, ByeDPI, Shroudly, WARP, v2rayN…) varsa uyarır; Windows proxy ayarı kapanmış bir programı gösteriyorsa (eski ByeDPI/proxy araçlarından kalan) tek tıkla onarır.
- **Tek tıkla güncelleme (2.0.1+):** Yeni sürüm çıktığında pencerede "Yeni sürüm hazır" yazar. **Güncelle →** düğmesine basınca TurkishDPI yeni sürümü GitHub'dan indirir, sürümle birlikte yayımlanan `SHA256SUMS.txt` ile karşılaştırır, kendini bulunduğu klasörde değiştirir ve yeni sürümle yeniden açılır. İndirilen exe'nin sürümü etiketle aynı ve çalışandan yeni olmalıdır. Ayarlar ve öğrenilen bilgiler korunur. Güncelleme yapılamazsa bkz. [Güncelleme çalışmazsa](#güncelleme-çalışmazsa).
- **Temiz kaldırma:** *Ayarlar → Kaldır ve temizle…* başlangıç görevini, Program Files kopyasını, ayarları, günlüğü ve WinDivert sürücü hizmetini siler (indirdiğiniz `TurkishDPI.exe` ve zip'ten çıkan dosyalar kalır, onları kendiniz silebilirsiniz). Program açılmıyorsa aynısını elle yapmak için bkz. [Elle kaldırma](#elle-kaldırma).
- **Açıklanabilir kararlar:** Pencerede profil adının yanında neden seçildiği yazar (açılış, ağ değişimi, bekçi, uykudan dönüş…). Tek seferlik bir aksaklık yeniden tarama başlatmaz; aynı profil devam ediyorsa bağlantılar kesilmez. Bildirim yalnızca durum gerçekten değişince gelir.
- **Kendi kendine test:** `TurkishDPI.exe --selftest` tarar, ödeme sitelerinin dokunulmadan geçtiğini doğrular, sonucu günlüğe yazıp kapanır.

> **Not:** DPI atlatma; SNI/Host incelemesi ve DNS ile yapılan engelleri aşar. Bir sitenin **IP adresi** engellenmişse veya site Türkiye'ye kendisi hizmet vermiyorsa hiçbir DPI aracı (VPN olmadan) bunu aşamaz – TurkishDPI bu durumu site testinde size söyler.

### Uyumluluk

- **Windows 7 SP1, 8.1, 10, 11** – 64 bit (`x64` zip) ve 32 bit (`x86` zip). Windows 7'de WinDivert sürücüsünün imzası için KB4474419 (SHA-2), bağlantı testleri için KB3140245 (TLS 1.2) güncellemeleri gerekir; KB2533623 olmadan da çalışır.
- **ARM işlemcili Windows desteklenmez:** WinDivert'in ARM64 sürücüsü yoktur. Program bu durumda belirsiz bir sürücü hatası yerine bunu açıkça söyler.
- **Ekran okuyucu:** Tüm düğmeler, durum bilgisi, Ayrıntılar satırları, test siteleri ve site kutusu Ekran Okuyucusu/NVDA tarafından okunur (UI Automation ile doğrulandı); her şey Tab, Boşluk ve Enter ile kullanılabilir.
- **Discord:** Web sitesi, uygulama, ağ geçidi, CDN ve **güncelleyici** (`updates.discord.com`) engelli; TurkishDPI açıkken hepsi açılıyor (ölçüldü). Sesli görüşme sunucuları (`*.discord.media`) Türkiye'de şu an engelli değil, ses de çalışıyor.
- **Windows hizmeti:** TurkishDPI hizmet olarak değil, oturum açılınca Görev Zamanlayıcı ile başlar (tepside simgesiyle).
- **Linux/macOS:** Desteklenmez (WinDivert yalnızca Windows'ta vardır). Linux için [zapret](https://github.com/bol-van/zapret) kullanılabilir.

## Neden TurkishDPI?

| | GoodbyeDPI-Turkey | Shroudly (masaüstü) | **TurkishDPI** |
|---|---|---|---|
| Arayüz | — | Electron masaüstü uygulaması | **Kendi küçük Win32 arayüzü** |
| Ayar seçimi | — | — | **Otomatik** – 10 profili gerçek bağlantı testiyle dener |
| DNS | Sabit alternatif sunucu (Türkiye sürümü: Yandex 77.88.8.8:1253) | DoH (birçok yol) | **Otomatik** – en hızlı temiz DNS'i ölçer (Yandex / AdGuard / OpenDNS), hibrit mod |
| Öğrenme | Yok | Site başına (proxy motorunda) | **Ağ başına başarı/gecikme istatistiği** |
| Ödeme sistemleri | Muaf tutma yok (isteğe bağlı alan adı listesi yalnızca atlatmayı o alan adlarına sınırlar) | — | **Banka/ödeme alan adlarına dokunulmaz** |
| IPv6 | — | Windows paket arka ucu IPv6'ya dokunmaz | **IPv6 için de bölme ve sahte paket** (uzantı başlıkları olmadan) |
| Windows hizmeti | Hizmet olarak çalışabilir | — | Hizmet değil; oturum açılışında görev olarak başlar |
| Bozulunca | — | — | **Otomatik** – ağ değişince, uyandıktan sonra ve her 10 dk'da kontrol |
| Bellek | ölçülmedi | ölçülmedi | **Boşta:** uygulama ~16 MB bellek (7 MB'ı özel), motor ~3 MB (2 MB'ı özel); v2.0.1, 14 Kasım 2026 ölçümü |
| Boyut | ölçülmedi | ölçülmedi | **Exe yaklaşık 2,2 MB** (v2.0.1: 2,16 MB; 2.0.2 derlemesinde hata ayıklama bilgisi atıldığı için daha küçüktür), **x64 zip'i yaklaşık 0,6 MB** (0,57 MB; WinDivert dahil) |

GoodbyeDPI-Turkey ve Shroudly sütunları bu projelerin kaynak kodunun incelenmesine dayanır (14 Kasım 2026); "—" doğrulanmadı demektir, bellek ve boyut ölçülmedi.

## İndirme

[**Releases**](https://github.com/farmex441-temp/TurkishDPI/releases/latest) sayfasından:

- **`TurkishDPI-vX.Y.Z-x64.zip`** — 64-bit Windows (önerilen). Zip'i bir klasöre çıkarıp `TurkishDPI.exe`'yi çalıştırın; `WinDivert.dll` ve `WinDivert64.sys` exe ile aynı klasörde kalmalı.
- `TurkishDPI-vX.Y.Z-x86.zip` — 32-bit Windows (`WinDivert32.sys` de içindedir).
- `SHA256SUMS.txt` — iki zip'in SHA-256 özetleri.

Zip'te tek bir klasör ve içinde şu dosyalar vardır: `TurkishDPI.exe`, `WinDivert.dll`, WinDivert sürücüsü (`WinDivert64.sys`; x86 zip'inde ayrıca `WinDivert32.sys`), `LICENSE`, `NOTICE`, `THIRD_PARTY_LICENSES.txt`, `WinDivert-LICENSE.txt` ve `BENIOKU.txt`. x64 zip'inin boyutu yaklaşık 0,6 MB'tır (v2.0.1: 0,57 MB); x86 zip'i biraz daha büyüktür.

> Exe'nin içine sürücü gömülmez, sürücü dosyaları zip'te ayrı durur: v1.0'daki tek dosyalık sürüm, içine gömülü sürücü yüzünden bazı antivirüslerce işaretleniyordu.

### İndirmeyi doğrulama

İndirdiğiniz zip'in bozulmadığını ya da yolda değiştirilmediğini şöyle denetleyebilirsiniz (dosya adını kendi indirdiğinizle değiştirin):

```bat
cd /d "%USERPROFILE%\Downloads"
certutil -hashfile TurkishDPI-v2.0.1-x64.zip SHA256
```

(İlk satır, zip'in bulunduğu klasöre geçer; zip başka bir klasördeyse oraya geçin.)

Çıkan 64 karakterlik değeri, Release sayfasındaki `SHA256SUMS.txt` içinde aynı dosya adının karşısındaki değerle karşılaştırın; harf harf aynı olmalıdır (Windows 7/8 özeti bayt çiftleri arasında boşlukla yazar; boşlukları yok sayın). Farklıysa zip'i çıkarmayın, yeniden indirin. Bu denetim indirme sırasındaki bozulmayı ve araya girmeyi yakalar; GitHub hesabının kendisinin ele geçirilmesine karşı koruma sağlamaz (dosyalar kod imzalı değildir).

## Kullanım

1. `TurkishDPI.exe`'yi çalıştırın, yönetici iznini onaylayın.
2. Saatin yanındaki simgeye yerleşir ve çalışan profili kendisi bulur (bu ağda ilk kez çalışıyorsa bir dakika kadar sürebilir; ilerleme pencerede görünür). Bu belgedeki "profil", pencerede "yöntem" diye geçer. TurkishDPI zaten çalışırken `TurkishDPI.exe`'yi yeniden başlatırsanız çalışan pencere öne gelir.
3. Pencereyi kapatınca uygulama saatin yanındaki simgede çalışmaya devam eder. Simgeye tıklayınca pencere açılır, sağ tıklayınca hızlı menü gelir.
4. Pencerenin üç sayfası vardır: **Durum** (büyük Duraklat / Devam et düğmesi, ağ ve hız), **Ayrıntılar** (kullanılan yöntem ve DNS, *Değiştir* ile elle yöntem seçimi, *Yeniden tara*, test siteleri, *Site kontrolü*) ve **Ayarlar**: *Otomatik yöntem seçimi*, *Windows açılışında başlat*, *Ödeme koruması* anahtarları; *Günlüğü aç*, *Gelişmiş ayarlar* (ini dosyasını açar), *Antivirüs yardımı*, *GitHub sayfası* bağlantıları; *Kaldır ve temizle…* ve *Çıkış*. Simgeye sağ tıklayınca menüde *TurkishDPI'ı aç*, *Duraklat / Devam et*, *Yeniden tara* ve *Çıkış* bulunur.

"Windows açılışında başlat" seçeneği, TurkishDPI'ı (exe, WinDivert dosyaları ve lisanslar) korumalı `C:\Program Files\TurkishDPI` klasörüne (32 bit sürüm 64 bit Windows'ta: `C:\Program Files (x86)\TurkishDPI`) kopyalar ve Görev Zamanlayıcı'ya **o kopyayı** çalıştıran yönetici yetkili bir oturum açma görevi ekler; her açılışta UAC sorusu çıkmaz. Görev hiçbir zaman zip'i çıkardığınız klasörü (ör. İndirilenler) çalıştırmaz: o klasöre başka programlar da yazabildiği için bu, onlara UAC'sız yönetici yetkisi verirdi. Yeni bir sürümü çalıştırdığınızda korumalı kopya kendiliğinden güncellenir.

### Nasıl çalışır?

1. **İnternet kontrolü** – Engelsiz sitelere (Google, Microsoft) erişim yoksa bekler, profil denemesi yapmaz.
2. **DNS seçimi** – Standart dışı portlardaki temiz DNS sunucularının (Yandex :1253, AdGuard :5353, OpenDNS :5353/:443) hepsini paralel ölçer ve sahte cevap vermeyen en hızlısını seçer. Hibrit modda her sorgu hem ağınızın DNS'ine hem bu temiz sunucuya gider; ilk güvenilir cevap kullanılır. (Gizlilik notu: bu modda DNS sorgularınız seçilen temiz DNS sağlayıcısına da gider; istemezseniz ini'de `DnsHybrid=0`.)
3. **Profil taraması** – Profilleri bu ağda öğrenilmiş başarı oranına ve hıza göre sıralar, motoru başlatır ve engelli test sitelerine (varsayılan: `discord.com`, `www.roblox.com`, `www.wattpad.com`) **gerçek TLS bağlantısı** kurar. Yeni bir ağda ilk çalışan profilden sonra iki aday daha ölçülür ve en hızlısı seçilir.
4. **Bekçi** – Ağ değiştiğinde, uykudan uyanınca ve belirli aralıklarla test eder. Bir test başarısız olursa 20 saniye sonra tekrar dener; ancak ikinci kez de başarısız olursa yeniden tarar. Motor çökerse yeniden başlatır.

### Motor nasıl atlatır?

Bir web bağlantısının ilk paketinde (TLS ClientHello ya da HTTP isteği) sitenin adı açıkça yazar; ISS'nin filtresi bu adı okuyup bağlantıyı keser. Motor bu paketi yakalar ve:

- gerçek paketi **böler** ve parçaları **ters sırada** gönderir: önerilen profilde ilk parça yalnızca 2 bayttır, filtre ne sınıflandırabileceği kadar veri ne de bir TLS başlığı görür; sunucu parçaları yine doğru sırayla birleştirir;
- gereken ağlarda önce **sahte bir kopyasını** gönderir: filtreye ulaşır ama sunucuya varmadan ölür (düşük TTL, bozuk TCP sağlama toplamı, adı değiştirilmiş);
- filtrenin bağlantıyı kesmek için uydurduğu **RST paketlerini** düşürür;
- ağ kartı bölmeyi donanıma bıraktığında motora tek bir büyük ilk segment gelebilir (ör. ~1,8 KB'lık bir ClientHello); bunu en çok 1200 baytlık parçalara keser (1500 bayta kadar olan paketler 2.0.1'deki gibi gönderilir). Ad ilk segmentte yoksa ad tabanlı profiller 2. bayttan sonra keser;
- QUIC/HTTP3'ü engeller ki tarayıcı bu tekniklerin işlediği TCP'ye geçsin;
- DNS sorgularını temiz bir DNS'e de gönderir (hibrit yarış).

Banka ve ödeme bağlantılarına hiç dokunulmaz.

### Profiller

Profiller `src/app/presets.h` içindeki sırayla denenir (10 profil; ilk yedisi DNS yönlendirmeli, son üçü DNS'e dokunmayan varyantlardır). Tabloda her profilin motor argümanları (`--drop-rst 1` ve DNS hariç) gösterilmiştir; `+ DNS` temiz DNS argümanlarını (otomatik seçilir) temsil eder. Sahte paket kullanan profillerde `--fake-ttl` değeri de vardır (tabloda gösterilmiştir).

| # | Profil (arayüzdeki ad) | Motor argümanları |
|---|---|---|
| 0 | Ters bölme (önerilen) | `--desync none --split disorder --split-pos 2` + DNS |
| 1 | Ters bölme + sahte paket (TTL 5) | `--desync fake --split disorder --split-pos 2 --autottl 0 --fake-ttl 5` + DNS |
| 2 | Sahte + ters bölünmüş (ölçülen TTL) | `--desync fakeddisorder --split-pos 2 --autottl 1 --fake-ttl 5` + DNS |
| 3 | Sıralı bölme + sahte paket (TTL 5) | `--desync fake --split on --split-pos 2 --autottl 0 --fake-ttl 5` + DNS |
| 4 | TLS kaydı bölme (Superonline) | `--desync fake --split on --tlsrec 1 --autottl 1 --fake-ttl 4` + DNS |
| 5 | Ad içinden bölme + sahte paket | `--desync fake --split on --autottl 1 --fake-ttl 4` + DNS |
| 6 | Ad içinden ters bölme + sahte | `--desync fakeddisorder --autottl 1 --fake-ttl 4` + DNS |
| 7 | Ters bölme (DNS yönlendirmesiz) | profil 0'ın DNS'siz hali |
| 8 | Ters bölme + sahte (DNS yönlendirmesiz) | profil 1'in DNS'siz hali |
| 9 | TLS kaydı bölme (DNS yönlendirmesiz) | profil 4'ün DNS'siz hali |

Hepsi `--drop-rst 1` ile sahte RST'leri düşürür ve (motorun varsayılanı olarak) QUIC'i engeller.

**Ölçüm (Türk Telekom hattı, her teknik 5 deneme × 4 site):** İlk isteği 2. bayttan sonra bölüp parçaları ters sırada göndermek her engelli siteyi her seferinde açtı ve gecikme eklemedi (~150 ms; motor kapalıyken engelsiz siteler ~115 ms). Bölmeyi sitenin adının içinden yapmak güvenilmezdi (roblox 1/5); ölçülen TTL'li sahte paketin ardından gerçek verinin gelmesi ~300 ms ekledi (filtre gerçek ilk parçayı düşürüyor, Windows 300 ms sonra yeniden gönderiyor); 1. bayttan sonra bölmek hiç işe yaramadı. Sahte paketli profiller, sahte paket gerektiren ağlar için yedekte durur; otomatik tarama her ağda en hızlı çalışanı seçer.

### v2.0.0 ölçümü (`--selftest`, Türk Telekom, 8 Kasım 2026)

Geliştiricinin kendi hattında yapılmış tek seferlik ölçümdür; sonuçlar ağa ve saate göre değişir. Kendi hattınızda `--selftest` ile ölçebilirsiniz (aşağıya bakın).

| | Sonuç |
|---|---|
| Engelli test siteleri (discord, roblox, wattpad) | 3/3, ~160 ms |
| Discord (site, ağ geçidi, CDN, güncelleyici) | hepsi açık, 150–165 ms |
| TurkishDPI'ın eklediği gecikme (kontrol siteleri) | x64: +6 ms, x86: −1 ms |
| Ödeme siteleri (PayPal, Garanti, iyzico) | açıldı, bağlantılara dokunulmadı |
| Var olmayan alan adı (NXDOMAIN) | 44–191 ms'de hata (beklemeden) |

### Kendi kendine test

```bat
TurkishDPI.exe --selftest
```

Yönetici izni ister. Program profilleri tarar, ödeme sitelerinin dokunulmadan geçtiğini doğrular, motorun eklediği gecikmeyi ölçer, sonucu günlüğe yazıp kapanır. Sonucu `C:\ProgramData\TurkishDPI\TurkishDPI.log` içindeki `SELFTEST` satırlarında görürsünüz; son `SELFTEST` satırı `PASS` ya da `CHECK` olur (ardından bir `exit` satırı gelebilir). `PASS` için koruma açık ya da bekleme modunda olmalı, üç ödeme sitesi açılmalı ve ödeme koruması atlanan bağlantı saymalıdır; bu yüzden `PaymentProtection=0` iken sonuç her zaman `CHECK` çıkar. TurkishDPI zaten çalışıyorsa (tepsideki simge dahil) test çalışmaz (çıkış kodu 3); önce onu kapatın. Günlük 1 MB'ı geçince eski içerik `TurkishDPI.log.old` adıyla kenara alınır ve yeni günlük sıfırdan başlar (önceki `.old` silinir).

### Ayarlar

`C:\ProgramData\TurkishDPI\TurkishDPI.ini` (klasör yalnızca Yöneticiler tarafından okunabilir; *Ayarlar → Gelişmiş ayarlar* dosyayı Not Defteri'nde açar). `[TurkishDPI]` bölümündeki anahtarların hepsi okunur; olmayanlar için gösterilen varsayılan kullanılır. `[net_…]` bölümleri ise programın öğrendiği verilerdir. Dosya yalnızca program açılırken okunur ve program çalışırken kendi anahtarlarını (otomatik mod, ödeme koruması, seçilen profil, öğrenilenler…) yeniden yazar; bu yüzden elle değiştirmeden önce TurkishDPI'ı kapatın (simge → *Çıkış*), dosyayı düzenleyin, sonra yeniden başlatın.

**Önemli:** Windows'un ini okuyucusu satır sonundaki `; açıklama` kısmını değerden ayırmaz. Açıklamayı değerin yanına yazarsanız `Targets` ve `Control` listesine açıklamadaki sözcükler de site olarak eklenir. Açıklamalar bu yüzden aşağıda ayrı satırlarda, `;` ile başlayarak verilmiştir; kendi dosyanızda da böyle yapın.

```ini
[TurkishDPI]
; 1 = otomatik mod, 0 = elle seçilen profil
Auto=1
; son çalışan profil (0-9, bkz. Profiller)
Preset=0
; son çalışan temiz DNS sunucusu (0-5, presets.h sırası)
Dns=0
; 1 = banka/ödeme sitelerine dokunma
PaymentProtection=1
; 1 = DNS sorgularını hem ağın DNS'ine hem temiz DNS'e gönder
DnsHybrid=1
; bekçi aralığı, dakika (1-240; dışındaki değerler bu aralığa çekilir)
WatchdogMinutes=10
; 1 = günde bir kez GitHub'da yeni sürüm kontrolü
CheckUpdates=1
; engelli test siteleri; virgül, boşluk veya ; ile ayrılır, en çok 8 site
Targets=discord.com,www.roblox.com,www.wattpad.com
; engelsiz kontrol siteleri (internet var mı?); en çok 8 site
Control=www.google.com,www.microsoft.com

; akıllı motorun her ağ için öğrendikleri (program yazar, elle düzenlemeyin)
[net_<ağ-kimliği>]
; ağ bağdaştırıcısının adı (ör. Wi-Fi, Ethernet)
Name=Wi-Fi
; profil 0: deneme, başarı, ortalama gecikme (ms)
e0=12,11,84
; bu ağın temiz DNS tercihi
Dns=2
; engel sayfası adreslerinin son öğrenildiği zaman (1601'den beri geçen saniye; program yazar)
bt=13400000000
; bu ağın DNS'inin engelli adlara verdiği engel sayfası adresleri
b0=195.175.254.2
```

Yalnızca boş bırakılan `Targets`/`Control` listesi yerine varsayılan liste kullanılır; yazdığınız alan adları denetlenmez.

### Komut satırı

```bat
TurkishDPI.exe                       :: tepsi uygulaması (otomatik)
TurkishDPI.exe --tray                :: sessiz başlat (Windows açılışında bu kullanılır)
TurkishDPI.exe --selftest            :: tara, ödeme korumasını doğrula, günlüğe yaz, çık
TurkishDPI.exe --help                :: kısa yardım penceresi (-h, /?)
TurkishDPI.exe --engine --help       :: yalnızca motor ve seçenekleri
TurkishDPI.exe --engine --desync fakeddisorder --autottl 1 --dns-addr 208.67.222.222 --dns-port 5353 --dns-hybrid
```

`--engine` seçenekleri (`TurkishDPI.exe --engine --help` ile de görülür): `--fake-ttl`, `--autottl`, `--drop-rst`, `--split off|on|disorder`, `--split-pos`, `--desync fake|fakedsplit|fakeddisorder|none`, `--seqovl`, `--tlsrec`, `--decoy-name`, `--udp-fake`, `--no-quic-block`, `--dns-addr`, `--dns-port`, `--dnsv6-addr`, `--dnsv6-port`, `--dns-hybrid`, `--dns-block-ip`, `--exclude DOSYA` (her satırda bir alan adı, `#` yorum), `--no-payment-exclude`. `--ui-preview` gibi geliştirici seçenekleri `--help`'te yer almaz.

## Gizlilik ve güvenlik

**Bilgisayarınızdan neler çıkar:**
- **DNS:** Hibrit DNS açıkken (varsayılan) her DNS sorgusu hem ağınızın DNS'ine hem de seçilen temiz DNS'e (Yandex, AdGuard veya OpenDNS; en hızlısı) gider. Bu sorgular şifresizdir. İsterseniz `DnsHybrid=0` ile yalnızca temiz DNS kullanılır.
- **Bağlantı testleri:** Profil seçimi ve bekçi için test sitelerine (varsayılan discord.com, roblox.com, wattpad.com) ve kontrol sitelerine (google.com, microsoft.com) yaklaşık 10 dakikada bir HTTPS bağlantısı kurulur; içerik indirilmez.
- **Güncelleme kontrolü:** Günde bir kez GitHub API'sine sürüm sorusu gider (`CheckUpdates=0` ile kapatılır). Güncellemeyi yalnızca siz **Güncelle →** düğmesine bastığınızda indirir; kendiliğinden hiçbir şey kurmaz.
- **Güncellemenin güvenliği:** Dosyalar yalnızca HTTPS üzerinden `github.com`'dan (ve GitHub'ın indirmeleri yönlendirdiği `*.githubusercontent.com`'dan) alınır; adresler program tarafından izlenir ve her biri denetlenir; indirme süresi sınırlıdır (özet dosyası için 30 saniye, zip için 3 dakika); zip, aynı sürümdeki `SHA256SUMS.txt` ile tutmazsa ya da içindeki exe'nin dosya sürümü etiketle aynı ve çalışandan yeni değilse kurulmaz. Dosyalar kod imzalı olmadığı için bu kontrol bozuk ya da yolda değiştirilmiş bir indirmeyi yakalar, GitHub hesabının kendisini ele geçiren birine karşı koruma sağlamaz. Exe'nin yanındaki WinDivert dosyaları zip'tekilerle birebir aynı değilse (ya da zip'te yoksa) otomatik güncelleme yapılmaz, indirme sayfası açılır.
- Bunların dışında hiçbir veri gönderilmez: telemetri, hesap veya analiz yoktur.

**Yerelde ne saklanır:** `C:\ProgramData\TurkishDPI` yalnızca Yöneticiler ve SYSTEM tarafından okunabilir. Ayarlar, öğrenilen ağ bilgisi (ağ geçidi kimliği ve bağdaştırıcı adı) ve günlük burada durur. Günlük, ziyaret ettiğiniz siteleri kaydetmez.

**Yönetici yetkisiyle çalışan bir program olarak alınan önlemler:**
- WinDivert DLL'i ve sürücüleri derleme anında sabitlenen SHA-256 ile doğrulanır; başka bir dosya yüklenmez veya kopyalanmaz.
- Sistem kitaplıkları (winhttp, iphlpapi, dnsapi, bcrypt, gdiplus) yalnızca System32'den yüklenir; exe'nin klasörüne bırakılan bir DLL yüklenmez.
- "Windows açılışında başlat" yalnızca yöneticiye ait `C:\Program Files\TurkishDPI` kopyasını (32 bit sürüm 64 bit Windows'ta: Program Files (x86)) çalıştırır; kopyalanamazsa görev hiç oluşturulmaz (ya da eskisi silinir).
- Veri klasörü yöneticiye aittir; başkasına ait dosya veya bağlantı (junction/symlink) bulunursa kaldırılır, kaldırılamazsa program başlamaz.
- Bağlantılar ve günlük, kullanıcının dosya ilişkilendirmeleri kullanılmadan doğrudan `explorer.exe`/`notepad.exe` ile açılır.

## Sorun giderme

- **Antivirüs:** TurkishDPI.exe sürücü taşımaz; yalnızca "Windows açılışında başlat" açıkken kendisini ve WinDivert dosyalarını Program Files'a kopyalar. Exe, yanındaki `WinDivert.dll` ve sürücü dosyalarının resmi sürümle aynı olduğunu (SHA-256) doğrulamadan onları yüklemez. Paketteki `WinDivert64.sys`, WinDivert'in resmi imzalı sürücüsüdür; bazı üreticiler (ör. Elastic, Kaspersky "not-a-virus", Lionic) **her** WinDivert sürücüsünü genel bir kuralla "WinDivert"/"RiskTool" olarak etiketler. Bu, WinDivert kullanan her araçta (zapret, Shroudly'nin gelişmiş modu…) aynıdır ve imzalı sürücü değiştirilemez. Windows Defender'ın işaretlediği şey ise WinDivert'in resmi zip arşivindeki örnek araçlardır; TurkishDPI bunları dağıtmaz. Başka bir antivirüs engellerse uygulamadaki *Ayarlar → Antivirüs yardımı* adım adım yol gösterir; TurkishDPI hiçbir zaman antivirüs ayarlarınızı kendisi değiştirmez.
- **Kaspersky:** WinDivert sürücüsünü engelleyebilir (hata 1275); gerekirse kaldırılması gerekir.
- **Çakışan hizmet:** Bilgisayarda `GoodbyeDPI` ya da `zapret` (`winws1`/`winws2`) hizmeti çalışıyorsa TurkishDPI açılışta sorar ve onayınızla hizmeti durdurup kaldırır. Çalışan başka bir DPI/VPN aracı varsa pencerede uyarı çıkar.
- Ayrıntılı kayıt: *Ayarlar → Günlüğü aç* (dosya: `C:\ProgramData\TurkishDPI\TurkishDPI.log`; 1 MB'a gelince döndürülür).

### Sürücü hataları

Motor başlayamazsa pencerede başlık "Sürücü başlatılamadı" olur (açıklama dosya sorunuysa "Dosyalar uygun değil") ve altında aşağıdaki açıklamalardan biri görünür. Hata kodu bilinen kodlarda pencerede görünmez; günlükte `Error opening filter: <kod>`, `engine did not start (… err=<kod>)` ve `driver error <kod>: stopping…` satırlarında yazar. Sürücü hatalarında (87 hariç) program tek deneme yapar, başka profil denemez ve profillerin öğrenilmiş sırasını bozmaz. Tablo pencerede gördüğünüz açıklamaya göre sıralıdır.

| Pencerede görünen açıklama | Kod (`err=`) | Ne yapmalı |
|---|---|---|
| WinDivert64.sys (32 bit Windows'ta WinDivert32.sys) bulunamadı. Zip'teki tüm dosyaları exe ile aynı klasöre çıkarın; dosya varsa antivirüs karantinasına bakın. | 2 | Zip'in tamamını çıkarın, karantinayı denetleyin |
| WinDivert.dll bulunamadı. Zip'teki tüm dosyaları exe ile aynı klasöre çıkarın; dosya varsa antivirüs karantinasına bakın. | 126 | Aynısı |
| WinDivert.dll bu Windows'a uymuyor (x64/x86 zip'i karışmış olabilir). Doğru zip'i indirin. | 193 | Doğru zip'i indirin |
| Yönetici izni gerekiyor. Programı yönetici olarak çalıştırın. | 5 | Yönetici olarak çalıştırın |
| ARM işlemcili Windows desteklenmiyor: WinDivert sürücüsünün ARM64 sürümü yok. x64 veya x86 işlemcili bir bilgisayar gerekir. | 50 | Çözümü yok (desteklenmeyen işlemci) |
| Sürücü imzası doğrulanamadı. Windows güncellemelerini yükleyin. | 577 | Güncellemeleri yükleyin (Windows 7'de KB4474419) |
| Başka sürümde bir WinDivert sürücüsü yüklü (eski GoodbyeDPI/zapret?). Onları kapatıp bilgisayarı yeniden başlatın. | 654 | Diğer aracı kapatın, yeniden başlatın |
| WinDivert dosyaları beklenenler değil (değiştirilmiş ya da başka sürüm), güvenlik için yüklenmedi. Resmi zip'i yeniden indirin. | 1006 | Resmi zip'i yeniden indirin |
| Sürücü bir güvenlik yazılımı tarafından engellendi (ör. Kaspersky). | 1275 | Güvenlik yazılımını ayarlayın (*Antivirüs yardımı*) |
| Base Filtering Engine (BFE) hizmeti kapalı. Hizmetler'den açın. | 1753 | `services.msc` içinde BFE'yi başlatın |
| WinDivert dosyalarına erişilemiyor: antivirüs engellemiş ya da başka bir program kullanıyor olabilir. | 1920 | Antivirüsü ve dosyayı kullanan programı denetleyin |

Listede olmayan bir kodda pencere yine "Sürücü başlatılamadı" der ve altında genel bir metin gösterir: "WinDivert sürücüsü açılamadı (hata <kod>). Başka bir WinDivert aracı (GoodbyeDPI, zapret) çalışıyorsa kapatıp yeniden deneyin." Kod bu metinde yazar. (Hiçbir profil bir test sitesini açamıyor ama siteler motor olmadan açılıyorsa pencere "Engel yok" der ve bekleme modunda kalır; sürücü sorunu olmayıp profiller çalışmıyorsa başlık "Çalışamıyor", metin "Çalışan profil bulunamadı. Günlük dosyasına bakın" olur.)

### Güncelleme çalışmazsa

Güncelleme akışı: pencerede *Yeni sürüm hazır: vX.Y.Z* çıkar, **Güncelle →**'ye basınca program `SHA256SUMS.txt`'i (en çok 30 saniye) ve zip'i (en çok 3 dakika) `github.com`'dan indirir, zip'in özetini doğrular, zip'teki exe'nin dosya sürümünün etiketle aynı ve çalışan sürümden yeni olduğunu denetler. Sonra çalışan exe'yi `TurkishDPI.exe.<sayı>.old` adıyla kenara alıp yenisini aynı klasöre koyar, ardından lisans ve BENIOKU metinlerini yeniler (metinler yalnızca exe yerleştirildikten sonra değişir) ve yeniden açılır; `.old` dosyası sonraki açılışta silinir. Açılışta yalnızca `TurkishDPI.exe.<sayı>.old` ile paketteki dosya adlarından `<ad>.upd-new` biçimindeki artıklar silinir, başka dosyaya dokunulmaz. İki başarısızlık biçimi vardır:

- **Olağan durum:** İndirme, doğrulama ya da yerleştirme başarısız olur, eski exe yerinde kalır (gerekirse geri konur), program çalışmaya devam eder, pencerede *Güncelleme yapılamadı: vX.Y.Z* yazar ve düğme *Sayfa →* olur (sürüm sayfasını açar). Lisans/BENIOKU metinleri exe'den sonra değiştirildiği için bu durumda eski kalır.
- **Nadir durum:** Yerleştirme sırasında bir şey ters gider (ör. yeni exe yazıldıktan sonra değişir) ve eski exe geri konamaz ya da çalışan exe yeniden kilitlenemezse program "Güncelleme tamamlanamadı. TurkishDPI kapatılıyor." iletisini gösterir, Tamam'a basınca sürümler sayfasını açar ve kapanır; son sürümü oradan indirin (`TurkishDPI.exe` adındaki dosyanın doğrulanmış olduğu varsayılamaz, yeniden açmayın).

- Nedenini günlükteki `update: failed (…)` satırı gösterir. İndirme hataları `download SHA256SUMS.txt: …` ya da `download zip: …` biçimindedir ve ayrıntı olarak `HTTP <kod>`, `error <kod>`, `took longer than N seconds`, `too many redirects`, `redirect refused`, `too large` ya da `out of memory` içerir. Diğer nedenler: `bad tag`, `zip not in SHA256SUMS.txt`, `zip SHA-256 mismatch`, `broken zip`, `no exe in zip`, `exe for another CPU`, `exe version differs from the tag`, `release is not newer than this version`, `open folder`, `exe path`, `write exe`, `self info`, `running exe busy or not found in its folder`, `move running exe aside`, `move new exe in place`, `new exe changed or held open after writing`.
- Exe'nin yanındaki WinDivert dosyalarının her biri zip'te bulunmalı ve birebir aynı olmalıdır; değilse (`new WinDivert files (manual update needed)`) otomatik güncelleme bilerek yapılmaz ve sürüm sayfası kendiliğinden açılır.
- **Elle güncelleme:** Sürüm sayfasından zip'i indirip doğrulayın (yukarıdaki *İndirmeyi doğrulama* bölümü), TurkishDPI'ı kapatın (simge → *Çıkış*) ve zip'teki dosyaları eski klasöre kopyalayıp eskilerin üzerine yazın. "Windows açılışında başlat" açıksa yeni exe'yi bir kez çalıştırmanız yeterlidir: Program Files kopyasını kendisi yeniler (daha yeni bir kopyanın üzerine yazmaz).
- Güncelleme kontrolünü kapatmak için ini'de `CheckUpdates=0`.

### Elle kaldırma

*Ayarlar → Kaldır ve temizle…* program açılıyorsa en kolay yoldur: görevi, WinDivert hizmetlerini, `C:\ProgramData\TurkishDPI` klasörünü ve Program Files kopyasını siler. Program hiç açılmıyorsa aynısını elle yapın. Aşağıdaki komutlar **Komut İstemi (cmd)** içindir, PowerShell'de çalışmaz (`rmdir /S /Q` ve `%DEĞİŞKEN%` orada farklıdır). Windows 11'deki "Terminal (Yönetici)" PowerShell açabilir: önce `cmd` yazıp Enter'a basın ya da Başlat'ta *Komut İstemi* → sağ tık → *Yönetici olarak çalıştır* deyin.

```bat
:: 1) Çalışan TurkishDPI'ı ve motor sürecini kapat. Uygulama ile motor aynı
::    exe adını taşır; motor önce kapanırsa uygulama yenisini başlatabilir,
::    bu yüzden komutu iki kez çalıştırın (ikincisi "bulunamadı" derse tamam)
taskkill /F /T /IM TurkishDPI.exe
taskkill /F /T /IM TurkishDPI.exe

:: 2) Windows açılışında başlat görevini sil
schtasks /Delete /F /TN "TurkishDPI"

:: 3) WinDivert sürücü hizmetlerini durdur ve sil (PowerShell'de "sc" başka
::    bir komuttur, bu yüzden sc.exe; olmayan hizmet için "hizmet yok"
::    hatası normaldir)
sc.exe stop WinDivert
sc.exe delete WinDivert
sc.exe stop WinDivert14
sc.exe delete WinDivert14
sc.exe stop TurkishDPI
sc.exe delete TurkishDPI

:: 4) Ayarlar ve günlük (yalnızca Yöneticiler erişebilir)
rmdir /S /Q "%ProgramData%\TurkishDPI"

:: 5) "Windows açılışında başlat" için yapılan korumalı kopya
rmdir /S /Q "%ProgramFiles%\TurkishDPI"

:: 6) 32 bit sürümü 64 bit Windows'ta kullandıysanız
rmdir /S /Q "%ProgramFiles(x86)%\TurkishDPI"
```

Notlar: "Kaldır ve temizle" tam olarak bu üç hizmet adını (`WinDivert`, `WinDivert14`, `TurkishDPI`) durdurup siler; hangisinin gerçekten bulunduğu kuruluma bağlıdır. Sürücü o an kullanımdaysa `sc.exe delete` hizmeti bir sonraki yeniden başlatmada siler. 32 bit (x86) sürümün korumalı kopyası 64 bit Windows'ta `C:\Program Files (x86)\TurkishDPI` altındadır (`sys_install_dir` WOW64 işleminde Program Files (x86) klasörünü verir). İndirdiğiniz `TurkishDPI.exe` ve zip'ten çıkan dosyalar bu adımlarla silinmez, onları kendiniz silebilirsiniz.

## Derleme

```bash
# MinGW-w64 (Linux/MSYS2) veya llvm-mingw
make ARCH=x86_64      # 64-bit
make ARCH=i686        # 32-bit
# WinDivert.dll ve WinDivert64.sys exe ile aynı klasöre konur
```

Derleme için `third_party/WinDivert/include/windivert.h` başlığı depodadır; DLL ve sürücü depoda yoktur. Sürüm exe'sinde yalnızca hata ayıklama bilgisi atılır (`-Wl,-S`); sembol tablosu bilerek kalır. Yerel derlemede WinDivert dosyaları `$(WINDIVERT)` içinde yoksa Makefile uyarır ve özet gömmez; böyle bir exe yayımlanmamalıdır. GitHub Actions her `v*` etiketi için x64/x86 sürümlerini (Ubuntu üzerinde MinGW-w64 ile) derleyip Release'e yükler; WinDivert 2.2.2-A arşivi iş akışında SHA-256 ile sabitlenmiştir. Yerelde llvm-mingw ile derlenen exe ucrt'ye, CI'daki MinGW-w64 sürümü msvcrt'ye bağlanır (kod ikisinde de derlenmelidir; bkz. CLAUDE.md). Etiket `src/app/version.h` ile aynı değilse CI derlemeyi durdurur; Release önce taslak olarak oluşturulur, üç dosyası (iki zip ve `SHA256SUMS.txt`) doğrulanınca yayımlanır, yayımlanmış bir sürüm yeniden yazılmaz. CI ayrıca zip'teki exe'leri ve x64/x86 birim testlerini çalıştırır.

### WinDivert'i değiştirmek (LGPL)

WinDivert, LGPL v3 ya da GPL v2 (çift lisans) ile dağıtılır; kaynak kodu <https://github.com/basil00/WinDivert> deposundadır. Sürüm (release) derlemelerinde TurkishDPI, kendisiyle gelen `WinDivert.dll`'in ve sürücünün SHA-256 özetini derleme sırasında exe'ye gömer ve özeti tutmayan bir DLL/sürücüyü yüklemez (hata 1006). Makefile, `$(WINDIVERT)` klasöründe bu dosyalar yoksa özet gömmez (özet gömülmeden derlenen exe bu denetimi yapmaz); resmi sürümleri üreten CI ise dosyalar yoksa derlemeyi durdurur. Kendi derlediğiniz ya da değiştirdiğiniz bir WinDivert'i kullanmak için TurkishDPI'ı o klasörle yeniden derleyin:

```bash
make ARCH=x86_64 WINDIVERT=/yol/WinDivert-klasörü
```

Klasörün düzeni resmi arşivle aynı olmalıdır: `include/windivert.h` ile `x64/` (ve x86 için `x86/`) altında `WinDivert.dll` ve `WinDivert64.sys` (x86'da ayrıca `WinDivert32.sys`). Makefile özetleri bu dosyalardan hesaplar (`WINDIVERT_DLL_SHA256`, `WINDIVERT_SYS64_SHA256`, `WINDIVERT_SYS32_SHA256`).

## Lisans ve teşekkür

Apache License 2.0 — ayrıntılar için [LICENSE](LICENSE) ve [NOTICE](NOTICE).

- [umutxyp/Shroudly](https://github.com/umutxyp/Shroudly) — motor çekirdeği: `src/engine/core/` ve `engine.c`'nin IPv4 paket yolu (MIT, metni [THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt))
- 1.x sürümleri [ValdikSS/GoodbyeDPI](https://github.com/ValdikSS/GoodbyeDPI) tabanlı bir motor kullanıyordu (Apache 2.0); 2.0'da o kod kaldırıldı.
- [WinDivert](https://reqrypt.org/windivert.html) 2.2.2 — basil00 (LGPL v3 ya da GPL v2, çift lisans; kaynak: <https://github.com/basil00/WinDivert>). Zip'te değiştirilmeden, imzalı haliyle gelir; bkz. yukarıdaki *WinDivert'i değiştirmek* bölümü.

Logo ve simgeler bu projeye özeldir (`tools/make_icons.py` ile üretilir).

> Bu yazılım yalnızca erişim hakkınız olan içeriklere ulaşmak içindir; kullanımından doğan sorumluluk kullanıcıya aittir.
