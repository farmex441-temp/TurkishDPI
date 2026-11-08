# TurkishDPI – Mimari: Mevcut ve Hedef

> Mevcut mimarinin kural ve gerekçeleri için [CLAUDE.md](../CLAUDE.md) esastır. Bu belge
> yalnızca (1) koddan çıkarılan güncel resmi, (2) önerilen hedef mimariyi ve (3) geçiş
> yolunu anlatır. Güvenlik gerekçeleri: [SECURITY-REVIEW.md](SECURITY-REVIEW.md).

## 1. Mevcut mimari (v1.1.0)

```
                    ┌──────────────────── TurkishDPI.exe (Yönetici, tek süreç) ───────────────────┐
 Kullanıcı ──GDI──▶ │ UI thread (ui.c)          Controller thread (app.c)        Update thread   │
                    │  pencere, tepsi, menü ◀──g_ui──  auto_scan / watchdog      GitHub API      │
                    │        │  post_cmd()  ──queue──▶  site_test / komutlar                     │
                    │        │                          ├ probe.c  (WinHTTP, UDP DNS, DnsQuery)   │
                    │        │                          ├ smart.c  (ini öğrenme)                  │
                    │        │                          └ system.c (rakip, proxy, ACL, kaldırma)  │
                    └────────┼──────────────────────────────┬──────────────────────────────────────┘
                             │                              │ CreateProcess + Job + stdout pipe
                             │                    ┌─────────▼──────────────── (Yönetici) ┐
                             │                    │ TurkishDPI.exe --engine ARGS         │
                             │                    │ gdpi_main: WinDivert paket döngüsü    │
                             │                    │ dnsredir (hibrit) · exclude (ödeme)   │
                             │                    └─────────┬────────────────────────────┘
                             │                              │ WinDivert.dll → WinDivert64.sys
  %ProgramData%\TurkishDPI\  (ini, log; korumalı DACL)       ▼ çekirdek
  Görev Zamanlayıcı: oturum açılışında --tray, HighestAvailable
```

Özellikleri:
- **Tek güven alanı:** her şey yönetici. Basit, ama UI ve kullanıcıya özel işler de
  yönetici (SECURITY-REVIEW S1, S3, S4, S5).
- **Tek yönlü protokol:** motor → uygulama stdout satırları. Motor ayarı yalnızca yeniden
  başlatmayla değişir.
- **Durum:** küresel değişkenler + `g_ui`; kalıcılık ini dosyası.
- **Politika ile mekanizma iç içe:** `auto_scan` hem karar veriyor hem süreç başlatıyor hem
  UI güncelliyor.

## 2. Hedef ilkeler

1. **Hafiflik korunur:** saf C, framework yok, pencere kapalıyken birkaç MB.
2. **En az yetki:** yalnızca paket motoru ve sistem ayarı değiştiren küçük bir çekirdek
   yükseltilmiş çalışır. UI ve kullanıcıya özel her şey yetkisizdir.
3. **Karar mantığı platformdan bağımsız ve test edilebilir:** saf C, sistem çağrısı yok,
   CI'da Linux'ta bile çalışan birim testleri.
4. **Platform başına ayrı motor:** her platformun aynı motoru kullanacağı varsayılmaz.
5. **Geçiş adım adım:** her adım tek başına yayınlanabilir; büyük patlama (big-bang)
   yeniden yazım yok.

## 3. Önerilen modül ayrımı

Önerilen "Core / Engine / Platform / Service / UI / Diagnostics / Updates" listesi iyi bir
başlangıç. Ancak **Diagnostics ayrı bir katman olmamalı**, çünkü tanılama, karar
mantığının kullandığı aynı ölçümlerin sınıflandırılmasıdır. Önerim dört katman ve dar
arayüzler:

```
core/                 (taşınabilir C99, sistem çağrısı YOK, %100 birim testli)
  policy.c            tarama/bekçi durum makinesi: "ne yapılmalı?" kararları
  learn.c             ağ başına istatistik, eskime, başarısızlık nedenleri
  classify.c          ölçüm sonuçlarından tanı: DNS_POISON / IP_BLOCK / SNI_BLOCK / …
  dnsmsg.c            DNS kodlama/çözme, sahte cevap tespiti (bugün probe.c+dnsredir.c'de iki kopya)
  presets.c           profil tanımları (platformdan bağımsız soyut teknikler + platform eşlemesi)
  events.h            karar günlüğü olayları (UI'ya ve log'a giden yapılandırılmış kayıtlar)

port/<os>/            (platform adaptörleri, ince)
  probe_*.c           HTTPS/TCP/DNS ölçümleri (Win: WinHTTP/Winsock; Linux: sockets + OpenSSL/mbedTLS?)
  netid_*.c           ağ kimliği, ağ değişimi bildirimi
  store_*.c           kalıcılık (Win: ini veya registry; Linux: /var/lib/turkishdpi)
  engine_*.c          motor yaşam döngüsü (Win: WinDivert alt süreci; Linux: nfqws/kendi NFQUEUE)

engine/<os>/          (paket motorları)
  win/                bugünkü GoodbyeDPI tabanlı motor (+ ödeme koruması, hibrit DNS)
  linux/              NFQUEUE tabanlı motor (bkz. §6)

host/<os>/            (süreç barındırıcılar)
  win-service/        yönetici çekirdek: policy + engine yönetimi + IPC sunucusu
  win-tray/           yetkisiz tepsi/pencere (bugünkü ui.c), IPC istemcisi
  linux-daemon/       systemd servisi + CLI
```

Neden bu ayrım:
- `policy.c` saf bir fonksiyon gibi tasarlanır: girdi (olay + mevcut durum + ölçümler),
  çıktı (eylemler). Bugünkü `auto_scan`'ın `Sleep`/`engine_start` çağrıları adaptöre gider.
  Böylece "bildirim yağmuru", "tek hata ile tam tarama", "STANDBY riski" gibi davranışlar
  **simülasyonla test edilebilir**.
- `classify.c` hem site testinde hem bekçide hem tanılama ekranında aynı kodla çalışır.
- DNS ayrıştırma bugün iki yerde var (`probe.c: dns_answer_ok` ve
  `dnsredir.c: dns_answer_is_forged`); tek kopyaya iner. Motor ve uygulama aynı bloklistesini
  kullanır.

## 4. Windows hedefi: servis + yetkisiz tepsi

```
┌──────── Oturum 0 ───────────────────────────────┐        ┌──── Kullanıcı oturumu (medium IL) ─────┐
│ TurkishDPI Service (LocalSystem)                 │        │ TurkishDPI.exe --tray  (asInvoker)     │
│  core/policy + learn + classify                  │◀─pipe─▶│  pencere, tepsi, bildirimler           │
│  probe_win, netid_win (NotifyIpInterfaceChange)  │        │  kullanıcıya özel işler:               │
│  engine yöneticisi ──▶ engine alt süreci (Job)   │        │   - HKCU proxy kontrol/onarım          │
│  store: %ProgramData%\TurkishDPI (korumalı)      │        │   - Discord updater relay (opsiyon)    │
│  güncelleme doğrulayıcı (imza)                   │        │   - URL/dosya açma                     │
└──────────────────────────────────────────────────┘        └────────────────────────────────────────┘
       Program Files\TurkishDPI\ (Administrators sahibi)  ← tüm ikililer burada
```

**IPC tasarımı**
- Adlandırılmış kanal `\\.\pipe\TurkishDPI`, açık DACL: SYSTEM + Administrators tam;
  `INTERACTIVE` okuma + yazma. Uzaktan erişimi engellemek için
  `PIPE_REJECT_REMOTE_CLIENTS`.
- Mesajlar: sabit başlık + uzunluk + küçük ikili yapılar ya da satır tabanlı
  `KEY=VALUE` (bugünkü stdout protokolünün devamı). JSON ayrıştırıcısı eklemeye gerek yok.
  Her alanın sınırı kontrol edilir; sunucu istemciye asla güvenmez.
- **Yetki sınıfları:**
  - herkes: durum oku, olay günlüğü oku, yeniden tara, duraklat/devam, site testi iste;
  - **yalnızca yönetici**: ödeme korumasını kapat, profil listesini/ayarları değiştir,
    kaldır, güncelleme kur. Sunucu `ImpersonateNamedPipeClient` + yükseltilmiş yönetici
    token kontrolü yapar. Tepsi bu komutlar için `TurkishDPI.exe --admin <eylem>` ile UAC
    ister.
- İstemci doğrulaması: `GetNamedPipeClientProcessId` + oturum kimliği (bilgi amaçlı;
  yetki kararı token'a göre verilir, süreç adına göre değil).

**Neden servis, neden şimdi değil?**
- Kazanç: S1, S3, S4 ve S5 kökten kapanır. UAC'sız otostartın yerini standart bir servis
  alır. Oturum açılmadan önce bile koruma olur (çok kullanıcılı makinelerde tutarlı).
  Hızlı kullanıcı değiştirmede tek motor çalışır.
- Maliyet:
  - Gerçek bir **kurulum adımı** gerekir (Program Files + servis kaydı). Taşınabilir
    "zip aç, çalıştır" deneyimi "ilk çalıştırmada kur" deneyimine dönüşür.
  - Servis + sürücü + otomatik başlama antivirüs sezgiselleri için "kalıcılık" sinyalidir.
    VirusTotal etkisi ölçülmeli; diğer oturumla koordine edilmeli.
  - Oturum 0'dan bildirim gösterilemez; bildirimler tepsiye taşınır.
  - Kod imzalama yoksa SmartScreen uyarısı aynı kalır.
- Öneri: önce SECURITY-REVIEW S1–S3'ün küçük düzeltmeleri (güvenli kurulum klasörü dahil).
  Servis ayrımı, core/policy ayrıştırması bittikten sonra ikinci büyük adım olsun. Mevcut
  `--engine` uyumluluğu ve `sc create` kullanımı korunur.

**x86/x64, Windows 10/11:** Servis ve pipe API'leri Win7+ ile uyumlu. `NotifyIpInterfaceChange`
Vista+. ARM64 için WinDivert sürücüsü yok (Shroudly de ARM64'te gelişmiş modu sunmuyor);
ARM64 desteği iddia edilmemeli.

## 5. Discord, oyunlar ve uygulamalar: mimari etkisi

TurkishDPI paket seviyesinde çalıştığı için Shroudly'nin "proxy'yi dikkate almayan
uygulama" sorunlarının çoğu **bizde yok**:

| Sorun | Shroudly (proxy) | TurkishDPI (WinDivert) | Bizim için gereken |
|---|---|---|---|
| Uygulama sistem proxy'sini yok sayıyor (Roblox, oyun başlatıcıları) | `HTTPS_PROXY` env, URL handler devralma | **Yok**: tüm 80/443 TCP motordan geçer | Hiçbir şey; yalnızca ölç ve doğrula |
| Discord masaüstü ana uygulama | Sistem proxy'si (Chromium) | Motordan geçer | Ölç (gateway.discord.gg WSS) |
| Discord güncelleyicisi TLS 1.2, sertifika açıkta | Yerel relay + `settings.json` `NEW_UPDATE_ENDPOINT`, TLS 1.3 ile yeniden çekme | İstemci hilesi işe yaramayabilir (Shroudly de "Advanced mode dahil" yardımcı olmadığını söylüyor) | Opsiyonel relay (bkz. aşağı) |
| Discord ses (UDP) | Gelişmiş modda IP-discovery/STUN öncesi düşük TTL sahte datagram | **Yok**: motor yalnızca TCP işliyor | Önce ölç, sonra dar UDP filtresi |
| IP engeli | Dürüstçe "VPN gerekir" | Aynı | Tanılama ile açıkça söyle |
| IPv6 QUIC | Shroudly IPv4 QUIC'i düşürüyor | `-q` filtresi IPv4/IPv6 ayırmıyor (ikisini de düşürür) | Bir şey gerekmez |

**Discord güncelleyici relay'i (opsiyonel, ölçümden sonra):**
- `%APPDATA%\discord*\settings.json` kullanıcıya ait. Relay ve ayar değişikliği **yetkisiz
  tepsi** sürecinde olmalı (servis mimarisine iyi oturur; bugün yönetici süreçte yapmak
  S5 sorununu büyütür).
- Kritik teknik soru: relay'in Discord'a **TLS 1.3** ile bağlanması gerekiyor (sertifikanın
  şifreli gitmesi için). **Windows 10'un SChannel'ı TLS 1.3'ü desteklemiyor** (Windows 11 /
  Server 2022 destekliyor). WinHTTP ile yapılan relay Win10'da yine TLS 1.2 kullanır ve
  sorun çözülmez. Seçenekler:
  - (a) yalnızca Win11'de etkinleştirmek;
  - (b) mbedTLS 3.x (Apache-2.0, TLS 1.3, ~200–300 KB) gömmek;
  - (c) TurkishDPI motorunun TLS 1.2 akışında sertifikayı taşıyan **gelen** segmentlere bir
    şey yapıp yapamayacağını araştırmak. Gelen yönde istemci tarafı hilenin işe yaraması
    beklenmez; Shroudly de böyle söylüyor.
  Ölçmeden karar verilmemeli.
- Lisans: Shroudly MIT. Fikir ve davranış alınabilir; kod alınırsa NOTICE'e eklenir.
- Kapsam: Stable/PTB/Canary/Development ayrı klasörlerdir (`discord`, `discordptb`,
  `discordcanary`, `discorddevelopment`). Her biri ayrı test edilmeden "destekleniyor"
  denmemeli. Kullanıcının kendi `NEW_UPDATE_ENDPOINT` değeri varsa dokunulmamalı; kapatınca
  satır geri alınmalı.

## 6. Linux

- GoodbyeDPI motoru WinDivert'e bağlı; Linux'ta **ayrı motor** gerekir. NFQUEUE
  (nftables `queue num N bypass`) + ham soket ile sahte paket enjeksiyonu.
- Seçenekler:
  1. **zapret `nfqws`'yi harici ikili olarak kullanmak** (MIT): olgun, Türkiye'de
     denenmiş stratejiler var. Bizim core'umuz strateji seçimi + öğrenme + tanılama
     yapar, motor olarak `nfqws`'yi argümanlarla başlatır (bugünkü `--engine` modelinin
     aynısı). Lisans uyumlu (MIT → Apache-2.0 projeye NOTICE ile). Dağıtım: dağıtım
     paketlerinde `nfqws` yoksa onu birlikte paketlemek gerekir.
  2. Kendi NFQUEUE motorumuz: `fakepackets.c`/SNI ayrıştırma mantığının taşınması. Daha
     fazla kontrol, ama aylarca iş ve tekrar test.
  **Öneri: (1) ile başla**; core'un motor adaptör arayüzü bunu mümkün kılacak şekilde
  tasarlanır.
- Hibrit DNS: Linux'ta systemd-resolved, dnsmasq ve NetworkManager çeşitliliği yüzünden
  paket seviyesinde DNS yönlendirme riskli. Önerim yerel bir DoH/temiz-DNS stub'ı ya da
  resolved'a `DNS=` ayarı (onayla, geri alınabilir).
- Servis: systemd unit (`CAP_NET_ADMIN`, `CAP_NET_RAW`; root yerine yetenekler),
  nftables kuralları servis durunca temizlenir.
- UI: önce CLI (`turkishdpi status|scan|test <site>|diagnose`). Tepsi (AppIndicator) sonra.
- Dağıtım farkları: nftables vs iptables-legacy, NFQUEUE çekirdek modülü, SELinux/AppArmor.

## 7. macOS

- Paket seviyesinde motor pratik değil: `pf` `divert-to` ancak şeffaf proxy (zapret `tpws`
  gibi) ile çalışır, root gerektirir. Modern yol NetworkExtension'dır; Apple geliştirici
  hesabı, imzalama ve notarizasyon gerektirir.
- **Öneri:** macOS'u şimdilik ertele. İlk değer adayları: tanılama (DNS/IP/SNI ayrımı) ve
  Discord güncelleyici relay'i. Bunlar proxy/motor gerektirmez; ama Discord relay'i için
  de TLS 1.3 kütüphanesi gerekir.

## 8. Geçiş planı (yayınlanabilir adımlar)

1. **Sertleştirme (motor dokunulmadan):** SECURITY-REVIEW S2 (tek satır), S3, güvenli kurulum
   klasörü (S1). Mimariyi değiştirmez.
2. **core çıkarma, davranış değişmeden:** `dnsmsg.c` (iki DNS ayrıştırıcıyı birleştir),
   `learn.c` (`smart.c`'nin saf kısmı), `classify.c` (yeni). Linux üzerinde CI'da birim
   testleri.
3. **policy.c:** `auto_scan`/bekçi kararlarını saf fonksiyona taşı; bildirim politikası,
   iki-vuruş kuralı ve karar günlüğü burada. Simülasyon testleri.
4. **Servis + tepsi** ayrımı (IPC, kurulum, kaldırma güncellemesi).
5. **Linux daemon** (`nfqws` adaptörü), CLI.
6. Opsiyonel modüller: Discord relay, UDP ses denemeleri, imzalı güncelleyici.

Her adımda: x64 ve x86 derlemesi, msvcrt/ucrt farkları (CLAUDE.md "Conventions"), mevcut
testler ve (motor değişirse) ASan/UBSan.
