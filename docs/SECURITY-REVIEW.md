# TurkishDPI – Güvenlik İncelemesi (v1.1.0, `main` @ 55b6244)

> Yöntem: kaynak kod incelemesi. **Hiçbir bulgu canlı istismar ile doğrulanmadı**; her
> bulgu için "kanıt" satırı kodun ilgili yerini gösterir. Önem dereceleri tehdit modeline
> göre verildi.
>
> **Tehdit modeli:** TurkishDPI yönetici yetkisiyle çalışıyor ve ağ trafiğini çekirdek
> seviyesinde değiştiriyor. Saldırgan profilleri:
> - (a) aynı kullanıcı oturumunda **yetkisiz (medium IL)** çalışan bir süreç: tarayıcıdan
>   inen bir dosya, bir eklenti, kötü amaçlı bir "crack" vb.;
> - (b) ağda **yol üzerindeki** taraf: ISS/DPI, kafe Wi-Fi'si;
> - (c) **yol dışı** ağ saldırganı.
>
> Amaç (a)'nın TurkishDPI üzerinden yönetici olamaması, (b) ve (c)'nin kullanıcıya zarar
> verememesi.

## Özet tablo

| ID | Bulgu | Önem | Saldırgan |
|---|---|---|---|
| S1 | Kullanıcının yazabildiği klasördeki exe/DLL, otostart ile UAC'sız yönetici olarak çalışıyor | **Yüksek** | a |
| S2 | DLL arama sırası: `winhttp.dll`, `iphlpapi.dll` (statik) ve `LoadLibrary("dnsapi.dll")` exe klasöründen yüklenebilir | **Yüksek** (S1 ile aynı kök) | a |
| S3 | `WM_COMMAND` için UIPI istisnası: yetkisiz süreç korumayı kapatabiliyor, ayar değiştirebiliyor | Orta | a |
| S4 | Yükseltilmiş Notepad (log/ini) ve tüm UI'nin yükseltilmiş çalışması | Orta-Düşük | a |
| S5 | OTS yükseltmede yanlış kullanıcı (otostart görevi, HKCU) | Orta (işlevsel + güvenlik) | – |
| S6 | Ödeme koruması DoH ve bölünmüş ClientHello durumunda delinebilir | Orta (ürün vaadi) | – |
| S7 | Temiz DNS düz UDP; hibritte tüm sorgular yabancı çözücüye de gidiyor | Düşük-Orta (gizlilik) | b |
| S8 | DNS probunda bağlanmamış soket + tahmin edilebilir TXID | Düşük | c |
| S9 | Güncelleme sistemi (gelecekteki otomatik güncelleyici için şartlar) | Bilgi / tasarım | b |
| S10 | Prob User-Agent'ı aracı ifşa ediyor | Düşük (gizlilik) | – |
| S11 | Global mutex işgali → sessiz açılmama | Düşük | a |
| S12 | Kaldırma başka araçların WinDivert servisini siliyor | Düşük | – |
| S13 | CI tedarik zinciri (sabitlenmemiş Actions, geniş yazma izni) | Düşük-Orta | – |

---

## S1 – Kullanıcının yazabildiği klasörden yükseltilmiş otostart (Yüksek)

**Durum.** Release bir zip. README "bir klasöre çıkarın" diyor; kullanıcılar çoğunlukla
`İndirilenler` ya da `Masaüstü` altına çıkarır. Bu klasörlere kullanıcı (medium IL) yazabilir.
"Windows ile başlat" açılınca görev, `g_exe` yolunu `HighestAvailable` ile **UAC sormadan**
çalıştırıyor (`app.c:1147` `autostart_set`). Exe de `WinDivert.dll`'i kendi klasöründen
yüklüyor (`windivert_dyn.c:41-47`).

**Saldırı.** Medium IL'de çalışan herhangi bir süreç (a) şunlardan birini yapar:
`TurkishDPI.exe`'yi değiştirir, `WinDivert.dll`'i değiştirir ya da S2'deki DLL'lerden birini
klasöre koyar. Bir sonraki oturum açılışında kod **yönetici olarak, UAC penceresi olmadan**
çalışır. Bu, klasik bir UAC atlatma / yetki yükseltme. CLAUDE.md'deki "DLL only from the
exe's own folder" kuralı burada koruma sağlamıyor, çünkü klasörün kendisi güvenilmez.

**Not.** Otostart kapalıyken bile, kullanıcı exe'yi her açtığında UAC onayı verdiği için
aynı ekleme yöntemi işe yarar. Ama o durumda en azından bir UAC penceresi çıkar.

**Öneri (öncelik sırasıyla).**
1. **Güvenli kurulum konumu:** "Windows ile başlat" açılınca (ya da ilk çalıştırmada
   önerilerek) exe, `WinDivert.dll`, `.sys` ve lisanslar `%ProgramFiles%\TurkishDPI`
   altına kopyalansın. Klasör sahibi Administrators olsun, ACL korunsun (`sys_secure_dir`
   ile aynı yaklaşım). Görev bu kopyayı göstersin. Taşınabilir kullanım aynen kalabilir;
   yalnızca otostart güvenli konuma bağlansın.
2. **Klasör güven kontrolü:** Açılışta exe klasörünün ve dosyalarının etkin DACL'si
   kontrol edilsin (`GetEffectiveRightsFromAcl` ya da `AccessCheck` ile Users/Authenticated
   Users için `FILE_WRITE_DATA|FILE_ADD_FILE|DELETE`). Yazılabilirse otostart reddedilsin ve
   kullanıcıya açıkça söylensin.
3. **WinDivert.dll bütünlüğü:** CI'da sabitlenen SHA-256 değeri derleme zamanında exe'ye
   gömülsün. DLL yüklenmeden önce **açık bir tutamaç üzerinden** hash'i doğrulansın
   (TOCTOU olmaması için dosya `FILE_SHARE_READ` ile açık tutulsun). Alternatif:
   `WinVerifyTrust` ile WinDivert imzası. Hash daha basit ve antivirüs açısından nötr.
4. Uzun vadede servis mimarisi (ARCHITECTURE.md) ile yükseltilmiş kod zaten yalnızca
   Program Files'tan çalışır.

## S2 – DLL arama sırası (Yüksek, S1'in kardeşi)

**Kanıt.**
- `Makefile` LIBS: `-lwinhttp -liphlpapi -lcomctl32 -lgdiplus …` (statik import).
  Bu makinenin `KnownDLLs` listesinde `winhttp.dll`, `iphlpapi.dll`, `dnsapi.dll` **yok**.
  Windows statik importları önce **uygulama klasöründen** arar.
- `src/engine/dnsredir.c:167` → `LoadLibrary("dnsapi.dll")`. Motor `--dns-addr`
  ayrıştırılırken (`goodbyedpi.c:899,916`) **her preset başlatmada** yönetici olarak
  çağırıyor. Uygulama tarafındaki `probe.c` doğru biçimde
  `LOAD_LIBRARY_SEARCH_SYSTEM32` kullanıyor; motor kullanmıyor.

**Saldırı.** Exe klasörüne `dnsapi.dll` ya da `winhttp.dll` bırakmak yönetici kod
çalıştırması sağlar. Tarayıcı indirmeleri aynı klasöre düşebildiği için ("Downloads
planting") ayrıca bir saldırgan süreç bile gerekmeyebilir.

**Öneri.**
- `dnsredir.c` → `LoadLibraryExW(L"dnsapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32)`.
- `main()` başında, her şeyden önce `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)`.
  Bu, dinamik yüklemeleri kapatır; statik importları kapatmaz.
- Statik importlar için iki yol var: bağımlılık yükleme bayrağı
  (`/DEPENDENTLOADFLAG:0x800`; GNU ld/LLD'de karşılığı **doğrulanmalı**) ya da
  `winhttp`/`iphlpapi` fonksiyonlarını `LoadLibraryExW(..., SEARCH_SYSTEM32)` ile
  çalışma zamanında çözmek (`windivert_dyn.c` deseni).
- `WinDivert.dll`'i `LOAD_WITH_ALTERED_SEARCH_PATH` yerine
  `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32` ile yüklemek.
- Doğrulama: Process Monitor ile "NAME NOT FOUND" exe-klasörü aramalarını listelemek
  (yönetici + kullanıcı onayı gerekir).

## S3 – `WM_COMMAND` için UIPI istisnası (Orta)

**Kanıt.** `ui.c:771-778`: `ChangeWindowMessageFilterEx(g_wnd, WM_COMMAND, MSGFLT_ALLOW)`.
`command()` herhangi bir `IDM_*` değerini işliyor.

**Etki.** Yetkisiz bir süreç `PostMessage(FindWindow(L"TurkishDPI_main"), WM_COMMAND, …)` ile:
- `IDM_EXIT`: korumayı **onaysız** kapatır;
- `IDM_AUTOSTART`: otostartı onaysız açar/kapatır;
- `IDM_PRESET+i` ve `IDM_AUTO`: profil değiştirir;
- `IDM_LOG`/`IDM_INI`: **yükseltilmiş Notepad** açar (S4);
- `IDM_UNINSTALL`/`IDM_PAYMENT`: onay kutusu çıkarır (sosyal mühendislik için kullanılabilir).

UIPI, düşük süreçten yükseltilmiş Notepad'e girdi göndermeyi engellediği için doğrudan
yetki yükseltme görünmüyor. Ama bütünlük ve DoS etkisi var.

**Neden gereksiz.** Menü komutları (`TrackPopupMenu`) aynı süreçten geliyor; UIPI süreç
içi mesajları zaten engellemiyor. Gerçekten gereken yalnızca `TaskbarCreated` ve `WM_TRAY`.

**Öneri.** `WM_COMMAND` istisnasını kaldırmak. `WM_TRAY` işleyicisinde yalnızca beklenen
`lParam` değerlerini (`WM_LBUTTONUP`, `WM_RBUTTONUP`, `WM_CONTEXTMENU`, `WM_LBUTTONDBLCLK`)
kabul etmek; bu zaten öyle. Test: medium IL bir PowerShell'den `PostMessage` ile
`IDM_EXIT` göndermek; düzeltmeden sonra etkisiz olmalı.

## S4 – Yükseltilmiş UI, yükseltilmiş Notepad (Orta-Düşük)

- Tüm GDI/GDI+ çizimi, menüler ve dosya diyalogları yönetici süreçte. Ekran
  ayrıştırması, bir yazı tipi ya da GDI+ açığı yönetici bağlamında çalışır.
- `app_open_file` Notepad'i **yükseltilmiş** açıyor; "Aç" diyaloğu üzerinden yönetici
  olarak herhangi bir şey çalıştırılabilir. Kullanıcı zaten yönetici olduğu için doğrudan
  açık sayılmaz, ama en az yetki ilkesine aykırı.
- Öneri: kısa vadede log'u **salt okunur** göster (uygulama içi metin penceresi ya da
  Explorer üzerinden yetkisiz Notepad; `Users` grubunun okuma izni var). ini düzenlemeyi
  uygulama içi "Gelişmiş" diyaloğa taşı. Uzun vadede servis + yetkisiz tepsi (ARCHITECTURE).

## S5 – OTS yükseltmede yanlış kullanıcı (Orta)

Standart kullanıcı UAC'ta başka bir yönetici hesabının parolasını girdiğinde süreç o
yöneticinin kimliğiyle çalışır:
- `autostart_set` görevi `USERDOMAIN\GetUserName` ile kaydeder. Görev yönetici hesabının
  oturum açışında tetiklenir; asıl kullanıcının oturumunda **hiç çalışmaz**.
- `sys_proxy_broken/repair` yöneticinin HKCU'sunu okur ve yazar.
- Öneri: etkileşimli oturum kullanıcısını `WTSQueryUserToken` / `WTSGetActiveConsoleSessionId`
  ile bulmak. En temiz çözüm, kullanıcıya özel her şeyin (proxy, Discord ayarı, tepsi)
  yetkisiz tepsi sürecine taşınması.

## S6 – Ödeme koruması delinebilir (Orta, ürün vaadi)

**Kanıt.** `goodbyedpi.c` ~1395-1404: bağlantı iki koşuldan biri tutarsa dokunulmadan
geçer: SNI **ilk TLS segmentinde** bulunup listede eşleşirse (`extract_sni` +
`exclude_check_hostname`) ya da hedef IP daha önce **düz DNS cevabından** öğrenildiyse
(`exclude_learn_dns`, yalnızca UDP/53).

**Delik.**
- Chrome/Edge "Güvenli DNS" ve Firefox TRR ile DNS HTTPS içinde gider; IP öğrenilmez.
- Post-quantum anahtar paylaşımı (X25519MLKEM768) ile ClientHello ~1,8 KB'a çıkar, yani bir
  MSS'den büyük. Chrome uzantı sırasını rastgeleleştirdiği için SNI **ikinci segmentte**
  kalabilir. `extract_sni` ilk segmentte SNI bulamaz.
- ECH kullanan sitelerde dış SNI `cloudflare-ech.com` gibi ortak bir ad olur.
- Bu durumlarda banka bağlantısına sahte paket veya parçalama uygulanır; koruma
  "varsayılan açık" olduğu halde sessizce çalışmaz.

**Öneri.**
1. Önce ölç. Selftest DoH açık bir tarayıcı senaryosunu kapsamıyor (WinHTTP sistem DNS'i
   kullanıyor). Ölçüm, DoH açık Chrome ile banka sitesine gidip motor log'unda
   `excluded` sayacına bakılarak yapılabilir (yönetici + kullanıcı izni gerekir).
2. Motorda **ClientHello birleştirme**: 4'lü anahtarla ilk 2–3 segmenti (≤16 KB) kısa
   süre tutup SNI'yi birleşik tampondan çıkarmak. Bu, paket döngüsünü değiştirir; ASan/UBSan
   altında test edilmeli. Daha hafif alternatif: ilk segmentte SNI yoksa ve
   ClientHello `record length` > segment ise sahte paketi bir sonraki segmente kadar
   ertelemek.
3. UI'da dürüstlük: "Tarayıcıda Güvenli DNS açıksa ödeme koruması yalnızca SNI ile
   çalışır."

## S7 – Temiz DNS gizliliği ve bütünlüğü (Düşük-Orta)

- Temiz çözücülere (Yandex, AdGuard, OpenDNS) düz UDP ile, standart dışı portlardan
  gidiliyor. Yol üzerindeki taraf (b) bu cevapları değiştirebilir. Bugün ISS bunu yapmıyor
  olabilir, ama garanti değil.
- Hibrit modda **tüm** DNS sorguları hem ISS'ye hem yabancı çözücüye gidiyor. Kullanıcıya
  söylenmiyor.
- Öneri: README/UI'da açık bilgi; seçenek olarak DoH/DoT (uygulama tarafında WinHTTP ile
  DoH probu kolay, motor tarafında DoH yönlendirmesi ağır, bkz. ROADMAP); ileride DNSSEC
  doğrulaması gerekmez, DoH yeterli.

## S8 – DNS probu (Düşük)

`probe.c:258` `probe_dns_ms`: `sendto` + `recv`; soket `connect` edilmemiş, yani her
kaynaktan gelen cevabı alır. TXID `GetTickCount() ^ GetCurrentThreadId()`, tahmin edilebilir.
Yol dışı biri (c) DNS seçimini veya engel IP öğrenimini etkileyebilir. Etki düşük: öğrenilen
yanlış bir "engel IP" yalnızca ISS'nin o IP'li cevaplarını düşürür ve sonuçta temiz çözücüye
düşülür. Öneri: `connect()` + `BCryptGenRandom` ile TXID + soru bölümünün eşleştiğinin
kontrolü.

## S9 – Güncelleme sistemi

Bugün: GitHub API'den `tag_name` okunuyor (`strstr` ile; 8 KB tampon, sınırlar doğru),
banner gösteriliyor, tıklanınca yetkisiz tarayıcıda releases sayfası açılıyor. Kod indirme
ve çalıştırma yok, yani **güvenli**.

Otomatik güncelleyici yapılırsa şunlar zorunlu:
1. **İmza:** Release varlıkları için ayrık imza (ör. minisign/Ed25519; açık anahtar exe'ye
   gömülü). Yalnızca TLS'e veya GitHub'a güvenmek yetmez, çünkü hesap ele geçirilebilir.
   SHA256SUMS dosyasının kendisi de imzalanmalı. Kod imzalama (Authenticode) gelirse ek
   kontrol olarak `WinVerifyTrust`.
2. **Konum:** İndirme yalnızca yönetici-only bir klasöre (`ProgramData\TurkishDPI\update`,
   `sys_secure_dir` ile). Çıkarma yine orada, yerine koyma Program Files'a. Kullanıcının
   yazabildiği bir yerden asla çalıştırma.
3. **Sürüm geri alma koruması:** Yalnızca daha yeni sürüme izin; imzalı manifestte sürüm
   numarası.
4. **Geri dönüş:** Eski exe `.bak` olarak tutulsun; yeni sürüm ilk açılışta selftest'in
   hafif bir sürümünü geçemezse geri dönülsün.
5. **Onay:** Varsayılan "bildir + tek tıkla kur"; sessiz otomatik kurulum yok.
6. Antivirüs etkisi: kendini güncelleyen yönetici süreçler sezgisel taramaları tetikler.
   İmzalı, ayrı bir "güncelleyici" modu (`--apply-update`) daha şeffaftır.

## S10 – Prob User-Agent'ı (Düşük)

`probe.c:62` `"Mozilla/5.0 (…) TurkishDPI-probe"`. Bu UA google, microsoft, discord,
roblox ve wattpad'e her bekçi turunda gidiyor. ISS göremez (TLS içinde), ama siteler
görebilir. Nötr bir UA ya da hiç UA kullanmak yeterli.

## S11 – Mutex işgali (Düşük)

`app.c:1257` `CreateMutexW(NULL, TRUE, L"Global\\TurkishDPI_app")`. Mutex varsa ve
pencere bulunamazsa süreç **mesajsız** çıkıyor. `Global\` ad alanında nesne oluşturmak
`SeCreateGlobalPrivilege` ister. Filtrelenmiş yönetici token'ının bu ayrıcalığı taşıyıp
taşımadığı **doğrulanmalı**. Öneri: bu durumda kullanıcıya mesaj göstermek; mutex'e açık bir
DACL vermek.

## S12 – Kaldırma (Düşük)

`sys_uninstall` `WinDivert`, `WinDivert14` ve `TurkishDPI` servislerini koşulsuz durdurup
siliyor. Kullanıcı ayrıca GoodbyeDPI veya zapret kullanıyorsa onlar da durur. Silinen servis
WinDivert sürücüsü tekrar açılınca yeniden oluşur, yani kalıcı zarar yok. Öneri: onay
metninde belirtmek. Kaldırmanın bağlantı izlemediği (reparse point) kontrolü doğru yapılmış.

## S13 – CI tedarik zinciri (Düşük-Orta)

- Üçüncü taraf Action'lar etikete bağlı (`softprops/action-gh-release@v2`, `actions/*@v4`).
  Commit SHA'sına sabitlenmeli.
- `permissions: contents: write` iş akışı düzeyinde; `test` ve PR `build` işleri yazma
  izni almamalı. Yazma yalnızca release adımında olmalı.
- Derleyici apt'tan sürümsüz kuruluyor. Tekrarlanabilir derleme ve
  `actions/attest-build-provenance` ile köken kanıtı eklenebilir. Bu, kullanıcıların
  ve antivirüs üreticilerinin ikilinin CI'dan çıktığını doğrulamasını da kolaylaştırır.

---

## İyi yapılmış olanlar (korunmalı)

- `sys_secure_dir`: tutamaç üzerinden DACL/sahiplik, reparse point reddi, başka
  kullanıcıların bıraktığı girdilerin silinmesi. TOCTOU'ya karşı doğru yaklaşım.
- Kaldırma ve temizlik reparse point takip etmiyor.
- `schtasks.exe`, `notepad.exe`, `explorer.exe` mutlak System32/Windows yollarıyla
  çağrılıyor. Sistem DLL'leri (uygulama tarafında) `LOAD_LIBRARY_SEARCH_SYSTEM32` ile
  yükleniyor.
- URL'ler `explorer.exe` üzerinden açılıyor; tarayıcı yükseltilmiş başlamıyor.
- Motor komut satırına giden ini değerleri (engel IP'leri) doğrulanıyor. Presetler sabit;
  kullanıcının girdiği site adları komut satırına gitmiyor.
- Telemetri yok; öğrenme tamamen yerel.
- Ödeme koruması varsayılan açık; kapatmak onay istiyor.
- Paket ayrıştırıcılarında (`exclude_learn_dns`, `dns_answer_is_forged`, `skip_name`)
  sınır kontrolleri doğru görünüyor; bozuk sıkıştırma işaretçisi testi var. Fuzz testi yok
  (öneri: libFuzzer/AFL ile `dns_answer_is_forged`, `exclude_learn_dns`, `extract_sni`).
- Antivirüs ayarlarına hiç dokunulmuyor; yardım metni kullanıcıyı yönlendiriyor.

## Önerilen sıra

1. S2'nin tek satırlık kısmı (`dnsredir.c` LoadLibrary) + S3 (bir satır). Riski düşük,
   kazancı hemen.
2. S1 güvenli kurulum + klasör güven kontrolü + `WinDivert.dll` hash doğrulaması.
3. S6 ölçümü, ardından motor düzeltmesi.
4. S5/S4: servis + yetkisiz tepsi mimarisi (büyük iş).
5. S13, S8, S10, S11, S12: küçük temizlikler.
