# Panduan Instalasi Plugin "Dual Record" di OBS Studio

File plugin **`obs-dual-record.dll`** sudah **BERHASIL DICOMPILE** dan siap dipasang ke OBS Studio.

---

## 📍 Lokasi File Sumber (Hasil Build)

- **File Plugin DLL:**
  ```
  c:\FIKKAN\project\Plugins OBS\obs-dual-record\obs-dual-record\build_x64\RelWithDebInfo\obs-dual-record.dll
  ```

- **Folder Data / Resources:**
  ```
  c:\FIKKAN\project\Plugins OBS\obs-dual-record\obs-dual-record\data\
  ```

---

## 🛠️ Langkah Pemasangan ke OBS Studio

1. **Tutup OBS Studio** jika sedang berjalan.

2. **Pasang File DLL Plugin**:
   - Salin file `obs-dual-record.dll` dari folder sumber di atas.
   - Buka folder: `C:\Program Files\obs-studio\obs-plugins\64bit\`
   - Paste file `obs-dual-record.dll` di sana. *(Klik **Continue** jika Windows meminta konfirmasi Administrator)*.

3. **Pasang Folder Data Plugin**:
   - Buka folder: `C:\Program Files\obs-studio\data\obs-plugins\`
   - **Buat folder baru** di dalamnya, beri nama: `obs-dual-record` *(Klik **Continue** jika diminta konfirmasi Administrator)*.
   - Buka folder `obs-dual-record` baru tersebut.
   - Salin seluruh isi dari folder project `data\` di atas dan paste ke dalam folder `C:\Program Files\obs-studio\data\obs-plugins\obs-dual-record\`.

4. **Buka OBS Studio**.
   - Panel **"Dual Record"** akan otomatis muncul di OBS Studio.
   - *Catatan:* Jika belum terlihat, buka menu atas OBS: **View** -> **Docks** -> centang **Dual Record**.
