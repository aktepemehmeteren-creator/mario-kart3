# Mario Kart PSP — Cinematic Edition

Bu BUILD3 kaynak kodunun görsel/oynanış yükseltmesidir. PSP donanımı nedeniyle gerçek God of War seviyesinde yüksek çözünürlüklü texture/animasyon beklenmemelidir; bunun yerine PSP'nin sınırları içinde daha sinematik bir görünüm hedeflenmiştir.

## Yapılan yükseltmeler

- Daha detaylı kart gövdeleri, kokpit, cam, spoiler, jant ve lastikler
- Katmanlı araç gölgelendirmesi ve daha zengin malzeme renkleri
- Yol kenarında kayalar, lambalar ve büyük yarış kemerleri
- Daha yoğun ve sinematik çevre silüeti
- Sinematik takip kamerası ve hafif kamera eğimi
- Boost sırasında hız çizgileri ve sinematik letterbox efektleri
- Gökyüzünde stilize güneş/bloom efekti
- PSP fill-rate'i gözeten düşük poligonlu optimizasyon
- `-O3` derleme optimizasyonu

## PSP'ye kurulum

GitHub Actions ile build alındığında oluşan `EBOOT.PBP` dosyasını:

`PSP/GAME/MARIOKART/EBOOT.PBP`

konumuna koy.

PSP'de:

`Game -> Memory Stick -> Mario Kart PSP`

şeklinde görünmelidir.

## Build

PSPDEV ile:

```bash
mkdir build
cd build
psp-cmake ..
make
```

Sonuç:

`build/EBOOT.PBP`

