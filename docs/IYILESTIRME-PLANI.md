# TurkishDPI iyileştirme planı (v2.0.1 sonrası)

Tarih: 2026-11-14. Kapsam: projenin tamamı (motor, denetleyici, arayüz, güncelleyici, derleme/CI, testler, belgeler).

Bu plan altı bağımsız denetimin sonucudur. Denetimler yalnızca okuma yaptı; bu belge dışında hiçbir dosya değişmedi.

## 1. Özet

- **Kritik güvenlik açığı bulunmadı.** Güncelleyicinin tasarımı ve zip okuyucu sağlam bulundu.
- **Kullanıcının karşılaşabileceği gerçek hatalar var.** En önemlileri aşağıdaki tabloda; dokuzunu kodda kendim doğruladım.
- **"%100 optimize" ölçülebilir hedeflere çevrildi** (bölüm 3). Ölçülemeyen bir hedefe ulaşıldığı söylenemez.
- **İş üç sürüme bölündü:** v2.0.2 (hatalar), v2.1.0 (hız ve sağlamlık), v2.2.0 (diğer sağlayıcılar için yeni teknikler).
- **Kodun tamamını Opus 5.5 ve Sonnet 5.5 ajanları yazacak.** Ben iş paketlerini tanımlıyor, sonuçları doğruluyor, birleştiriyor ve ölçüyorum.

## 2. En önemli bulgular

"Doğrulandı" sütunu: bulguyu ilgili kod satırlarında kendim kontrol ettim mi.

| # | Bulgu | Etki | Doğrulandı |
|---|---|---|---|
| 1 | Yaklaşık 1550 bayttan büyük ClientHello paketleri hiç bölünmüyor, olduğu gibi gidiyor (`engine.c`, `tcpdpi.c` 1600 bayt sınırı). | Ağ kartında "large send offload" açıksa Chrome/Edge'in ~1,8 KB'lik el sıkışması tek paket gelir ve önerilen profil sessizce etkisiz kalır. Arkadaşının yaşadığı sorunun adayı. | Kod evet; paketin gerçekten tek parça gelip gelmediği **ölçülmedi** |
| 2 | Hibrit DNS, yerel çözücünün özel adresli cevaplarını (192.168.x, 10.x) sahte sayıp atıyor (`dns.c` `bad_v4`). | Modem arayüzü, NAS, şirket VPN adları çözülmeyebilir. | Evet |
| 3 | Ödeme koruması IP eşleşmesine site adından önce bakıyor (`engine.c` `excluded_conn`). | Ödeme sitesiyle aynı CDN adresini paylaşan engelli bir site 2 saate kadar açılmaz. | Evet |
| 4 | Uygulama tepsideyken exe ikinci kez çalıştırılınca pencere öne gelmiyor (`app.c:1796`, `ui.c` `tray_click_genuine`). | Kullanıcı "program açılmıyor" sanır. | Evet |
| 5 | Duraklatılmışken ödeme koruması ayarı değişirse koruma kendiliğinden açılıyor (`CMD_RESTART` → `CMD_RESUME`). | Duraklatma kaybolur. | Evet |
| 6 | Tarama sırasında yapılan site kontrolü "internet yok" ile biterse tarama sürdürülmüyor. | Pencere 10 dakikaya kadar "taranıyor" der, motor aday profilde kalır. | Evet |
| 7 | Tanınmayan sürücü hata kodlarında 10 profilin hepsi boşuna deneniyor ve hepsi "başarısız" diye kaydediliyor. | Öğrenilen sıralama bozulur. | Evet |
| 8 | Site adı ilk pakette değilse "ad içinden bölme" ve TLS kaydı profilleri hiçbir şey yapmıyor. | Superonline için düşünülen profiller Chrome'da yarı yarıya etkisiz olabilir. | Evet |
| 9 | Önerilen profil (sahte paket yok) her bağlantının SYN-ACK'ini ve TLS uyarılarını yine de kullanıcı moduna çekiyor. | Her bağlantıda gereksiz iki bağlam geçişi ve el sıkışmada gecikme. | Evet |
| 10 | Site testleri iptal edilemiyor; "Duraklat" ve "Çıkış" süren testin bitmesini bekliyor (10–20 sn olabilir). | Arayüz yanıt vermiyor gibi görünür. | Rapor |
| 11 | Bilinen ağda başlangıç her şeyi sırayla yapıyor (görev kaydı, DNS ölçümü, kontrol turu). | Koruma gereğinden geç başlar. | Rapor |
| 12 | Sürüm etiketi ile `version.h` uyuşmazsa CI durdurmuyor; sürüm yayını atomik değil. | Yanlış etiketli bir sürüm herkesi sonsuz güncelleme döngüsüne sokar. | Rapor |
| 13 | Yayımlanan exe CI'da hiç çalıştırılmıyor, 32 bit hiç test edilmiyor; yerel derleme (ucrt) ile yayın (msvcrt) farklı. | Test edilen ile dağıtılan aynı dosya değil. | Rapor |
| 14 | Güncelleme yarıda kalırsa kurtarma zayıf; indirmenin toplam süre sınırı yok; yeni exe'nin sürümü etiketle karşılaştırılmıyor. | Nadir ama kötü sonuçlu hata yolları. | Rapor |
| 15 | Yayımlanan exe 2,16 MB; 0,73 MB'ı sıfır dolu veri, 0,5 MB'ı hata ayıklama bilgisi. README "1 MB'tan küçük" diyor. | Gereksiz boyut, yanlış belge. | Rapor |

Diğer bulguların tamamı bölüm 5'teki iş paketlerine dağıtıldı (toplam yaklaşık 95 bulgu).

## 3. Ölçülebilir hedefler

| Ölçü | Bugün | Hedef | Nasıl ölçülecek |
|---|---|---|---|
| Bilinen ağda "başlatıldı → koruma açık" süresi | ~3 sn (günlük) | 1,5 sn veya altı | Günlükteki `starting` ve `decision` satırları |
| "Duraklat" / "Çıkış" yanıt süresi (tarama sırasında) | 10–20 sn'ye kadar | 1 sn'nin altı | Günlük zaman damgaları |
| Önerilen profilde bağlantı başına yakalanan paket | ClientHello + SYN-ACK + uyarılar | Yalnız ClientHello | `STATS packets` / bağlantı sayısı |
| Motorun eklediği gecikme | 0–6 ms | Aynı ya da daha iyi | `--selftest` |
| 1,8 KB'lik ClientHello'nun bölünmesi | Bölünmüyor (kod) | Her zaman bölünür | Birim testi + paket yakalama |
| Yayın exe boyutu | 2,16 MB | ~1,2 MB (semboller duruyor, paketleme yok) | CI çıktısı |
| Derleyici uyarısı | 0 (`-Wall -Wextra`) | 0, `-Werror` ile ve daha sıkı uyarılarla | CI |
| Sanitizer altında çalışan ayrıştırıcılar | 6 dosya | Bütün ayrıştırıcılar + fuzz | CI |
| Yayımlanan exe'nin CI'da çalıştırılması | Yok | x64 ve x86, her derlemede | CI "smoke" işi |

Boşta kaynak kullanımı zaten düşük (uygulama 16 MB bellek, motor 3 MB); bunun için hedef "kötüleşmesin".

## 4. Çalışma düzeni

**Roller**
- **Fable 5.2 (organizatör):** İş paketlerini ve kabul ölçütlerini yazar, ajanların iddialarını kodda doğrular, dalları birleştirir, ölçüm yapar, sürüm kararını sana sorar. Kod yazmaz.
- **Opus 5.5:** Motor, güvenlik, eşzamanlılık ve durum makinesi, riskli yeniden düzenlemeler.
- **Sonnet 5.5:** CI, testler, belgeler, arayüz cilası, mekanik işler.

**Her iş paketi için kurallar**
1. Kendi dalı ve kendi çalışma kopyası; iki ajan aynı dosyaya aynı anda dokunmaz.
2. Önce hatayı gösteren test yazılır, sonra düzeltme.
3. x64 ve x86 uyarısız derlenir; bütün birim testleri ASan/UBSan altında geçer.
4. Yazan ajandan farklı bir Opus ajanı değişikliği bağımsız inceler. İncelemenin bulgularını ben doğrular, gerekirse geri gönderirim.
5. GoodbyeDPI'dan kod, seçenek adı ya da bayt tablosu alınmaz; yalnızca teknik fikir. Shroudly kodu alınırsa MIT atfı eklenir.

**Her sürümün sonunda (senin UAC onayınla)**
- `--selftest` x64 ve x86, arayüz uçtan uca testi, gerçek güncelleme testi (bir önceki sürümden yenisine).
- Bölüm 3'teki tablonun yeniden ölçülmesi.
- VirusTotal taraması. Sonuç kötüleşirse sürüm bekler.
- Yayın için senin onayın.

## 5. İş paketleri

### Aşama 0 — Hazırlık

| İş | Kim | Not |
|---|---|---|
| llvm-mingw derleyicisini yeniden kur | Ben, senin onayınla | Geçici klasördeki kopya silinmiş. İndirme için izin isteyeceğim. |
| `gh auth login` | Sen | PR ve sürüm için gerekli; `git push` çalışıyor. |
| Depo kökündeki dosyalar | Sen karar ver | `BENIOKU.txt`, `WinDivert.dll`, `WinDivert64.sys`, `WinDivert-LICENSE.txt` kökte duruyor (bir zip buraya çıkarılmış). Silmeyeceğim; `.gitignore`'a eklenecek. |
| Birleşmiş 6 dal ve `.worktrees/ui-redesign` | Sen karar ver | Silinmeleri önerilir; etiketler kalır. |
| Arkadaşının günlüğü | Sen | v2.0.1'de *Ayarlar → Günlüğü aç* dosyasını gönderirse bulgu 1 ve 8'i gerçek veriyle sınarım. |
| 1 numaralı bulgunun ölçümü | Ben + Opus | Motora "en büyük ClientHello boyutu" sayacı eklenir, Chrome ile bir kez ölçülür. Sonuç E1'in önceliğini belirler. |

### Aşama 1 — v2.0.2: doğruluk

Hedef: kullanıcının karşılaşabileceği hataları kapatmak. Yeni özellik yok.

| Paket | Model | Dosyalar | İçerik | Kabul |
|---|---|---|---|---|
| **E1** Motor doğruluğu | Opus | `engine.c`, `core/tcpdpi.c`, testler | Büyük ClientHello'yu MTU'ya sığan N parçaya böl (ters sıra korunur); ad ilk pakette değilse sabit erken kesime düş; ödeme korumasında ad varsa ad karar versin; süzgeç metninde taşma koruması | 1800 / 4000 / 64000 baytlık girdilerde parçalar MTU'yu aşmaz, sıra numaraları kesintisiz; `--selftest` gecikmesi değişmez |
| **E2** Hibrit DNS | Opus | `dns.c`, `dns_test.c` | Sorulan çözücünün özel adresli cevabı geçsin; temiz çözücünün hata cevabı yarışı tek başına bitirmesin | Yeni test durumları; mevcut durumların hepsi geçer |
| **E3** Ödeme listesi testleri | Sonnet | `tests/exclude_test.c` | `exclude_learn_dns`, `exclude_check_ip` için ilk testler (CNAME zinciri, süre dolumu, tablo taşması) | ASan altında geçer |
| **A1** Denetleyici ve arayüz hataları | Opus | `app.c`, `ui.c` | İkinci açılışta pencereyi öne getiren özel mesaj; duraklatmanın korunması; site kontrolünden sonra taramanın sürmesi; tanınmayan sürücü hatasında tek deneme ve kayıt yok; duraklat/devam komutlarının çift tıklamaya dayanıklı olması; çıkışta pencerenin hemen gizlenmesi | Her biri için günlükte gösterilebilir senaryo; arayüz önizlemeleri değişmez |
| **S1** Güncelleyici sağlamlığı | Opus | `system.c`, `updpkg.c`, testler | Yarıda kalan güncellemede eski exe'yi her durumda geri koy; indirme için toplam süre sınırı; yeni exe'nin sürümü etiketle ve çalışan sürümle karşılaştırılsın; temizlik yalnız kendi dosya adlarımızı silsin; yol birleştirme için tek yardımcı; etiket ayrıştırmada taşma | Hata yollarının her biri için test; gerçek güncelleme testi geçer |
| **C1** Sürüm güvenliği ve CI | Sonnet | `build.yml`, `Makefile`, `.gitignore` | Etiket = `version.h` denetimi; sürüm taslak olarak oluşsun, doğrulandıktan sonra yayımlansın; yayımlanan exe'yi çalıştıran "smoke" işi (x64 ve x86); 32 bit birim testleri; `dns.c` ve `exclude.c` için ASan; testlerde `-Werror`; exe boyutu (`-Wl,-S`, sıfır dolu `.data` incelemesi); zip'teki metin dosyalarında Windows satır sonu | CI yeşil; exe ~1,2 MB; semboller duruyor |
| **D1** Belgeler | Sonnet | `README.md`, `docs/`, `NOTICE` | Kopyalanınca bozulan ini örneği; eski ve yanlış ifadeler; 2.0 öncesi belgelere "tarihsel" notu; SHA-256 doğrulama, hata kodları tablosu, elle kaldırma bölümleri; `CLAUDE.md`'deki kodla çelişen ifadeler | Her ifade kodla karşılaştırılmış |

Sıra: E1, E2, E3, C1, D1 paralel. A1 ve S1 ayrı dosyalarda paralel; S1'in `app.c`'ye dokunan kısmı A1 birleştikten sonra.

### Aşama 2 — v2.1.0: hız ve sağlamlık

| Paket | Model | İçerik |
|---|---|---|
| **R1** `app.c`'yi böl | Opus | Davranış değişmeden `engine_proc.c`, `autostart.c`, `config.c`, `update.c`, `selftest.c`, `log.c`. Sonraki paketlerin paralel çalışabilmesi için ilk iş. |
| **A2** İptal edilebilir site testleri | Opus | Komut ya da çıkış gelince süren WinHTTP isteği kapatılır; tur başına süre sınırı; zaman aşımından sonra yeniden deneme yok. |
| **A3** Hızlı başlangıç | Opus | Bilinen ağda öğrenilmiş profil ve DNS ile hemen başla, tek turla doğrula; DNS ölçümü ve görev kaydı sonra; eski hizmet uyarısı sessiz başlangıcı bekletmesin. |
| **A4** Motor işleminin başlatılması | Opus | İşlem doğrudan iş nesnesinin içinde ve yalnız gereken tutamaçla oluşturulsun (Windows 7 için bugünkü yol yedek); exe'nin çözülmüş gerçek yolu kullanılsın ve başlatılan görüntü doğrulansın; motor düzgün kapansın. |
| **L1** Öğrenme | Opus | Saf ve testli modül: zamanla eskiyen kayıtlar, hıza duyarlı sıralama, bozuk ini değerlerine dayanıklılık, kararlı ağ kimliği, eski ağ bölümlerinin budanması, yalnız değişen değerin yazılması. |
| **P1** Yoklama | Sonnet | DNS süresi ölçümünde deneme başına kimlik; engel sayfası öğrenmenin paralelleşmesi; ağdan gelen DNS baytlarını işleyen kod için testler; Türkçe karakterli alan adları. |
| **E4** TLS kaydı profilinin sıcak yolu | Opus | Artımlı sağlama toplamı, çevirim tablosu için karma tablo, SACK bloklarının çevrilmesi, kısa boşta kalma süresi. |
| **E5** Motor karar çekirdeği | Opus | IPv4/IPv6 ortak görünüm ve saf karar işlevi; önerilen profilde gereksiz SYN-ACK/uyarı yakalamanın kaldırılması; IPv6 için sahte RST ayrımı ve eksik korumalar; `--check-args` ile CI'da her profilin denetlenmesi. |
| **U1** Arayüz performansı ve küçük UX | Sonnet | Değişmeyen durumda yeniden çizim ve tepsi çağrısı yok; Esc; şerit önceliği; işlem sürerken geri bildirim; pencere çalışma alanına sığsın. |
| **U2** Erişilebilirlik | Sonnet (tepsi kısmı Opus) | Anahtarların gerçek açık/kapalı durumu; şerit eyleminin adı; yüksek karşıtlık; tepsi simgesine klavyeyle erişim (güvenlik yüzeyi olduğu için ayrı inceleme). |
| **T1** Test altyapısı | Sonnet | Ayrıştırıcılar için fuzz, CodeQL/cppcheck, derleme kanıtı (attestation), yinelenebilir derleme. |
| **T2** Tekrarlanabilir uçtan uca testler | Sonnet | Güncelleme testini `tools/` altına al (`make VERSION=…`), testler hata kodu döndürsün. |

### Aşama 3 — v2.2.0: diğer sağlayıcılar

Bu aşamadaki teknikler yalnız Türk Telekom hattında denenebilir. Diğer hatlarda işe yarayıp yaramadığını kullanıcı raporlarından öğreneceğiz; bu yüzden ilk iş rapor aracı.

| Paket | Model | İçerik |
|---|---|---|
| **F1** Tanı raporu | Sonnet | Kullanıcının GitHub'a yapıştırabileceği anonim metin: seçtiği sağlayıcı, profil başına sonuç, el sıkışmanın nasıl bittiği. IP adresi içermez. |
| **F2** TLS kaydı çeşitleri ve çoklu kesim | Opus | Kesim konumu ve sırası seçilebilir; ikiden fazla kesim; test içinde "akışı birleştiren filtre" taklidi. Superonline için. |
| **F3** İki pakete yayılan ClientHello | Opus | İlk parçayı tutup tamamını birlikte işle. Süzgeci genişlettiği için en dikkatli yapılacak iş. |
| **F4** Sahte paket seçenekleri | Opus | Bozuk sağlama toplamı yerine seçilebilir yöntemler (bazı modemler bozuk paketi düşürüyor); sahte RST ayrımında SYN-ACK TTL'i. |
| **F5** Bozulan siteler için otomatik atlama | Sonnet | Bölme yüzünden açılmayan engelsiz siteleri öğren ve dokunma. |

### Yapılmayacaklar

- Windows hizmeti, ARM64, Linux: mevcut tasarım kararları geçerli.
- Kod imzalama: sertifika gerektirir, senin kararın.
- Tor, kendi sunucu, reklam engelleme (Shroudly'de var): kapsam dışı ve yeni saldırı yüzeyi.
- IP parçalama, SYN verisi gibi kırılgan teknikler: Türkiye için kanıt yok.
- Antivirüs tespitinden kaçmak için paketleme ya da gizleme: yasak.

## 6. Riskler ve bilinmeyenler

- **1 numaralı bulgunun ön koşulu ölçülmedi.** Kod büyük paketi bölmüyor; ancak paketin motora gerçekten tek parça gelip gelmediği ağ kartına bağlı. Senin makinende engelli siteler açıldığına göre her kurulumda görülmüyor olabilir. Aşama 0'daki ölçüm bunu netleştirir.
- **E1 önerilen profilin çekirdeğine dokunuyor.** Türk Telekom'daki 20/20 ölçüm tekrarlanmadan yayımlanmaz.
- **Aşama 3 teknikleri diğer hatlarda denenemiyor.** Ölçülmemiş profiller, ölçülmüş olanlardan sonra sıralanır ve öyle etiketlenir.
- **Rapor sütunundaki bulgular** ajanlar tarafından satır numarasıyla verildi ama tek tek tarafımdan doğrulanmadı. Her paket önce bulguyu testle gösterecek; gösteremezse bulgu düşer.
- **Maliyet:** yaklaşık 25 iş paketi ve her biri için bir inceleme, toplam 50 civarı ajan çalışması. Aşama 1 yedi paket ve yedi incelemedir.

## 7. Kaynaklar

Denetim raporları (bu oturumda): motor (Opus), denetleyici (Opus), güvenlik ve güncelleyici (Opus), arayüz (Sonnet), derleme/CI/test/belgeler (Sonnet), Shroudly / GoodbyeDPI / zapret karşılaştırması (Sonnet).

Karşılaştırmanın temel sonucu: Shroudly'nin Windows paket çekirdeği zaten TurkishDPI'da; almadığımız kısımlar onun vekil sunucu katmanında. GoodbyeDPI-Turkey'in Türk Telekom ve Superonline için önerdiği temel yöntem (ilk isteği 2. bayttan bölüp ters sırada göndermek) TurkishDPI'ın ölçülmüş önerilen profiliyle aynı. Vodafone, TurkNet ve mobil hatlar için dört kaynakta da veri yok.
