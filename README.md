# 🎥 Dual Record for OBS Studio

![Platform](https://img.shields.io/badge/Platform-Windows_10%2F11_x64-blue)
![OBS Version](https://img.shields.io/badge/OBS_Studio-v28%2B%20%7C%20v31%2B%20%7C%20v32%2B-orange)
![License](https://img.shields.io/badge/License-GPLv2-green)

Plugin OBS Studio (Windows) untuk merekam **2 aplikasi meeting / jendela sekaligus** (misalnya Zoom + Google Meet, atau Chrome + Antigravity IDE) ke **2 file `.mp4` terpisah** secara bersamaan. Masing-masing file menyimpan tampilan video jendela dan audio terisolasi yang **tidak saling bentrok**.

Muncul sebagai panel dock baru di dalam OBS Studio bernama **"Dual Record"**.

---

## 📦 Download & Cara Instalasi Cepat

### 1. Download Plugin Siap Pakai:
👉 **[📥 Klik Di Sini Untuk Mengunduh obs-dual-record-plugin.zip](https://github.com/Fikkanel/obs-dual-record/raw/main/obs-dual-record-plugin.zip)**

### 2. Cara Pasang di OBS Studio:
1. Pastikan **OBS Studio** sudah ditutup.
2. Ekstrak file **`obs-dual-record-plugin.zip`** yang sudah diunduh.
3. Salin/Copy 2 folder hasil ekstrak (`obs-plugins` dan `data`) lalu **Paste ke folder instalasi OBS**:
   ```text
   C:\Program Files\obs-studio\
   ```
4. Buka **OBS Studio**.
5. Di menu bar bagian atas, klik **Docks** → centang **Dual Record**. Panel plugin akan langsung muncul di layar OBS!

---

## ✨ Fitur Utama

- **🎬 Video Per-Slot Terisolasi**: Setiap slot memiliki *Private GPU Scene* (`obs_scene_create_private`) mandiri yang menangkap tampilan jendela target (`window_capture`), dialirkan via `obs_view_t` dan di-encode oleh encoder x264 independen ke file `.mp4` masing-masing.
- **🔊 Audio Per-Slot Terisolasi**: Menggunakan WASAPI Process Loopback (`wasapi_process_output_capture`) yang dikunci ke **Track 5 (Slot A)** dan **Track 6 (Slot B)** OBS, sehingga suara aplikasi A dan B murni terpisah dan tidak mencemari audio utama OBS.
- **🖥️ Live GPU Preview**: Menampilkan pratinjau live video real-time untuk jendela yang dipilih pada Slot A dan Slot B di dalam dock UI.
- **📊 Real-time Audio Level Meter**: Indikator VU-meter audio (hijau → kuning → merah) di bawah preview tiap slot untuk mendeteksi suara secara real-time.
- **📐 Flexible Wide Layout**: Tampilan UI dock side-by-side 50:50 yang elegan dan proporsional.
- **💾 Asynchronous Output Flush Guard**: Mencegah file `.mp4` corrupt saat perekaman dihentikan, memastikan file MP4 dapat diputar dengan lancar.

---

## 🚀 Cara Penggunaan

1. Buka 2 aplikasi meeting / video yang ingin direkam (misalnya **Zoom** dan **Microsoft Edge**).
2. Di panel **Dual Record**:
   - Klik **Refresh daftar jendela**.
   - Pilih Jendela A pada **Slot A** (misal: Zoom).
   - Pilih Jendela B pada **Slot B** (misal: Edge / Meet).
3. Periksa **Live Preview** dan **Bar Audio Level**: pastikan sinyal visual & indikator warna bergerak saat audio diputar.
4. Tentukan folder penyimpanan tujuan pada bidang **Simpan ke:**.
5. Klik **Start Dual Record** untuk mulai merekam kedua file sekaligus.
6. Klik **Stop Dual Record** setelah selesai. Dua file `.mp4` akan tersimpan di folder tujuan.

> 💡 **Tips Pemisahan Audio Windows**:  
> Windows WASAPI menangkap audio berdasarkan **Proses (.exe)**. Untuk memastikan audio 100% terpisah tanpa bentrok:
> - Gunakan 2 aplikasi terpisah (contoh: **Chrome** untuk Slot A dan **Microsoft Edge / Zoom** untuk Slot B).
> - Jika ingin menggunakan browser yang sama untuk 2 slot, gunakan 2 **User Profile / Guest Window** Chrome yang berbeda agar Windows menetapkan Process ID terpisah.

---

## 🛠️ Persyaratan Sistem

- **Windows 10 versi 2004 (build 19041) atau lebih baru** (wajib untuk fitur WASAPI Process Audio Capture).
- **OBS Studio v28.0+** (diuji dan dikompilasi untuk **OBS Studio 31.x / 32.x 64-bit**).

---

## 🏗️ Cara Build dari Source Code (Bagi Developer)

### Prasyarat Build:
- **Visual Studio 2022 / 2026 Build Tools** (dengan C++ Desktop Development Workload).
- **CMake 3.28+**.

### Langkah Kompilasi:
Buka PowerShell / Command Prompt di folder repository ini, lalu jalankan:

```powershell
# 1. Konfigurasi CMake
cmake -G "Visual Studio 18 2026" -A x64 -B build_x64 -DCMAKE_PREFIX_PATH="C:/Program Files/obs-studio" -DCMAKE_TLS_VERIFY=0

# 2. Build Plugin DLL (RelWithDebInfo)
cmake --build build_x64 --config RelWithDebInfo --target obs-dual-record
```

---

## 📄 Lisensi

Distributed under the **GPL-2.0 License**. See `LICENSE` for more information.
