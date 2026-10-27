# 8bitShell Güvenlik Değerlendirmesi

## Yönetici özeti

8bitShell'in mevcut Linux sürümü için bağlantı, terminal, dosya aktarımı, yapılandırma,
makro, kayıt ve derleme katmanlarında güvenlik sıkılaştırması uygulanmıştır. 11 Eylül
2026 tarihli değerlendirmede öncelik; sunucu kimliğinin doğrulanması, içe aktarılan
verinin komuta dönüşmemesi, yerel dosyaların korunması ve saldırgan çıktının kaynak
tüketiminin sınırlandırılmasıdır. Ürün için zorunlu bir FIPS veya kuruma özgü kriptografi
profili tanımlı değildir; uygulanan profil güçlü, açık ve test edilebilir bir varsayılandır.

**Sonuç: kontrollü pilot için daha güçlü bir teknik temel vardır; bağımsız denetimden
geçmiş, sertifikalı veya koşulsuz enterprise production-ready bir ürün iddiası yoktur.**
Kaynak kodda bulunan ve bu raporda tanımlanan risk yolları kapatılmış veya sınırlandırılmıştır.
Bağımsız sızma testi, gerçek kurum ortamındaki uyumluluk, imzalı dağıtım ve sürekli
zafiyet yönetimi halen üretim öncesi kabul koşuludur. Başka SSH istemcilerinin bir kurumda
kullanılabiliyor olması, bu uygulamanın otomatik olarak onaylı olduğu anlamına gelmez.

En önemli davranış değişikliği ilk bağlantıdadır: uygulamanın yönettiği SSH, SFTP ve
monitor kanalları bilinmeyen/değişmiş sunucu anahtarını kabul etmez. Sunucu anahtarı veya
host CA güveni bağımsız doğrulamayla önceden tanıtılmalıdır. Eski şifrelemeye, agent
forwarding'e ve keyfi SSH seçeneklerine otomatik geri dönüş yoktur. Bu nedenle bazı eski
profiller ve sunucular artık bilinçli olarak bağlantı hatası verebilir.

## Kapsam ve değerlendirme ölçütü

İncelenen uygulama C++23, Qt Quick ve libvterm kullanır; ağdaki SSH protokolünü kendisi
uygulamak yerine işletim sisteminin OpenSSH istemcisine devreder. İnceleme mevcut
`src`, `qml`, CMake, testler ve çalışma zamanı yapılandırma yollarını kapsar. CyberArk
otomasyonu, RDP/VNC, ekip senkronizasyonu, parola kasası ve gelecekteki Windows portu
bu sürümde tamamlanmış güvenlik özellikleri olarak değerlendirilmemiştir.

Sonraki ek: [sınırlı PSMP yönlendirme ve kişisel profil](CYBERARK.md) artık uygulanmıştır.
Kişisel sicil/paylaşılan rota ayrımı, yerel gateway onayı, protokol izinleri ve mevcut
SSH politikası birlikte uygulanır. Bu ek, genel konuşma otomasyonu veya canlı kurum
PSMP/MFA doğrulaması değildir. Profil resmi yalnız kullanıcı seçimiyle alınan, boyutu
sınırlandırılmış PNG/JPEG'den yeniden üretilmiş yerel bir kopyadır. Terminal olarak
`-e` ile açıkça istenen yerel programlar, paylaşılan SSH profili çalıştırma yolundan
ayrıdır; bu özellik yerel kabuk komutlarını yönetilen SSH politikasına tabi kılmaz.

Kanıtlar üç sınıftır: kaynak kodda uygulanmış kontroller, yerel otomatik/entegrasyon
testleri ve birincil üretici/standart kaynakları. Aşağıdaki önem dereceleri uygulama
bağlamındaki mühendislik öncelikleridir; bağımsız doğrulanmış CVSS puanı veya yeni CVE
ilanı değildir. Dış sistemlere saldırı, kurum hesabıyla bağlantı, gerçek parola/MFA
kullanımı veya kullanıcı anahtarlarını değiştirme yapılmamıştır.

OpenSSH'nin kendi belgeleri, istemci seçeneklerinin ve yapılandırma kancalarının yerel
komut çalıştırabildiğini gösterir. Argüman dizisi kullanmak shell birleştirmesini önler,
ancak tek başına seçenek enjeksiyonunu önlemez; doğrulama ve daraltılmış seçenekler
ayrıca gerekir.[^1][^2] Uygulamanın kontrol modeli bu ayrımı esas alır.

## Varlıklar, saldırganlar ve güven sınırları

Korunacak varlıklar yalnız parolalar değildir. SSH agent yetkisi, özel anahtarlar,
sunucu envanteri, üretim komutları, veritabanı çıktıları, makrolar, indirilen dosyalar,
terminal geçmişi ve oturum kayıtları da hassastır. Bağlantı hedefinin doğruluğu ve
dosyanın yanlış yere yazılmaması, gizlilik kadar bütünlük açısından da önemlidir.

| Kaynak / sınır | Tehdit | Mevcut kontrol ve sınırı |
| --- | --- | --- |
| Ağ → OpenSSH | Araya girme, DNS yönlendirmesi, yanlış sunucu | Sıkı host-key doğrulaması; DNS tek başına güven kaynağı değil |
| Katalog / Quick Connect → süreç argümanları | Seçenek, ProxyJump veya yerel hook enjeksiyonu | Ortak doğrulayıcı, izinli seçenekler, sabit program yolu, özel `-F` dosyası |
| Uzak terminal → libvterm / UI | Kontrol dizileri, pano değiştirme, aşırı çıktı, görsel aldatma | OSC 52 kapalı, başlık/bellek sınırları, PlainText; parser süreç izolasyonu henüz yok |
| Makro / pano → terminal girdisi | Fark edilmeden komut çalıştırma | Çok satır/görünmez biçim/Enter için tam önizleme ve açık onay |
| SFTP / masaüstü sürükleme → dosya sistemi | Symlink takibi, yanlış konum, çakışma, kalıcı hassas kopya | Özel staging, tür kontrolleri, yerel atomik no-replace, geçici drag dosyaları |
| YAML/XML/JSON/INI → modeller | Ayrıştırıcı kaynak tüketimi, hatalı veri, eski verinin kaybı | Boyut/derinlik sınırları, şema doğrulama, atomik yazma, başarısız yüklemede koruma |
| Özel anahtar / PPK → yerel depo | Anahtar sızıntısı, yanlış varsayılan, güvensiz converter | Owner-only kopya, metadata-only YAML, sabit kimlik, sabit-yollu hash-pinned PuTTYgen, `IdentitiesOnly` |
| Derleme → çalıştırılabilir dosya | Kütüphane yolu kaçırma, bellek hatası etkisi, bağımlılık açığı | ELF sertleştirme, RPATH kaldırma, hash pin, sanitizer testleri; imzalı tedarik zinciri henüz yok |

Uzak sunucu geçerli anahtara sahip olsa bile içeriği güvenilir kabul edilmez. Yetkili ama
ele geçirilmiş bir sunucu terminalde yanıltıcı bir parola sorusu gösterebilir, sahte
monitor verisi üretebilir veya kötü amaçlı dosya verebilir. Host-key doğrulaması sunucuyu
tanımlar; o sunucunun iyi niyetini veya işletim sisteminin temiz olduğunu kanıtlamaz.

Aynı masaüstü hesabını veya root yetkisini ele geçirmiş saldırgan kapsamlı bir artık
risktir. Uygulama, root'a veya aynı kullanıcı adına çalışan tüm yazılımlara karşı bir
güvenlik sınırı/DRM değildir. Özellikle yerel terminal bilerek normal bir shell'dir:
oraya elle yazılan `ssh` komutu uygulamanın yönetilen SSH profiline sarılmaz. Sistem
geneli yasaklama gerekiyorsa endpoint ve sunucu politikası ayrıca uygulanmalıdır.

## SSH bağlantı ve kimlik doğrulama politikası

`SshSecurity` artık terminal, SFTP, monitor ve bağlantı çözümleyicisinin ortak girişidir.
Her yol aynı doğrulamayı ve özel üretilmiş yapılandırmayı kullanır. Programlar
`/usr/bin/ssh` ve `/usr/bin/sftp` olarak sabittir; SFTP'nin SSH alt süreci de sabittir.
Profil alanları shell metnine birleştirilmez. Hedefin önüne seçenek sonlandırıcısı `--`
yerleştirilir. Yerel shell'in programı da mutlak ve çalıştırılabilir bir yol olmalıdır.

Özel yapılandırma `0700` geçici dizinde `0600` dosyadır. `-F` kullanımı nedeniyle
`~/.ssh/config` ve sistem `ssh_config` dosyaları okunmaz; `Match exec`, `LocalCommand`,
`ProxyCommand`, `Include` ve kullanıcı tanımlı multiplexing kancaları bu kanaldan
çalışmaz. Bu tercih, dağıtımın SSH config üzerinden gelen crypto-policy include'larını
da dışarıda bırakır. **OS/FIPS politikasının otomatik miras alındığı iddia edilemez.**
Gelecekte kurumsal profil gerekirse ayrı, sürümlü ve test edilmiş bir uygulama politikası
olarak tasarlanmalıdır; kullanıcıya keyfi `-o` kutusu açmak güvenli bir eşdeğer değildir.

Uygulanan profilin özeti şöyledir; tam metnin kaynağı
[`SshSecurity.cpp`](../src/core/src/SshSecurity.cpp) dosyasıdır:

| Alan | Uygulanan davranış |
| --- | --- |
| Sunucu güveni | `StrictHostKeyChecking=yes`; DNS ile otomatik güven ve otomatik key update kapalı |
| Güven deposu | OpenSSH'nin standart kullanıcı/sistem known_hosts yolları; uygulama otomatik yazmaz |
| Agent | Yerel agent ile imzalama mümkün; agent forwarding ve otomatik agent'a anahtar ekleme kapalı |
| Tünel / multiplex | Ek forwarding'ler temizlenir; Tunnel, ControlMaster, ControlPersist, escape command line kapalı |
| X11 | Varsayılan kapalı; açık onaydan sonra yalnız `-X`; trusted `-Y` kapalı; 120 saniye authorization timeout |
| Algoritmalar | CBC/3DES/RC4 aileleri, SHA-1 KEX, `ssh-rsa` SHA-1/DSA imzaları ve eski MAC'ler dışlanır |
| RSA boyutu | Asgari 3072 bit |
| Kimlik doğrulama | Terminalde parola/MFA mümkün; SFTP/monitor BatchMode ile key/agent; GSSAPI ve hostbased kapalı |
| Süreler | 10 saniye connect timeout; 15 saniye keepalive, 3 başarısız yanıt; sınırlı parola denemesi |

Algoritma listesi eski bir blogdan tamamen kopyalanarak dondurulmamıştır. Güncel OpenSSH
varsayılanları korunup istenmeyen aileler çıkarılır; bu, sonradan gelen modern yöntemleri
yanlışlıkla dışlamamayı amaçlar. OpenSSH, hibrit post-kuantum key exchange'i varsayılanlarına
eklemiştir; uygulama bunu bozmaz ancak yalnız PQ bağlantısı şart koşmaz.[^3]
Dolayısıyla her bağlantının post-kuantum olduğu veya bütün kimlik doğrulamanın PQ olduğu
söylenemez. Terminal kanalında OpenSSH'nin algoritma uyarıları görünür kalır.

Quick Connect yalnız `-p`, `-l`, `-i`, `-J`, `-X`, `-C` kabul eder. Host, kullanıcı,
port ve jump zinciri ayrı ayrı doğrulanır; komut ekleri, kontrol karakterleri, geçersiz
portlar, tekrar eden değer seçenekleri ve keyfi config/socket yolları reddedilir.
IPv6 desteklenir; jump host IPv6 adresi köşeli parantez ister. En fazla sekiz hop vardır.
Kimlik dosyası mutlak veya `~/` yoludur; `%` ve `$` expansion token'ları kabul edilmez.
İşlem ortamından harici askpass ve bazı loader/provider override'ları çıkarılır; PATH
sistem program dizinlerine sınırlandırılır. Bu, önceden ele geçirilmiş başlatma ortamını
tamamen güvenli hale getiren bir sandbox olarak sunulmaz.

ProxyJump özel dikkat gerektirir: yalnız ana sürece `-o` eklemek her hop'u aynı politika
altına almayabilir. Özel `-F` dosyasının jump sürecine aktarıldığı gerçek OpenSSH testiyle
kontrol edilmiştir. Her hop'un kendi doğrulanmış host key'i gerekir. Hedef için verilen
`-i`, farklı bir jump-host identity yönetimi değildir; böyle ortamlarda yerel agent veya
ileride ayrı per-hop kimlik modeli gerekir. Tam başarılı çok-hop/MFA matrisi henüz kurum
ortamında denenmemiştir.

13 Eylül 2026 SSH-key eki, en fazla 64 özel anahtarı owner-only yerel kopyalar olarak
yönetir. `keys.yaml` yalnız rastgele sabit kimlik, görüntü adı, kısıtlı dosya adı, tip,
bit sayısı ve fingerprint taşır; özel anahtar, kaynak yolu ve PPK parolası taşımaz.
Değiştirme önce yeni nesli yazar ve OpenSSH ile doğrular, metadata'yı atomik geçirir,
sonra eski uygulama kopyasını siler. Sabit kimlik korunduğu için session/default/CyberArk
atamaları günlük rotasyonda bozulmaz. Silinen veya güvensiz hale gelen açık bir referans
parola/agent'a sessiz düşmek yerine bağlantıyı bloklar.

Genel varsayılan ile CyberArk varsayılanı ayrıdır; CyberArk anahtarı yalnız çözülmüş PSMP
profilinde öncelik kazanır. Somut bir managed veya external anahtar seçildiğinde
`IdentitiesOnly=yes` uygulanır; böylece agent ve varsayılan dosyalardaki ilgisiz kimlikler
sunucuya sırayla teklif edilmez. Bir session `identityKey: none` ile uygulama varsayılanını
bilinçli biçimde atlayıp standart OpenSSH/agent davranışını seçebilir. OpenSSH'nin birden
çok `IdentityFile` değerini biriktirmesi ve `IdentitiesOnly` anlamı bu davranışın teknik
dayanağıdır.[^1]

PPK v2/v3 girdisi bağlantıda Plink'e verilmez. Güncel PuTTYgen 0.85 yalnız bir kez modern
OpenSSH özel anahtarına çevirir.[^19] Program yolu uygulamanın yanına veya Linux'ta sabit
`/usr/bin/puttygen` yoluna bağlıdır; `PATH` ile arama yapılmaz. Resmi paketlerde dönüştürücü
SHA-256 kilitli resmi kaynaktan derlenir; Windows çıktısının provenance kaydı kaynak kilidini
binary özetine bağlar ve MIT bildirimi pakete girer. Şifreli PPK parolası komut argümanına,
YAML'a veya loga yazılmaz; 0700 geçici dizindeki 0600 dosya üzerinden verilir, sınırlı
süreli süreçten sonra silinir ve UI alanı temizlenir. Parolanın kısa süreli Qt/process
belleğinde bulunması ve aynı kullanıcı/root zararlısına karşı bir HSM sınırı olmaması
açık artık risktir.

## Terminal, pano, makro ve UI

Uzaktan gelen isimler, dosya adları, komut önizlemeleri, başlıklar ve katalog etiketleri
metindir; HTML değildir. Uygulama `Text` alanları PlainText'e geçirilmiş, combo-box
öğelerinde düz metin delegate kullanılmış ve dinamik tooltip içerikleri escape edilmiştir.
Qt'nin AutoText davranışı işaretlemeyi ve resim yüklemeyi yorumlayabildiğinden, bu ayrım
gereksiz ağ/dosya okuma ve render saldırı yüzeyini azaltır.[^4]

Uzak OSC 52 dizilerinin panoya yazması için bir callback tanımlanmaz. Bir gerçek PTY
testi pano içeriğinin aynı kaldığını kontrol eder. OSC başlığı 4096 byte ile sınırlandırılır.
Terminal hücre geçmişine sekme başına 64 MiB bütçe, grid'e 1000×500 üst sınır ve PTY
girdi kuyruğuna 1 MiB sınır uygulanır. Her event-loop turunda okunan çıktı miktarı sınırlıdır.
Bu sayılar toplam uygulama belleğinin garantili üst sınırı değildir; çok sayıda sekme,
Qt nesneleri ve arama sonuçları ayrıca bellek kullanır.

Çok satırlı yapıştırma, görünmez Unicode biçimlendirme ve Enter gönderen makro için
önce tam içerik ve hedef gösterilir. CR ve biçim karakterleri önizlemede görünür etiket
haline gelir; terminal kontrol karakterleri ve aşırı büyük girdiler reddedilir. Cancel
klavye odağıyla açılır; Enter/Escape iptal akışı UI testindedir. Güvenlik penceresi
açıkken sekme ekleme/değiştirme/kapatma yolları engellenir; böylece onayın hedefi kaymaz.
İptal veya terminal reset'inden sonra bekleyen girdi gönderilmez.

Tek satırlı, yalnız yapıştıran bir makro hızlı kullanımını korur. Ancak remote program
satır sonu olmadan da girdiyi yorumlayabilir; yapıştırmak “zararsız inceleme modu” değildir.
Onaylanan çok satır, bracketed-paste desteklemeyen veya onu farklı kullanan programda
komut çalıştırabilir. Button grupları şeffaf YAML olarak kalır; hiçbir şekilde parola
kasası değildir. İçe aktarılan makrolar import sırasında otomatik yürütülmez.

Regex vurgulama ve conversation eşleşmelerinde desen uzunluğu, match/depth/heap bütçeleri
vardır. JIT devre dışı bırakılır; PCRE2 belgelerinde JIT'in bazı kaynak limitlerini aynı
şekilde uygulamadığı ve yinelenen limitlerde düşük değerin geçerli olduğu belirtilir.[^5]
En fazla 64 highlight kuralı ve sınırlı satır uzunluğu işlenir. Bütçeye takılan eşleşme
vurgulama üretmeyebilir; terminalin byte içeriği değiştirilmez. Bu, sınırsız fuzzing veya
regex motorunun hatasızlığı iddiası değildir.

## SFTP ve yerel dosya güvenliği

SFTP batch komutları shell komutu değildir, fakat kendi kaçış/glob kuralları vardır.
Bu yüzden program ve SSH seçeneklerinden bağımsız olarak yol uzunluğu, kontrol karakterleri,
dosya türü ve komut sayısı kontrol edilir. Üretim ortamında `BIT8SHELL_SFTP_EXECUTABLE` ile
başka program çalıştırma yolu kaldırılmıştır. Yerel test adaptörü native test API'sinde
kalır; QML, CLI veya ortam değişkeniyle seçilmez.

Batch mode'un SSH hata/uyarılarını susturan varsayımı `-N` ile kaldırılmıştır. Böylece
sunucu anahtarı hatası yalnız genel “connection closed” yerine teşhis edilebilir kalır;
bu davranış gerçek SFTP bağlantı testinde kontrol edilir.[^18]

İndirme hedef dizinin içinde özel `.bitshell-download-*` staging dizinine yapılır. Sonuç
ağacı doğrulandıktan sonra Linux `renameat2(RENAME_NOREPLACE)` ile yayımlanır. Hedefte
sonradan bir dosya, symlink veya aynı adlı başka nesne ortaya çıksa da üzerine yazılmaz;
uygun alternatif isim denenir. Dosyalar `0600`, oluşturulan dizinler `0700` olur. Başarısız
indirme final hedefte tamamlanmış dosya gibi görünmez; özel staging sonraki işlemde veya
controller kapanışında temizlenir.

Yüklemede dizin ağacı önceden taranır; symlink ve özel dosyalar, alt dizin içinde olsalar
bile reddedilir. İndirilmiş ağaçta da yalnız izinli türler kabul edilir. En fazla 50.000
aktarım girdisi, 20.000 liste öğesi ve 16 MiB liste çıktısı sınırları vardır. Navigasyon
ve aktarım süre limitleri kullanıcı kılavuzunda belirtilir. Bu sınırlar uzak sunucunun
gerçekte ne yaptığını kanıtlamaz.

Yerel file-browser kopyalarında da 50.000 girdilik bütçe, 64 dizin seviyesi sınırı,
owner-only yeni dizinler ve sınırlı çakışma ismi denemesi uygulanır. Dizin içeriği kopyalama
sırasında streaming iterator ile dolaşılır; limit öncesi bütün ağaç belleğe alınmaz.

Yerel no-replace ile uzak overwrite önleme aynı güvence değildir. Uzak yüklemede isim
çakışması son listeye göre değerlendirilir; başka bir kullanıcının eşzamanlı değişimine
karşı sunucu tarafında atomik exclusive-create garantisi yoktur. Kısmi uzak yükleme
kalabilir. Yerel upload preflight da kötü niyetli aynı-UID süreçlerin dosyayı tam aktarım
anında değiştirmesine karşı tam bir filesystem sandbox değildir. Paylaşımlı/hostile
yerel kaynak ağaçlarında ek snapshot/worker tasarımı gerekir.

Masaüstüne sürüklenen remote dosyanın geçici kopyası artık kalıcı uygulama cache'inde
saklanmaz. Özel geçici dizin değiştirilince veya controller yok edilince temizlenir;
harici kopyalama tamamlanana kadar uygulama açık tutulmalıdır. Çökme sonrası geçici dosya
kalabilir. Önceki sürümlerin `drag-exports/` dizinleri ve eski kullanıcı dosyaları bu
değişiklikte topluca silinmemiştir.

## Yapılandırma ve kayıtlar

YAML dosyaları 8 MiB ile sınırlıdır. Parser event aşamasında 32 seviyelik derinlik,
200.000 event, 1 MiB scalar ve tek belge koşulu uygulanır; limitler yalnız Node grafiği
oluştuktan sonra kontrol edilmez. Bütün anchor/alias kullanımı, özel tag'ler ve yinelenen
anahtarlar reddedilir. Model şemaları bilinmeyen alanları ve uygunsuz tipleri ayrıca
reddeder. XML importer 128 MiB, 64 seviye ve 200.000 element ile sınırlıdır; DTD/entity
kullanımı kabul edilmez. Legacy JSON/INI de sınırsız `readAll` yerine bounded okuma kullanır;
INI özel bir snapshot üzerinden ayrıştırılır.

YAML container derinliğine ek olarak mantıksal katalog büyümesi de sınırlandırılır:
10.000 session/expanded folder, yol başına 1024 karakter/64 seviye; makrolarda 256 grup ve
grup başına 256 buton. Böylece küçük bir YAML'daki uzun slash-separated yol sınırsız
ancestor üretmez. Import, reload ve save doğrulaması sınırı aşan veriyi truncate etmez;
reddeder ve önceki dosyayı korur. Bu yol-amplification durumu ayrıca regresyon testindedir.

`PrivateFile` yeni parent dizinlerini owner-only oluşturur, path bileşenlerini symlink
takip etmeden açar ve düzenli dosya türü arar. Atomik config yazma doğrudan dosyayı
truncate etmeye geri dönmez. Log açarken sahiplik ve hard-link sayısı da kontrol edilir.
Canonical YAML'da lock ve içerik hash'i eski modelin dışarıdan düzenlenmiş dosyayı
sessizce ezmesini önler. Hatalı yükleme önceki modeli korur ve düzeltilmiş reload'a
kadar yazmayı engeller. Legacy kaynak dosyalar silinmez.

Bu korumalar, keyfi bir settings dosyasını güvenilir yetki kaynağı haline getirmez.
Örneğin geçerli logging ayarları log kaydını açabilir, yolu değiştirebilir veya explicit
replace modunu seçebilir. Import UI bu yan etkileri bildirir. Settings yalnız güvenilir
kaynaktan alınmalıdır; tercihlerin anlamı kullanıcı yetkisiyle dosya yazmayı içerir.
Mevcut dizinlerin izinleri ve eski yedek/export dosyaları geriye dönük topluca değiştirilmez.
QSaveFile parent kontrolü aynı-UID saldırganın bütün yarışlarını önleyen dir-fd tabanlı
bir capability sistemi değildir.

Normal log formatı korunmuştur. Keystroke'lar doğrudan kaydedilmez; fakat remote echo ve
program çıktısı parola, token, sorgu sonucu veya başka hassas bilgi içerebilir. ANSI
temizleme gizli veri maskeleme değildir. OWASP kayıt rehberi hassas verinin dışlanmasını
ve kayıt erişiminin korunmasını ayrı kontroller olarak ele alır.[^6] Bu uygulamanın
logları otomatik redaction, şifreli kasa, WORM veya merkezi SIEM audit kaydı değildir.

Loglarda traversing/symlink/hardlink kontrolleri, owner-only açma ve rotasyon vardır.
Bir dosya eşzamanlı iki logger tarafından kullanılamaz; benzersiz dosya adı gerekir.
Silme/rename/flush hatası görünür hata üretir ve kayıt durur; rotasyon başarısızken mevcut
log sessizce truncate edilmez. Diskin dolmasına karşı sonlu boyut/retention ayarı seçilmelidir;
“sınırsız log” seçeneği durmaktadır. Endpoint diski, backup, clipboard history ve screenshot
politikaları uygulama dışındaki idari kontrollerdir.

## Süreç ve derleme sertleştirmesi

PTY için argv/env fork öncesinde hazırlanır; child sabit programı `execve` ile çalıştırır.
İlgisiz descriptor'lar exec öncesi kapatılır, master nonblocking/CLOEXEC açılır ve hata
durumunda başlatma durur. Child exit/reaping yolları test edilmiştir. Başlatılan oturum
gibi tam shell kabiliyeti gereken süreçlerde `no_new_privs` veya genel seccomp kısıtları
körü körüne uygulanmamıştır; bunlar normal `sudo` ve terminal kullanımını kırabilir.

Ana uygulama root/set-id çalışmayı reddeder, dumpable bayrağını kapatır ve core limitini
sıfırlar. Bunun child exec, işletim sistemi crash collector'ı, swap, root ve debugger
yetkilerinin tamamına karşı kesin sır koruması olmadığı önemlidir. Linux'ta pipe tabanlı
core collector davranışı yalnız RLIMIT_CORE kontrolüyle eşdeğer değildir.[^7]

Linux native build'de PIE/PIC, desteklenen stack protector/stack-clash/CF-protection,
release için `_FORTIFY_SOURCE=3`, RELRO/NOW ve NX stack ayarları eklenmiştir. Denetlenmiş
release dosyasında PIE, full RELRO, non-executable stack, stack-canary ve fortified libc
referansları doğrulanmıştır. Donanım CET/IBT'nin her sistemde fiilen etkin olduğu veya
bütün bellek hatalarının engellendiği iddia edilmez.

Binary incelemesinde Qt/CMake install-RPATH hazırlığının build-tree binary'sinde boş
path bileşenleri bıraktığı görülmüştür. Bu, çalışma dizininden kütüphane çözümleme riski
oluşturur; native Linux yapılandırmasında RPATH kaldırılmıştır. Son binary'de RPATH/RUNPATH
yoktur. Dinamik loader arama sırası ve CMake'in RPATH kapatma seçeneği bu kararın teknik
dayanağıdır.[^8][^9] Özel Qt dağıtımı/AppImage yapılacaksa paketleme ayrıca değerlendirilmelidir.

## Öncelikli bulgular ve kapanış kanıtı

| Öncelik | Risk yolu / önkoşul | Düzeltme | Kanıt |
| --- | --- | --- | --- |
| Yüksek | Operatörün kötü niyetli hedef/seçenek girmesi veya config hook mirası | Ortak izinli argüman builder, private `-F`, agent/tunnel kısıtları | 31 enjeksiyon girdisi; `ssh -G`; gerçek jump trust reddi |
| Yüksek | Ağ saldırganı veya yanlış host key | Strict host trust, otomatik kabul yok | Bilinmeyen/değişmiş anahtar reddi; doğru anahtar ile SSH/SFTP kabulü |
| Yüksek | Saldırgan yazılabilir CWD'den build binary başlatma | Boş/relative build RPATH kaldırıldı | Son ELF'de RPATH/RUNPATH yok; evidence script bunu release gate sayar |
| Yüksek | Kötü niyetli dosya yolu/türü veya indirme çakışması | Nofollow/sahiplik kontrolleri, staging ve atomic no-replace | FIFO/symlink/hardlink; nested link; yerel çakışma/sentinel testleri |
| Orta–yüksek | Makro/pano içeriğini kontrol eden kişi ve operatör etkileşimi | Tam içerik onayı, ESC/control reddi, hedef sabitleme | PTY ve UI Enter/Escape/sekme engelleme testleri |
| Orta | YAML/XML/regex/terminal kaynak tüketimi | Parser preflight, bounded regex, kuyruk ve scrollback bütçeleri | Derinlik/alias testleri; 1500 deterministik YAML mutasyonu; uzun OSC başlığı |
| Orta | Uzak/UI metninin HTML yorumlanması veya pano yazımı | PlainText, tooltip escaping, OSC 52 callback yok | Düz metin önizlemesi ve gerçek PTY pano sentinel testi |
| Orta | Log izinleri, rotasyon hatası, eşzamanlı logger | Owner-only dosyalar, lock, fail-stop rotasyon | İzin, rotasyon, link ve eşzamanlı lock testleri |

Bu tablo, bütün olası zafiyetlerin bulunmuş olduğunu söylemez. Testlerde yalnız amaçlanan
engelleme davranışının gözlenmesi, bütün geçmiş sürümler için exploitability veya uzaktan
kimlik doğrulamasız saldırı iddiası değildir. Ekran görüntüsü/UI kontrolleri offscreen Qt
ile yürütülmüştür; gerçek Plasma/Wayland çok monitör, yüksek DPI ve uzun süreli çalışma
matrisiyle karıştırılmamalıdır.

## Bağımlılık ve güncel güvenlik duyuruları

Test ortamı Nobara 44 x86-64, GCC 16.2.1 ve Qt 6.11.2'dir. Uygulama OpenSSH ve Qt/libvterm
için sistem paketlerini kullanır; YAML fallback'i kaynak hash'iyle sabitlenmiştir.
Sürüm numarası tek başına zafiyet kararı değildir: dağıtımın backport kanıtı ayrıca
incelenmelidir. Minimum CMake/Qt derlenebilirlik gereksinimleri güvenlik destek taahhüdü
değildir.

| Bileşen | Yerel kanıt | Değerlendirme |
| --- | --- | --- |
| OpenSSH | `openssh-clients-10.2p1-14.fc44.x86_64`; OpenSSL 3.5.7 | 10.5 upstream vardır; yerel paket daha düşük sürüm etiketiyle düzeltme backport'ları içerdiğini bildirir |
| Qt | qtbase `6.11.2-2.fc44`, qtdeclarative `6.11.2-1.fc44` | Güncel advisory listesiyle erişilebilirlik/sürüm incelemesi yapılmıştır; tüm transitive/plugin CVE'leri için tarama sonucu değildir |
| libvterm | `0.3.3-7.fc44` | Üretici sayfasında listelenen 0.3.3 ile uyumlu; C parser aynı UI sürecindedir |
| yaml-cpp | Bundled fallback `0.9.0`; sistemdeki 0.8 kullanılmıyor | 0.8 fallback yükseltildi; kaynak SHA-256 CMake'de sabit |
| PuTTYgen | Bundled command-line `0.85`; bağlantı motoru değil | Resmi kaynak SHA-256 sabit; Windows CLI MSVC ile kaynaktan üretilip provenance ile doğrulanır; düz ve şifreli PPK dönüşümü test edildi |

OpenSSH 10.5 sürüm notları 11 Ağustos 2026 tarihlidir; yakın sürümlerde client/SFTP,
rekey ve forwarding/multiplexing düzeltmeleri vardır.[^10] Yerel RPM changelog'unda
CVE-2026-59995, CVE-2026-60002, CVE-2026-73282 ve CVE-2026-35386 için düzeltme kayıtları
görülmüştür. Bunlar paket üreticisinin patch bildirimidir; burada her CVE'nin exploit'i
bağımsız çalıştırılmamıştır. “10.2 eski, kesin açıktır” ve “changelog var, kesin hiçbir
açık yoktur” sonuçlarının ikisi de desteklenmez. Uygulama multiplex/agent forwarding'i
kapatsa da upstream patch ihtiyacını ortadan kaldırmaz.

Qt için örnek triage:

- **CVE-2026-15037:** QDom serialization XML injection; üreticiye göre düzeltme 6.12.0'da.
  Kurulu 6.11.2'nin bu düzeltmeyi içerdiği iddia edilmez. Repo'da QDom çağrısı veya XML
  serialization yolu saptanmadı; importer QXmlStreamReader kullanır. Bu dar erişilebilirlik
  değerlendirmesi, tüm yüklü plugin'lere ilişkin bir VEX belgesi değildir.
- **CVE-2026-9499 / CVE-2026-6210:** üretici 6.11.1 düzeltmesini listeler; testte kullanılan
  6.11.2 bildirilen etkilenen aralığın dışındadır.
- AutoText/VectorImage geçmiş duyuruları, dış metni PlainText tutma ve gereksiz resim/QML
  yorumlamasını açmama kararını destekler.[^11]

Qt güvenliği uygulama ve dağıtım sorumluluğuyla birlikte ele alınmalıdır; Qt'nin SBOM
olanakları bu uygulamanın tam envanterini otomatik kanıtlamaz.[^12] libvterm'in üretici
sayfasında 0.3.3 listelenmesi de bağımsız fuzzing veya zafiyetsizlik garantisi değildir.[^13]

yaml-cpp'nin güvenlik politikası yalnız en son release için güvenlik güncellemesi
öngörür.[^14] Bu nedenle eski 0.8 fallback'i 0.9.0'a yükseltilmiştir. 0.9.0 notlarında
`LoadAll()` olası sonsuz döngü düzeltmesi dahil parser/format düzeltmeleri vardır.[^15]
İndirilen kaynak arşivinin pin'i:

```text
25cb043240f828a8c51beb830569634bc7ac603978e0f69d6b63558dadefd49a
```

Hash pin aktarım bütünlüğünü destekler; tek başına güvenilir builder, imzalı release veya
üretici hesabı ele geçirilmesine karşı tam koruma değildir. Ayrı binary/source provenance
ve imzalı dağıtım halen gereklidir.

## Doğrulama ve yeniden üretim

Güvenlik sıkılaştırması sonundaki referans test matrisinde Debug, Release ve AddressSanitizer + UndefinedBehaviorSanitizer +
LeakSanitizer yapılandırmalarının her biri 15 CTest hedefini hatasız tamamlamıştır.
Sanitizer testi `detect_leaks=1`, `abort_on_error=1`, `halt_on_error=1` ile çalışır;
suppressions dosyası kullanılmamıştır. Uygulama ve bundled YAML instrument edilmiştir;
sistemin Qt/libvterm/OpenSSH binary'leri bu çalışmada instrument edilerek yeniden
derlenmemiştir. Bu fark sanitizer sonucunun kapsamını sınırlar.

Gerçek SSH entegrasyonu geçici Ed25519 anahtarları ve yalnız loopback'te unprivileged
`sshd` kullanır. Doğru key ile authentication, bilinmeyen/değişmiş key reddi, CBC-only
server reddi ve jump'a policy aktarımı kontrol edilir. SFTP hem gerçek SSH güven akışıyla
hem de yerel OpenSSH sftp-server karşısında dosya aktarımı/çakışma testleriyle değerlendirilir.
Yerel SFTP server testi kurum ağı, büyük dosya, kesintili WAN veya MFA testi değildir.
SSH-key ekinden sonra Release yapılandırmasında 22/22 CTest hedefi geçmiştir; bu koşu düz
ve şifreli PPK importunu, zayıf RSA/public-key reddini, `IdentitiesOnly`, CyberArk önceliğini,
sabit kimlikle replacement ve private dosya izinlerini kapsar. Kullanıcının gerçek özel
anahtarı veya kurum CyberArk sistemi teste dahil edilmemiştir.

Başlıca kaynaklar: [`SecurityTest.cpp`](../tests/SecurityTest.cpp),
[`SshTrustTest.cpp`](../tests/SshTrustTest.cpp), [`TerminalViewTest.cpp`](../tests/TerminalViewTest.cpp),
[`PtyProcessTest.cpp`](../tests/PtyProcessTest.cpp), [`WorkspaceFeaturesTest.cpp`](../tests/WorkspaceFeaturesTest.cpp),
[`YamlConfigurationTest.cpp`](../tests/YamlConfigurationTest.cpp) ve
[`UiInteractionTest.cpp`](../tests/UiInteractionTest.cpp). Parser mutasyonları 1500
deterministik bozuk girdidir; coverage-guided, uzun süreli fuzz campaign değildir.

Yerel tekrar:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
cmake --preset release
cmake --build --preset release
ctest --preset release
bash scripts/security-evidence.sh build/release/bitshell
cmake --preset security
cmake --build --preset security
ctest --preset security
```

Security preset için toolchain'in ASan/UBSan geliştirme/runtime dosyaları gerekir. Bu
makinedeki doğrulamada imzası doğrulanmış libasan/libubsan RPM'leri yalnız geçici test
dizinine açılmış; sistem paketi kurulmamıştır. O dizine ait linker yolu yalnız özel
instrumentation build cache'indedir ve release binary'sinde yoktur. Bu geçici cache yolu
başka makineye taşınmamalıdır; yeni makinede kendi desteklenen toolchain'iyle yapılandırılmalıdır.

`security-evidence.sh` binary hash'i, sekiz ELF kontrolü, doğrudan bağımlılık sürümleri ve
kaynak hash manifest'i üretir; key/config/log içeriklerini okumaz veya dışarı göndermez.
Tam transitive SPDX/CycloneDX SBOM, otomatik CVE tarayıcı veya imzalı attestation değildir.
Tarihsel JUnit çıktıları ve UI kareleri özel geliştirme arşivinde tutulur;
halka açık kaynak ağacına makineye özgü QA kayıtları eklenmez. Her yeni release
için [yayın kabul süreci](RELEASING.md) ayrıca yürütülmelidir.

Bu referansı izleyen performans değişikliklerinin ayrı ölçümleri, 16 hedefli genişletilmiş
test matrisi ve yeni binary hash'i [performans doğrulamasında](PERFORMANCE-2026-09-11.md)
yer alır. Önceki güvenlik kanıtı yeni bir binary için otomatik attestation sayılmaz.

## Artık riskler ve üretim kabul koşulları

İlk öncelik güvenli host enrollment/rotation deneyimidir. Şu anda operatör yönetilen
known_hosts veya host CA kaydını dışarıda sağlamalıdır. Kurumsal CA yaşam döngüsü,
revocation/KRL dağıtımı, role-based access ve hedef/komut izinleri sunucu/kurum tarafında
tasarlanmalıdır. Uygulama bu yönetişim işlevlerinin yerine geçmez. Yalnız bilinmeyen host'u
reddetmek uçtan uca kimlik yaşam döngüsü çözümü değildir.

İkinci öncelik yerel saldırı yüzeyidir: libvterm ve UI aynı süreçtedir. Daha güçlü profil
için parser/transfer worker izolasyonu, dar IPC sözleşmesi, ayrı kaynak limitleri ve
coverage-guided fuzzing gerekir. Bu bir mimari yatırım ve uyumluluk testidir; “bir seccomp
satırı ekleyerek” bütün terminali sandbox ilan etmek doğru değildir. Parola broker'ı
eklendiğinde secret lifetime, zeroization, secure storage ve prompt spoofing yeniden
değerlendirilmelidir.

Üçüncü öncelik release işletimidir. NIST SSDF 1.1 final yayın; 1.2 revizyonu araştırma
tarihinde taslak olarak listelenmektedir.[^16] SLSA 1.2, kaynak/build güvence ve provenance
gereksinimlerini tanımlar.[^17] Bu yayınlar aşağıdaki kabul kontrollerini yönlendirir;
uygulamanın SSDF uyum sertifikası veya bir SLSA seviyesi aldığı söylenmez.

| Üretim öncesi koşul | Kabul kanıtı | Durum |
| --- | --- | --- |
| Bağımsız uygulama/terminal/SSH/SFTP sızma testi | Kapsamlı rapor, düzeltme ve retest | Yapılmadı |
| Kurumsal pilot matrisi | Direct/jump, IPv4/IPv6, key/MFA, host CA/rotation, eski Oracle, büyük dosya/bağlantı kopması, X11 kararı | Gerçek kurum ortamı bekliyor |
| Bağımlılık ve advisory yönetimi | Tam transitive SBOM, vendor backport/VEX değerlendirmesi, her release'te tarama, sorumlu ve süre | Yerel envanter/örnek triage var; sürekli süreç yok |
| İmzalı ve tekrarlanabilir dağıtım | Güvenilir build, artifact imzası, provenance, update/rollback doğrulaması | Yapılmadı |
| Gizli veri ve audit politikası | Parola kasası tasarımı, log sınıflandırma/retention, backup/disk koruması, gerekli ise merkezi audit | Yerel kontroller var; kurumsal işletim kararı gerekli |
| Güvenlik bildirim süreci | İzlenen özel kanal, desteklenen sürümler, müdahale sahibi, disclosure süresi | `SECURITY.md` taslağı var; işletilen kanal/SLA yok |

Yönetilen uygulama dışındaki yerel shell, root/same-UID süreçler, masaüstü clipboard history,
uzak sunucunun keyfi davranışı, onaylanan makrolar, sonlu olmayan log ayarı ve atomik olmayan
uzak upload çakışmaları açık sınırlardır. Bunlar saklanarak değil, uygun kullanım koşulları
ve takip eden testlerle yönetilmelidir. Yeni protokoller veya credential otomasyonu
eklendiğinde bu rapor otomatik olarak o özelliklerin güvenlik onayı sayılmaz.

## Kaynaklar

Aşağıdaki birincil kaynaklar 11 Eylül 2026 tarihinde kontrol edilmiştir. Kod/test bulguları
bu checkout'a aittir; üretici duyuruları zamanla değişebilir. Uzun alıntı yerine uygulamayla
ilgili dar teknik çıkarımlar kullanılmıştır.

[^1]: OpenBSD/OpenSSH. [ssh_config(5)](https://man.openbsd.org/ssh_config). Güncel istemci seçenekleri, host trust ve hook semantiği; erişim 2026-09-11.
[^2]: OWASP Foundation. [OS Command Injection Defense Cheat Sheet](https://cheatsheetseries.owasp.org/cheatsheets/OS_Command_Injection_Defense_Cheat_Sheet.html). Argüman enjeksiyonu, doğrulama ve parameterization; erişim 2026-09-11.
[^3]: OpenSSH Project. [Post-Quantum Cryptography](https://www.openssh.org/pq.html). Hibrit KEX varsayılanları ve kapsamı; erişim 2026-09-11.
[^4]: The Qt Company. [Text QML Type](https://doc.qt.io/qt-6/qml-qtquick-text.html). textFormat, AutoText/PlainText ve image davranışı; Qt 6 belgeleri, erişim 2026-09-11.
[^5]: PCRE2 Project. [pcre2pattern](https://www.pcre.org/current/doc/html/pcre2pattern.html). LIMIT_MATCH/LIMIT_DEPTH/LIMIT_HEAP ve NO_JIT; erişim 2026-09-11.
[^6]: OWASP Foundation. [Logging Cheat Sheet](https://cheatsheetseries.owasp.org/cheatsheets/Logging_Cheat_Sheet.html). Hassas veri, erişim ve kayıt koruması; erişim 2026-09-11.
[^7]: Linux man-pages project. [core(5)](https://man7.org/linux/man-pages/man5/core.5.html). Core dump ve pipe collector sınırları; erişim 2026-09-11.
[^8]: Linux man-pages project. [ld.so(8)](https://man7.org/linux/man-pages/man8/ld.so.8.html). Dinamik kütüphane arama yolları; erişim 2026-09-11.
[^9]: Kitware. [CMAKE_SKIP_RPATH](https://cmake.org/cmake/help/latest/variable/CMAKE_SKIP_RPATH.html). CMake referansı; erişim 2026-09-11.
[^10]: OpenSSH Project. [Release Notes](https://www.openssh.org/releasenotes.html). Özellikle 10.3–10.5; 10.5 yayın tarihi 2026-08-11; erişim 2026-09-11. Yerel ek kanıt: openssh-clients RPM changelog, dosya/terminal okuması, aynı tarih.
[^11]: The Qt Company / Qt Project. [List of known vulnerabilities in Qt products](https://wiki.qt.io/List_of_known_vulnerabilities_in_Qt_products). CVE sürüm aralıkları ve düzeltmeler; erişim 2026-09-11.
[^12]: The Qt Company. [Security in Qt](https://doc.qt.io/qt-6/security.html). Uygulama/dağıtım sorumlulukları ve SBOM olanakları; erişim 2026-09-11.
[^13]: Paul Evans / libvterm Project. [libvterm](https://www.leonerd.org.uk/code/libvterm/). Üretici sürüm ve mimari bilgisi; erişim 2026-09-11.
[^14]: yaml-cpp maintainers. [Security policy](https://github.com/jbeder/yaml-cpp/security). Desteklenen güvenlik sürümü; erişim 2026-09-11.
[^15]: yaml-cpp maintainers. [yaml-cpp 0.9.0 release](https://github.com/jbeder/yaml-cpp/releases/tag/yaml-cpp-0.9.0). Parser/format düzeltmeleri ve release kaynağı; erişim 2026-09-11.
[^16]: NIST. [Secure Software Development Framework publications](https://csrc.nist.gov/projects/ssdf/publications). SP 800-218 final ve revizyon taslağı durumu; erişim 2026-09-11.
[^17]: SLSA / OpenSSF. [SLSA specification v1.2](https://slsa.dev/spec/v1.2/). Approved kaynak/build/provenance çerçevesi; erişim 2026-09-11.
[^18]: OpenBSD/OpenSSH. [sftp(1)](https://man.openbsd.org/sftp). Batch/quiet mode ve `-N` davranışı; erişim 2026-09-11.
[^19]: PuTTY Project. [PuTTY key file format ve araç belgeleri](https://the.earth.li/~sgtatham/putty/0.85/htmldoc/Chapter8.html). PPK v2/v3 ve PuTTYgen dönüşüm modeli; erişim 2026-09-13.
