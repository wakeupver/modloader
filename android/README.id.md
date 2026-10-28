# Mod Loader untuk Android (plugin AML)

Port **Mod Loader** ke *GTA San Andreas Android 2.00* (ARMv7, `libGTASA.so`) dalam bentuk plugin
[AML (Android Mod Loader)](https://github.com/RusJJ/AndroidModLoader). Taruh mod di sebuah folder, jalankan game, mod
langsung aktif. Aturan folder dan `modloader.ini`-nya sama seperti versi Windows.

> **Status: versi pertama, belum pernah dijalankan di perangkat asli.** Semua yang bisa diperiksa tanpa game sudah
> diperiksa (lihat bagian Verifikasi), tetapi hook belum pernah diuji di HP. Siap-siap membaca `modloader.log`.
> Tolong laporkan hasilnya.

## Pasang

1. Pasang AML di game, lalu salin `libmodloader.so` ke folder mods AML (biasanya `Android/data/com.rockstargames.gtasa/mods/`).
2. Buat mod di `Android/data/com.rockstargames.gtasa/files/modloader/<Nama Mod>/`
   (`files/` adalah folder tempat game menyimpan file loose-nya).
3. Jalankan game, lalu baca `files/modloader/modloader.log`: isinya apa saja yang ditemukan dan diganti.

Saat pertama jalan dibuat `modloader/modloader.ini` (profil/prioritas, sintaks sama dengan upstream, lihat
`doc/Profiles.md`) dan `modloader/.data/config.ini`.

## Membuat mod

Mod = satu folder di dalam `modloader/`. File boleh diletakkan di mana saja di dalamnya:

| File di dalam mod | Hasil |
|---|---|
| `handling.cfg`, `american.gxt`, `gta.dat`, `fonts.dat`... file apa pun yang dibaca game dari foldernya | std.data: game diberi file *milik mod* saat memintanya. Dicocokkan lewat path (`data/handling.cfg`), atau kalau path tidak ada, lewat nama file saja. |
| `gta3.img`, arsip lain, file lain yang dibaca game lewat API file-nya | Sama, seluruh file diganti. |
| `*.dff` `*.txd` `*.col` `*.ifp` `*.rrr` `*.scm` dan `*.ipl` biner (`bnry`) | std.stream: mengganti entri bernama sama di dalam arsip IMG game (`infernus.dff` mengganti `infernus.dff` di `gta3.img`), folder tempatnya tidak penting. Nama yang tidak ada di arsip mana pun didaftarkan game sebagai entri baru (model baru tetap perlu baris IDE). Nama entri maksimal 23 karakter. |
| `*.ide` / `*.ipl` teks yang bukan pengganti file game | std.data menambahkannya ke `gta.dat` (IDE sebelum IPL pertama, IPL di akhir). |

* Mod berprioritas tertinggi menang (default 50, atur di `modloader.ini`); jika prioritas sama, yang urutan abjadnya terakhir menang.
* Folder berawalan `.` dan mod `_ignore` dilewati; semua daftar *Ignore/Include/ExclusiveMods* bekerja seperti aslinya.
* Untuk file loose yang namanya sama dengan jenis std.stream (mis. `anim/ped.ifp`), letakkan di path yang sama di dalam mod.

## Yang sudah dan belum

* ✔ `std.data` (ganti file, tambah IDE/IPL), ✔ `std.stream` (ganti/tambah entri IMG), ◐ ganti `text/*.gxt` (file biasa).
* ✘ merge baris demi baris `handling.cfg` dkk., ✘ merge `.fxt`, ✘ arsip `.img` baru, ✘ hot reload, ✘ plugin upstream lain (asi, scm, fx...).

## Build

GitHub Actions mengerjakan semuanya (`.github/workflows/android.yml`): tes unit, lalu build NDK; zip berisi
`libmodloader.so` jadi artifact (dan lampiran release untuk tag `android-v*`). Manual: lihat `README.md`.

## Cara kerja singkat

* **std.data** meng-hook `OS_FileOpen` (semua pembacaan file game lewat sini). Kalau mod punya file yang diminta, game
  diminta membuka file mod itu; nama berawalan `/` dibuka `NvFOpen` relatif terhadap root penyimpanan eksternal.
* **std.stream** membungkus file mod menjadi satu arsip IMG (`modloader/.data/cache/stream.img`) dan meng-hook
  `CStreaming::LoadCdDirectory`. Game hanya menyimpan entri *pertama* untuk tiap nama, jadi entri yang diganti di-patch
  setelah arsip aslinya dimuat; `ms_streamingBufferSize` dinaikkan bila perlu.
* Sebelum hook dipasang, byte awal fungsi dibandingkan dengan build yang dianalisis; build yang tidak cocok cukup
  mematikan fiturnya (dan menulis baris di log).

## Verifikasi

* Tes unit (222 pemeriksaan, termasuk jalur hook lewat host/game palsu) dijalankan native (juga dengan ASan/UBSan) dan sebagai ARM32 di qemu; kompilasi GCC 13 dan clang 18 tanpa warning.
* `python3 android/tools/verify_libgtasa.py libGTASA.so` memeriksa library terhadap kebutuhan port. Untuk
  `libGTASA.so` yang dikirim (sha256 `46c1a97f...06b9db`, build 2.00 yang juga membawa section Fastman92) semua simbol,
  fingerprint dan ukuran objek cocok.
* Perilaku fungsi yang di-hook dibaca dari disassembly library tersebut.
* **Belum**: dijalankan di perangkat, build NDK (dikerjakan oleh workflow).
