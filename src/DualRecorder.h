/*
DualRecorder - core recording engine (no Qt dependency).

Setiap slot merekam VIDEO & AUDIO terpisah:
  1. Video per slot: Membuat private scene (obs_scene_create_private) berisi
     source "window_capture" target, disambungkan ke obs_view_t mandiri,
     lalu di-encode dengan video encoder x264 per slot (mis. Slot A = Chrome,
     Slot B = Antigravity IDE / Zoom).
  2. Audio per slot: WASAPI application audio capture ("wasapi_process_output_capture")
     dikunci ke Track 5 (Slot A) dan Track 6 (Slot B). Audio murni terisolasi.
  3. Output .mp4 terpisah untuk masing-masing slot.
*/

#pragma once

#include <obs.h>
#include <string>
#include <functional>

struct SlotConfig {
	std::string label;       // nama tampilan, mis. "Kelas Zoom"
	std::string windowValue; // string window (Title:Class:Exe)
	std::string outputPath;  // path file .mp4 tujuan
};

class DualRecorder {
public:
	DualRecorder();
	~DualRecorder();

	bool Start(const SlotConfig &slotA, const SlotConfig &slotB, std::string &outError);
	void Stop();
	bool IsRecording() const { return recording_; }

	std::function<void(const std::string &reason)> OnUnexpectedStop;

private:
	struct Slot {
		obs_scene_t *scene = nullptr;          // private scene per slot
		obs_source_t *video_source = nullptr;  // window_capture
		obs_source_t *audio_source = nullptr;  // wasapi_process_output_capture
		obs_view_t *view = nullptr;            // private view per slot
		video_t *video = nullptr;              // private video output per slot
		obs_encoder_t *video_encoder = nullptr;// x264 video encoder
		obs_encoder_t *audio_encoder = nullptr;// AAC audio encoder
		obs_output_t *output = nullptr;        // mp4 output
		size_t mixer_idx = 0;
	};

	bool StartSlot(Slot &slot, const SlotConfig &cfg, size_t mixerIdx, int slotIndex, std::string &outError);
	void StopSlot(Slot &slot);

	Slot slots_[2];
	bool recording_ = false;

	static void HandleOutputStop(void *data, calldata_t *params);
};
