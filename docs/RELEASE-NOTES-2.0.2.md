## TurkishDPI 2.0.2

Bu sürüm yeni özellik getirmez; 2.0.1'deki hataları ve zayıf noktaları giderir: büyük ilk pakette motorun doğru çalışması, yerel adların (modem sayfası gibi) yeniden açılması, ödeme korumasının daha doğru karar vermesi, arayüzdeki bazı tutarsızlıklar ve güncelleyicinin daha sağlam olması.

**İndirme:** aşağıdaki `TurkishDPI-v2.0.2-x64.zip` (64 bit Windows, önerilen) ya da `TurkishDPI-v2.0.2-x86.zip` (32 bit). Zip'in tamamını bir klasöre çıkarın ve `TurkishDPI.exe`'yi çalıştırın. Dosyalar kod imzalı değildir; Windows SmartScreen "Windows bilgisayarınızı korudu" uyarısı gösterebilir. Bu durumda *Ek bilgi* → *Yine de çalıştır* deyin. İndirmenizi `SHA256SUMS.txt` ile doğrulayabilirsiniz (`certutil -hashfile <zip> SHA256`).

**2.0.1 kullanıyorsanız:** pencerede "Yeni sürüm hazır" yazar; **Güncelle →** düğmesi yeni sürümü indirip kendini değiştirir. Ayarlarınız ve öğrenilen bilgiler korunur.

### Düzeltmeler

**Motor**
- Ağ kartı paket bölmeyi donanıma bıraktığında motora tek bir büyük ilk paket (ör. yaklaşık 1,8 KB'lık bir TLS ClientHello) gelebilir. 2.0.1 bunu ya fazla büyük parçalarla gönderiyor ya da hiç değiştirmiyordu. Artık en çok 1200 baytlık parçalara bölünür. 1500 bayta kadar olan paketler 2.0.1'deki gibi gönderilir.
- Ad tabanlı profillerde ("Ad içinden bölme…" ve TLS kaydı profilleri), ilk pakette sitenin adı yoksa motor artık hiçbir şey yapmak yerine 2. bayttan sonra keser.
- Motorun gönderdiği sahte paketin kasıtlı olarak bozuk olan sağlama toplamı, rastlantıyla geçerli çıkamaz hale getirildi.
- `--exclude` ile verilen alan adı listesi, yerleşik listeden sonra yüklenir.

**DNS**
- Yerel bir DNS'ten (modem, VPN, bu bilgisayar) gelen özel ağ adresleri (`192.168.x.x`, `10.x.x.x`, `172.16–31.x.x`, `169.254.x.x`) artık sahte sayılmaz; modem sayfası, NAS ve şirket içi adlar yeniden açılır. Engel sayfası adresleri, `0.0.0.0`, loopback, çok noktaya yayın adresleri ve hata cevapları yine elenir. Operatörün CGNAT aralığındaki (`100.64.0.0/10`) bir DNS bu kolaylıktan yararlanmaz.

**Ödeme koruması**
- Karar artık bağlantıda görünen sunucu adına göre verilir. DNS'ten öğrenilen adres, yalnızca pakette sunucu adı yoksa ya da ad bilinen bir ECH genel adıysa (`cloudflare-ech.com`) kullanılır. Eskiden ödeme sitesiyle aynı CDN adresini paylaşan engelli bir site 2 saate kadar dokunulmadan kalabiliyordu.
- Kendi alan adı listeniz (`--exclude`) LF, CRLF ya da CR satır sonlarıyla, UTF-8 BOM'lu ve uzun yorum satırlı olabilir; geçersiz satırlar atlanır. Bir DNS cevabından en çok 16 adres öğrenilir; öğrenme tablosu dolunca en yakın zamanda süresi dolacak kayıt değiştirilir; yerleşik adlar her zaman yer bulur.

**Uygulama**
- TurkishDPI çalışırken `TurkishDPI.exe`'yi yeniden başlatmak çalışan pencereyi öne getirir.
- Duraklatılmışken bir ayarı (ör. ödeme koruması) değiştirmek korumayı kendiliğinden açmaz; yeni ayar devam edince uygulanır.
- Bir tarama sırasında yapılan site kontrolü, pencereyi "taranıyor" durumunda takılı bırakmaz. İnternet yokken yapılan site kontrolü çalışan motora dokunmaz.
- Sürücü hatalarında (87 hariç) program tek deneme yapar, başka profil denemez ve profillerin öğrenilmiş sırasını bozmaz. Bilinmeyen bir hata kodunda pencere kodu gösterir ve başka bir WinDivert aracını (GoodbyeDPI, zapret) kapatıp yeniden denemenizi söyler. Sürücü dosyası bulunamadığında metin, bu Windows'un ihtiyaç duyduğu sürücünün adını verir (`WinDivert64.sys` ya da `WinDivert32.sys`).
- Duraklat / Devam et düğmesine arka arkaya basmak artık zararsızdır.
- Çıkış ve *Kaldır ve temizle* pencereyi hemen gizler; işlem sürerken donmuş bir pencere görünmez.

**Güncelleme**
- İndirilen exe'nin dosya sürümü sürüm etiketiyle aynı ve çalışan sürümden yeni olmalıdır; aksi hâlde güncelleme yapılmaz.
- İndirme süresi sınırlıdır (özet dosyası için 30 saniye, zip için 3 dakika). Yönlendirmeleri program kendisi izler ve her adresi denetler.
- Günlükteki hata nedenleri daha ayrıntılıdır (ör. `download zip: HTTP 404`).
- Başarısız bir güncelleme çalışan exe'yi daha güvenilir biçimde geri koyar. Lisans ve BENIOKU metinleri yalnızca exe yerleştirildikten sonra yenilenir.
- Exe'nin yanındaki WinDivert dosyaları zip'tekilerle birebir aynı değilse otomatik güncelleme yapılmaz, sürüm sayfası açılır.
- Güncelleme yarıda kalır ve çalışan exe geri konamazsa program kapanır ve sürümler sayfasını açar; son sürümü oradan indirin.
- Açılışta yalnızca güncellemenin bıraktığı dosyalar (`TurkishDPI.exe.<sayı>.old` ve `….upd-new`) silinir.

**Derleme ve yayın**
- Sürüm exe'si daha küçüktür (hata ayıklama bilgisi atılmıştır).
- Sürüm, etiketle `version.h` uyuşmazsa derlenmez; yayın önce taslak olarak hazırlanır ve üç dosyası (iki zip, `SHA256SUMS.txt`) doğrulandıktan sonra yayımlanır. Yayımlanmış bir sürüm yeniden yazılmaz.
- Yayından önce paketteki exe'ler ve x64/x86 birim testleri CI'da çalıştırılır. Zip'teki metin dosyaları Windows satır sonlarıyla, `BENIOKU.txt` UTF-8 BOM'la gelir.

### Test

13 birim testi x64 ve x86'da geçti. Aşağıdakiler 14 Kasım 2026'da geliştiricinin kendi bilgisayarında (Windows 11, Türk Telekom hattı, Ethernet) yönetici olarak, bu sürümün yerel derlemeleriyle yapıldı; yayın exe'sini GitHub derler.

| Test | Sonuç |
|---|---|
| Kendi kendine test, x64 | Geçti: engelli 3 sitenin 3'ü açıldı (ortalama 164 ms), ödeme siteleri dokunulmadan geçti, motorun eklediği gecikme −1 ms |
| Kendi kendine test, x86 | Geçti: 3/3 (ortalama 109 ms), ödeme siteleri dokunulmadan geçti, gecikme farkı +2 ms |
| Tarayıcıyla siteler (Chromium 156) | Engelli üç site (discord.com, roblox.com, wattpad.com) ve engelsiz iki site (wikipedia.org, github.com) açıldı |
| İkinci açılış | Çalışan pencere öne geldi (tepsiden ve küçültülmüşken); `--tray` ile ikinci açılış sessiz kaldı |
| Duraklatma + ayar | Duraklatılmışken ödeme koruması değiştirildi: durum duraklatılmış kaldı, yeni ayar sonraki açılışta uygulandı |
| Çıkış | Pencere hemen gizlendi, program kapandı |
| Eksik sürücü dosyası | Tek deneme yapıldı, başka profil denenmedi, `WinDivert64.sys` adı geçen metin gösterildi, öğrenilmiş veriler değişmedi |
| Tek tıkla güncelleme | 1.9.9 etiketli bir derleme yaklaşık 2 saniyede yayımlanmış v2.0.1'e güncellendi; exe yayındakiyle birebir aynıydı |

Büyük ilk paket düzeltmesi bu bilgisayarda gerçek hatta denenemedi: tarayıcı testinde motora gelen en büyük ilk paket 1412 bayttı, yani tek pakette sığmayan bir paket görülmedi. Bu yol birim testleriyle ve bağımsız bir doğrulamayla sınandı.

### Bilinen sınırlar

- IP adresinden engellenen siteler (ve Türkiye'ye hizmet vermeyen siteler) VPN olmadan açılamaz; site kontrolü bunu söyler.
- Ev modemi, ISS'nin DNS cevaplarını olduğu gibi ilettiği için, bir ISS özel bir adresle sahte cevap verirse ve o adres engel listesinde değilse cevap geçer. Türkiye'deki engel sayfaları genel adreslerdir.
- Tarayıcı DoH kullanıyorsa ve ClientHello iki pakete bölünüyorsa ödeme koruması uygulanmayabilir (ölçülmedi).
- Dosyalar kod imzalı değildir. Güncelleme denetimleri bozuk ya da yolda değiştirilmiş indirmeyi yakalar; GitHub hesabının kendisinin ele geçirilmesine karşı koruma sağlamaz.
- ARM işlemcili Windows desteklenmez (WinDivert'in ARM64 sürücüsü yoktur). Windows hizmeti olarak değil, oturum açılışında görev olarak çalışır.
- WinDivert sürücüsü bazı antivirüslerde "WinDivert/RiskTool" olarak işaretlenir; bu, WinDivert kullanan her araçta görülen bir yanlış alarmdır.
- Windows 7/8.1, IPv6 ve diğer operatörler bu sürümde denenmedi.
- 2.0.0'dan gelenler güncelleme düğmesine sahip değildir; 2.0.2'yi elle indirin.
