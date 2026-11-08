# `--ui-preview` – teknik plan · TASLAK (kod yazılmadı)

Amaç: arayüzü yönetici izni olmadan, sahte `g_ui` verisiyle açmak ve her
durum × ölçek için otomatik ekran görüntüsü almak.

## 1. Komut satırı

```
TurkishDPI.exe --ui-preview <senaryo> [--scale 1|1.25|1.5|2] [--shot <dosya.bmp>]
```

- `--shot` yoksa: pencere açılır, elle denenir; kapatınca süreç biter.
- `--shot` varsa: pencere ekran dışında oluşturulur, bir kez çizilir,
  görüntü kaydedilir, süreç çıkar (kullanıcı hiçbir pencere görmez).
- `--help` çıktısına **eklenmez** (geliştirici aracı); CLAUDE.md'ye yazılır.

## 2. Kod değişiklikleri (küçük ve sınırlı)

| Dosya | Değişiklik |
|---|---|
| `app.c` `main()` | `--ui-preview` dalı, `app_main`'den **önce**: `return ui_preview_main(argc, argv);` (~4 satır). `sys_secure_dir`, ini, log, iş parçacıkları, WSAStartup hiç çalışmaz. |
| `app.h` | `int ui_preview_main(int argc, char **argv);` ve `extern int g_ui_preview;` |
| `ui.c` | `ui_preview_main`, senaryo tablosu (`preview_fill`), ekran görüntüsü, ayrıcalıklı yolların kapıları |

`g_lock` önizlemede `ui_preview_main` içinde `InitializeCriticalSection`
ile açılır (normalde `app_main` yapıyor).

## 3. Ayrıcalıklı / yan etkili her yolun kapatılması

Önizlemede `g_ui_preview = 1` ve:

| Yol | Normalde | Önizlemede |
|---|---|---|
| `click()` / `command()` → `post_cmd`, `post_site_test` | kuyruk + `SetEvent` | çağrılmaz; `site_result` "Önizleme: eylem çalıştırılmadı" |
| `set_payment_protect` | **ini yazar** | çağrılmaz; yalnız yerel `g_payment_protect` çevrilir |
| `app_uninstall`, `app_repair_proxy` | dosya/kayıt defteri | çağrılmaz (onay kutusu da açılmaz) |
| `app_open_file`, `app_open_url` | Notepad / tarayıcı | çağrılmaz |
| `app_exit` (Çıkış, `WM_ENDSESSION`) | denetleyiciyi durdurur, motor, DNS önbelleği | `PostQuitMessage(0)` |
| `app_network_event` (`WM_POWERBROADCAST`) | olay | çağrılmaz |
| `autostart_enabled` | yalnız önbellek okur | senaryodan gelen değer |
| Tepsi simgesi (`tray_add`) | Shell_NotifyIcon | eklenmez (`--tray-preview` ileride) |
| `ChangeWindowMessageFilterEx` | UIPI | atlanır |
| Pencere sınıfı | `TurkishDPI_main` | **`TurkishDPI_preview`** — aksi hâlde çalışan gerçek uygulamanın `FindWindowW` tek-örnek kontrolü önizleme penceresini bulup ona mesaj yollar |

Kapı tek yerde: `command()` ve `click()` başında
`if (g_ui_preview) { preview_action(id); return; }`. Böylece yeni bir
ayrıcalıklı menü öğesi eklense bile önizlemede varsayılan olarak çalışmaz.

**Doğrulama:** (1) kod incelemesi: `ui_preview_main`'den ulaşılabilen her
çağrı listelenir ve rapora yazılır; (2) `__COMPAT_LAYER=RunAsInvoker` ile
çalıştırılır — yönetici izni gerektiren bir şey denense başarısız olurdu;
(3) çalıştırma öncesi/sonrası `%ProgramData%\TurkishDPI` dosyalarının
değişiklik zamanı ve `schtasks /Query /TN TurkishDPI` karşılaştırılır.

## 4. Senaryolar

`active`, `active-long` (uzun ağ adı, uzun profil, 8 site), `partial`,
`scanning`, `first-run` (learned=0, scan 3/10), `standby`, `paused`,
`offline`, `error-driver` (driver_err=5), `error-av` (driver_err=1275),
`error-noprofile`, `banner-update`, `banner-proxy`, `banner-rival`,
`banner-multi` (proxy + güncelleme + çakışma), `manual` (g_auto=0),
`payment-off`. Ayrıca sekme seçimi: `--page status|details|settings`.

Her sayı senaryoda açıkça yazılır; senaryolar yalnız önizleme içindir,
gerçek modda hiçbir yerde kullanılmaz.

## 5. Ekran görüntüsü

- Ölçek: `--scale` verilirse `g_scale` pencere DPI'ı yerine bu değerden
  alınır (yalnız önizlemede). Böylece sistem DPI'ını değiştirmeden
  %100/125/150/200 alınır.
- Yakalama: pencere `(-32000, -32000)` konumunda görünür (gösterilmeden
  `WS_EX_TOOLWINDOW`), `UpdateWindow` → `PrintWindow(hwnd, dc,
  PW_CLIENTONLY | PW_RENDERFULLCONTENT)` → 32 bit DIB → elle yazılan BMP
  başlığı (GDI+ kodlayıcı kullanılmaz). Native EDIT ve sahip-çizimli
  düğmeler de görüntüye girer.
- `tools/ui_shots.ps1`: senaryo × ölçek × sayfa döngüsü,
  `$env:__COMPAT_LAYER='RunAsInvoker'`, ardından BMP → PNG
  (`python -I tools/bmp_to_png.py`, Pillow) ve
  `docs/design/screenshots/<senaryo>@<ölçek>.png`. PNG'ler yalnız belgelerde;
  exe'ye girmez.
- İlk iş: **bugünkü** arayüzün görüntülerini almak (önce/sonra karşılaştırması).

## 6. Bellek ölçümü

`tools/ui_shots.ps1 -Measure`: önizleme penceresi açıkken ve gizlendikten
sonra `Get-Process` → `WorkingSet64`, `PrivateMemorySize64`, ve GDI/USER
nesne sayıları (`GetGuiResources`, önizlemede `--stats` ile log'a yazılır).
Gerçek uygulamanın kapalı-pencere ölçümü yönetici izni gerektirir; onu
yaparken sana sorulacak.

## 7. Maliyet

`ui.c` ~+250 satır (senaryolar dahil), `app.c` ~4 satır, `app.h` 2 satır,
2 küçük betik. Çalışma zamanına etkisi: normal modda yalnız bir `if`.
