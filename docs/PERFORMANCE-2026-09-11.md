# 8bitShell performans doğrulaması

## Sonuç

Güvenlik sıkılaştırmasını izleyen performans çalışmasında bağlantı ağacı, terminal
çizimi, arama ve görünmeyen panellerin gereksiz işi azaltılmıştır. Güvenlik kontrolleri,
terminal çıktısı veya log yazma kapatılmamıştır. Yeni sürüm için bir “güvensiz/hızlı”
mod seçmek gerekmez.

Aşağıdaki sonuçlar aynı Nobara 44 / Qt 6.11.2 ortamında, Release derlemesiyle çalıştırılan
yerel sentetik iş yüküne aittir. Başka istemcilerle karşılaştırma veya gerçek kurum ağı
ölçümü değildir. Çizim sayıları yalnız CPU tarafındaki paint maliyetidir; uçtan uca FPS,
GPU texture upload süresi veya SSH gecikmesi olarak yorumlanmamalıdır.

## Önce / sonra ölçümü

Benchmark 1000×600 piksellik terminal, 121 sütun/30 görünür satır, başlangıçta 2.000
satırlık geçmiş, ANSI renkleri ve açık syntax highlighting kullanır. Katalogda 5.000
session, 500 alt klasör ve bir kök klasör vardır. Her ölçü üç örnek grubunun medyanıdır.
“Sonra” sütunu iki ayrı son çalıştırmadaki medyanların aralığıdır; yalnız en iyi sonuç
seçilmemiştir.

| İşlem | Önce, ms/işlem | Sonra, ms/işlem | Yorum |
| --- | ---: | ---: | --- |
| Değişmeyen terminali tekrar çizme | 3,682 | 0,117–0,121 | Yaklaşık 30–31 kat daha düşük çizim maliyeti |
| Tek hücre değişikliği + çizim | 3,655 | 0,241–0,247 | Yalnız hasarlı satır yenileniyor |
| 2.000 satırda arama sorgusu değiştirme | 6,370 | 3,779–3,855 | Yeni sorgu hâlâ geçmişi tarar |
| Arama işaretli terminali tekrar çizme | 9,996 | 0,116–0,119 | Önbellek ve sıralı match araması |
| Arama açıkken yeni satır işleme | 9,497 | 0,112–0,116 | Değişmeyen geçmiş yeniden ayrıştırılmıyor |
| Yaklaşık 4 KiB çıktı bloğunu işleme | 0,191 | 0,226–0,231 | Bu dar parser adımında kazanç gösterilmedi |
| Kapalı katalog ağacını tekrar alma, sıcak indeks | 2,870 | 0,00066 | İlk indeks oluşturma hariç; UI render süresi değil |
| Katalog kökünü açma, sıcak indeks | 3,162 | 0,345–0,353 | Sıralama/sayılar tekrar hesaplanmıyor |
| Bütün alt klasörleri eşleştiren katalog araması | 420,964 | 9,919–10,006 | Yaklaşık 42 kat daha düşük model maliyeti |
| Seçici katalog araması | 5,527 | 1,696–1,708 | Normalize edilmiş arama metni tekrar kullanılıyor |

20 görünmeyen terminalin 1,2 saniyedeki imleç zamanlayıcı tetiklenmesi **40'tan 0'a**
inmiştir. Bu sayı yalnız imleç zamanlayıcılarıdır; SSH keepalive, gerçek çıktı veya
dosya aktarımının durduğu anlamına gelmez.

Tüm benchmark işleminin kullanıcı+sistem CPU süresi, dışarıdan ölçümde 3,13 saniyeden
0,52 saniyeye düşmüştür: bu iş yükünde yaklaşık %83 daha az CPU süresi.
Uygulamanın her kullanımında veya bütün bilgisayarda aynı yüzde vaat edilmez. Ham parser
adımındaki daha kötü/oynak süreler tabloda korunmuştur. Ağ, kriptografi, yoğun tam ekran
yenileme ve disk performansı için genel hız artışı iddiası yoktur.

### Bellek karşılığı

CPU tasarrufu sınırsız RAM önbelleğiyle sağlanmamıştır. Görünür terminalin raster
önbelleği en fazla **16 MiB**'dır; gizlenince/minimize edilince bırakılır. Bu bütçeyi aşan
boyut/DPI kombinasyonları görüntü kalitesini düşürmeden doğrudan çizime döner. Katalog
indeksi yalnız mevcut revision'a aittir; her sorgunun sonucunu saklayan büyüyen bir
sonuç önbelleği yoktur.

Benchmark'in dışarıdan ölçülen en yüksek RSS değeri 135.632 KiB'den 138.276–138.364
KiB'ye çıkmıştır: yaklaşık **2,6–2,7 MiB ek bellek** karşılığında CPU/latency kazancı.
Bu, Qt/YAML/terminal geçmişi dahil bütün fixture sürecinin high-water değeridir; tek
önbelleğin veya gerçek 20 SSH sekmesinin bellek ölçümü değildir. RSS'nin düştüğü iddia
edilmez. Mevcut scrollback sınırları korunur; çok sayıda yoğun oturum toplam belleği artırır.

## Uygulanan değişiklikler

**Bağlantı ağacı.** SessionStore klasör hiyerarşisini, doğal sıralamayı, alt oturum
sayılarını ve case-folded arama metnini katalog değiştiğinde hazırlar. Aç/kapa ve filtre
işlemleri bu indeksi kullanır. Eski geniş klasör filtresi, eşleşen her klasör için bütün
klasörleri ve session'ları yeniden tarıyordu. Tam klasör yolunun ancestor metnini zaten
içerdiği kullanılarak bu iç içe tarama kaldırılmıştır. Edit, taşıma, import, reload ve
model sinyalleri indeksi geçersiz kılar; reddedilen reload mevcut modeli korur.

**Terminal çizimi.** Metin/rengin raster sonucu imleçten ayrı tutulur. İmleç yanıp
sönerken regex ve hücre çizimi yeniden yapılmaz. libvterm hasar bildirimleriyle küçük
değişikliklerde yalnız ilgili satırlar yenilenir. Unicode/combining karakterler korunur;
font nesneleri hücre başına yeniden oluşturulmaz. Gizli veya aktif olmayan terminallerin
imleç animasyonu durur. Kuyruğa önceden girmiş son bir paint çağrısı da gizli terminalin
önbelleğini yeniden oluşturamaz.

**Terminal araması.** Scrollback'in değişmeyen bölümündeki eşleşmeler korunur; yeni
geçmiş satırları ve canlı ekran taranır. Eviction, resize ve alternate screen durumları
işlenir. Hücreyi boyarken bütün eşleşmeleri sırayla aramak yerine binary search yapılır.
Yerinde kaldığı sürece seçili arama sonucu yeni çıktı geldiğinde korunur.

**Bildirimler ve dosya paneli.** Scrollback bildirimi her satırda değil, çıktı bloğunun
sonunda gerekli olduğunda gönderilir. Yerel dizin watcher olayları 75 ms pencerede
birleştirilir; uygulamanın kendi kopyası sürerken tekrar tekrar tarama yapılmaz. Gizli
dosya panelinin yerel watcher'ı kapanır, görünür olduğunda liste yenilenir. Kopyalama
veya devam eden SFTP aktarımı panel gizlendiği için kesilmez.

## Güvenlik ve doğruluk

Bu değişiklikte SshSecurity.cpp, PrivateFile.cpp, YamlFile.cpp, SessionLogger.cpp,
SftpController.cpp, PtyProcess.cpp ve BoundedRegex.h dosyalarının SHA-256 değerleri
güvenlik referansıyla aynıdır. SSH doğrulaması, algoritma politikası, nofollow/no-replace,
log flush/rotasyonu, parser/regex sınırları ve PTY okuma bütçesi gevşetilmemiştir.
Terminal güvenlik onayları da UI ve gerçek PTY testleriyle yeniden kontrol edilmiştir.

Debug, Release ve ASan+UBSan+leak-detection yapılandırmalarının her birinde **16/16 CTest
hedefi**, sıfır başarısız ve sıfır atlanan testle geçmiştir. Tam kayıtlarda Qt uyarısı,
sanitizer/runtime-error çıktısı veya kesilmiş test çıktısı yoktur. Sistem Qt/libvterm/
OpenSSH kütüphaneleri instrument edilerek yeniden derlenmiş değildir; sanitizer kapsamı
uygulama ve bundled YAML kodudur.

Ek regresyon testleri:

- Önbellekli çizimin sıfırdan çizilen görüntüyle piksel eşitliği: 1×, 1,25×, 2× DPI;
  ANSI, Türkçe, geniş karakter, combining mark, emoji, seçim, arama, font ve tema değişimi.
- 200 deterministik çıktı/erase/alternate-screen/resize/scrollback-eviction adımında
  incremental arama sonucunun tam taramayla eşitliği.
- 16 MiB cache bütçesi, gizlenince bellek bırakma ve zamanlayıcının durması.
- Hızlı karakter dönüşümünün surrogate/geçersiz Unicode değerlerinde önceki doğrulama
  davranışını koruması.
- Büyük çıktı bloğunda toplu bildirim ve eksiksiz arama sonucu.
- Sıcak katalog indeksinden sonra taşıma/edit ve reddedilmiş reload doğruluğu.
- 200 dosya değişiminin birleştirilmesi; gizli panelin tekrar açılışında güncellik.
- **20 eşzamanlı gerçek yerel PTY**, her birinde 600 satır ve bitiş işareti: gizli
  terminalin logu beklenen byte dizisiyle birebir eşit, bitiş satırı aranabilir.

Geliştirme sırasında yeni window/focus bağlantılarının Qt base-destructor aşamasında
yaşayan bir TerminalView varmış gibi çağrılması sanitizer tarafından yakalanmış ve
bağlantılar derived-destructor başında kesilerek düzeltilmiştir. Hata suppressions ile
gizlenmemiştir; kapanış/gizleme testleri suite'te kalmaktadır.

Son release dosyasının sekiz ELF güvenlik kontrolü de geçmiştir. Binary SHA-256:

```text
35b01ed1f8d51e5dd036d6b54eb1b7b9e817afa47e634c0d332aa66551c663cd
```

20-PTY testi gerçek 20 kurum SSH bağlantısı değildir. Mevcut gerçek SSH/SFTP trust
testleri ayrı olarak geçmiştir; uzak WAN/MFA/dosya boyutu matrisi yerine geçmez.
Başlangıç UI ve güvenlik pencereleri offscreen Qt ile görsel olarak kontrol edilmiştir;
native Plasma/Wayland ve gerçek günlük iş yükü pilotu halen gereklidir.

## Tekrar çalıştırma ve kanıtlar

Benchmark native test erişimi kullanır; uygulamaya QML/CLI/environment üzerinden ham
terminal girdisi veya farklı transport seçme kapısı açmaz. Çizimi izole ölçebilmek için
her iki benchmark karşılaştırmasında logging kapalıdır; uygulamanın varsayılan log ayarı
değiştirilmemiştir. Eşzamanlı PTY doğruluk testinde logging açıktır.

```bash
cmake --build --preset release --target performance_benchmark
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software build/release/tests/performance_benchmark
ctest --preset release
ctest --preset security
bash scripts/security-evidence.sh build/release/bitshell
```

Tarihsel ölçüm dosyaları özel geliştirme arşivindedir; makineye özgü çıktı dizini
halka açık kaynak ağacına dahil edilmez. Yeni release için ölçümler tekrar alınmalıdır.

- before.json, after.json, after-repeat.json: bütün ölçümler; ilgili resource.txt dosyaları CPU/RSS.
- dev-junit.xml, release-junit.xml, sanitizer-junit.xml: tam test sonuçları.
- stability-run.txt, stability-junit.xml: seçili sanitizer testlerinin üçer tekrarı.
- release-evidence.txt: kaynak/binary manifesti ve ELF kontrolleri.
- before-startup.png, after-startup.png ve ui/ klasörü: görsel kanıtlar.

Ham parser throughput'u, yoğun sürekli tam ekran çizimi/GPU upload'u, çok büyük yerel
dizinlerin ilk senkron taraması ve çok sayıda oturumun toplam scrollback belleği sonraki
ölçüm hedefleridir. Bu çalışma bütün darboğazların veya bütün donanım/masaüstü
kombinasyonlarının çözüldüğü anlamına gelmez.

## Teknik dayanaklar

Qt'nin [QQuickPaintedItem belgesi](https://doc.qt.io/qt-6/qquickpainteditem.html) raster
çizim/texture upload maliyetini ve paint sırasında GUI/render thread ilişkisini açıklar.
Bu nedenle CPU paint ölçümü FPS olarak sunulmamış; render yolunda yeni QObject veya
timer oluşturulmamıştır. Qt'nin
[performans rehberi](https://doc.qt.io/qt-6/qtquick-performance.html) gereksiz binding/iş
yükünün azaltılması ve ölçüm yaklaşımına dayanak oluşturur.
[QImage device pixel ratio](https://doc.qt.io/qt-6/qimage.html#devicePixelRatio) davranışı
önbellek boyutlandırmasında dikkate alınmış ve piksel testleriyle kontrol edilmiştir.
Kaynaklar 11 Eylül 2026'da kontrol edilmiştir; hız iddialarının dayanağı Qt'nin genel
belgeleri değil, yukarıdaki yerel önce/sonra ölçümleridir.
