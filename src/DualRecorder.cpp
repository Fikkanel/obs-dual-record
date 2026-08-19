#include "DualRecorder.h"

#include <util/windows/window-helpers.h>
#include <graphics/vec2.h>
#include <plugin-support.h>
#include <thread>
#include <chrono>

namespace {

// Mixer track index (0-based). Track 5 (index 4) & Track 6 (index 5).
constexpr size_t kMixerIdxSlotA = 4;
constexpr size_t kMixerIdxSlotB = 5;

// Global output channel yang tidak dipakai OBS UI.
constexpr uint32_t kOutputChannelBase = 13;

} // namespace

DualRecorder::DualRecorder() {}

DualRecorder::~DualRecorder()
{
	Stop();
}

void DualRecorder::HandleOutputStop(void *data, calldata_t *params)
{
	auto *self = static_cast<DualRecorder *>(data);
	long long code = 0;
	calldata_get_int(params, "code", &code);

	if (code != OBS_OUTPUT_SUCCESS && self->OnUnexpectedStop) {
		std::string reason = "Output berhenti tidak terduga (kode " + std::to_string(code) + ")";
		self->OnUnexpectedStop(reason);
	}
}

bool DualRecorder::StartSlot(Slot &slot, const SlotConfig &cfg, size_t mixerIdx, int slotIndex, std::string &outError)
{
	slot.mixer_idx = mixerIdx;

	obs_log(LOG_INFO, "[obs-dual-record] Starting slot %d: label='%s', window='%s', output='%s'",
		slotIndex, cfg.label.c_str(), cfg.windowValue.c_str(), cfg.outputPath.c_str());

	// ─── 1) VIDEO SOURCE: window_capture ───
	{
		obs_data_t *vid_settings = obs_data_create();
		obs_data_set_string(vid_settings, "window", cfg.windowValue.c_str());
		obs_data_set_int(vid_settings, "priority", WINDOW_PRIORITY_EXE);

		std::string vidName = "dual_rec_window_src_" + std::to_string(slotIndex);
		slot.video_source = obs_source_create_private("window_capture", vidName.c_str(), vid_settings);
		obs_data_release(vid_settings);

		if (!slot.video_source) {
			outError = "Gagal membuat video capture untuk '" + cfg.label + "'.";
			obs_log(LOG_ERROR, "[obs-dual-record] %s", outError.c_str());
			return false;
		}
		obs_log(LOG_INFO, "[obs-dual-record] Slot %d: window_capture source created OK", slotIndex);
	}

	// ─── 2) PRIVATE SCENE: tempat menaruh window_capture agar di-render GPU ───
	{
		std::string sceneName = "dual_rec_scene_" + std::to_string(slotIndex);
		slot.scene = obs_scene_create_private(sceneName.c_str());
		if (!slot.scene) {
			outError = "Gagal membuat private scene untuk '" + cfg.label + "'.";
			obs_log(LOG_ERROR, "[obs-dual-record] %s", outError.c_str());
			return false;
		}

		obs_sceneitem_t *item = obs_scene_add(slot.scene, slot.video_source);
		if (item) {
			obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
			obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);

			struct vec2 bounds = {1920.0f, 1080.0f};
			obs_sceneitem_set_bounds(item, &bounds);
		}
		obs_log(LOG_INFO, "[obs-dual-record] Slot %d: private scene created and window source added", slotIndex);
	}

	// ─── 3) PRIVATE OBS_VIEW & VIDEO OUTPUT ───
	{
		slot.view = obs_view_create();
		if (!slot.view) {
			outError = "Gagal membuat obs_view untuk '" + cfg.label + "'.";
			obs_log(LOG_ERROR, "[obs-dual-record] %s", outError.c_str());
			return false;
		}

		// Set scene source sebagai source utama di view ini (channel 0)
		obs_source_t *sceneSrc = obs_scene_get_source(slot.scene);
		obs_view_set_source(slot.view, 0, sceneSrc);

		// Ambil video handler untuk view ini (mirip cara OBS Virtual Cam bekerja)
		slot.video = obs_view_add(slot.view);
		if (!slot.video) {
			obs_log(LOG_WARNING, "[obs-dual-record] obs_view_add failed, fallback to main obs_get_video()");
			slot.video = obs_get_video();
		} else {
			obs_log(LOG_INFO, "[obs-dual-record] Slot %d: private video output created via obs_view_add", slotIndex);
		}
	}

	// ─── 4) AUDIO SOURCE: wasapi_process_output_capture ───
	{
		obs_data_t *src_settings = obs_data_create();
		obs_data_set_string(src_settings, "window", cfg.windowValue.c_str());
		obs_data_set_int(src_settings, "priority", WINDOW_PRIORITY_EXE);

		std::string audioName = "dual_rec_audio_" + std::to_string(slotIndex);
		slot.audio_source = obs_source_create_private("wasapi_process_output_capture",
							      audioName.c_str(), src_settings);
		obs_data_release(src_settings);

		if (!slot.audio_source) {
			outError = "Gagal membuat source audio untuk '" + cfg.label + "'.";
			obs_log(LOG_ERROR, "[obs-dual-record] %s", outError.c_str());
			return false;
		}

		// Batasi audio ini HANYA masuk ke mixer track spesifik (Track 5 atau Track 6).
		obs_source_set_audio_mixers(slot.audio_source, 1 << mixerIdx);

		// Pasang di global channel tinggi supaya OBS memproses audio ini.
		uint32_t ch = kOutputChannelBase + static_cast<uint32_t>(slotIndex);
		obs_set_output_source(ch, slot.audio_source);

		obs_log(LOG_INFO, "[obs-dual-record] Slot %d: audio source created, mixer bit=0x%x (Track %zu), channel=%u",
			slotIndex, (1 << mixerIdx), mixerIdx + 1, ch);
	}

	// ─── 5) VIDEO ENCODER PER SLOT (x264) ───
	{
		obs_data_t *video_settings = obs_data_create();
		obs_data_set_string(video_settings, "rate_control", "CBR");
		obs_data_set_int(video_settings, "bitrate", 6000);
		obs_data_set_int(video_settings, "keyint_sec", 2);

		std::string encName = "dual_rec_vid_enc_" + std::to_string(slotIndex);
		slot.video_encoder = obs_video_encoder_create("obs_x264", encName.c_str(),
							      video_settings, nullptr);
		obs_data_release(video_settings);

		if (!slot.video_encoder) {
			outError = "Gagal membuat video encoder untuk '" + cfg.label + "'.";
			obs_log(LOG_ERROR, "[obs-dual-record] %s", outError.c_str());
			return false;
		}

		obs_encoder_set_video(slot.video_encoder, slot.video);
		obs_log(LOG_INFO, "[obs-dual-record] Slot %d: video encoder created", slotIndex);
	}

	// ─── 6) AUDIO ENCODER AAC ───
	{
		obs_data_t *aac_settings = obs_data_create();
		obs_data_set_int(aac_settings, "bitrate", 160);

		std::string aacName = "dual_rec_aac_" + std::to_string(slotIndex);
		slot.audio_encoder = obs_audio_encoder_create("ffmpeg_aac", aacName.c_str(),
							      aac_settings, mixerIdx, nullptr);
		obs_data_release(aac_settings);

		if (!slot.audio_encoder) {
			outError = "Gagal membuat encoder audio untuk '" + cfg.label + "'.";
			obs_log(LOG_ERROR, "[obs-dual-record] %s", outError.c_str());
			return false;
		}
		obs_encoder_set_audio(slot.audio_encoder, obs_get_audio());
		obs_log(LOG_INFO, "[obs-dual-record] Slot %d: audio encoder created for Track %zu",
			slotIndex, mixerIdx + 1);
	}

	// ─── 7) OUTPUT MP4 ───
	{
		obs_data_t *out_settings = obs_data_create();
		obs_data_set_string(out_settings, "path", cfg.outputPath.c_str());

		std::string outName = "dual_rec_output_" + std::to_string(slotIndex);
		slot.output = obs_output_create("mp4_output", outName.c_str(), out_settings, nullptr);
		obs_data_release(out_settings);

		if (!slot.output) {
			outError = "Gagal membuat output rekaman untuk '" + cfg.label + "'.";
			obs_log(LOG_ERROR, "[obs-dual-record] %s", outError.c_str());
			return false;
		}

		obs_output_set_video_encoder(slot.output, slot.video_encoder);
		obs_output_set_audio_encoder(slot.output, slot.audio_encoder, 0);

		signal_handler_t *sh = obs_output_get_signal_handler(slot.output);
		signal_handler_connect(sh, "stop", &DualRecorder::HandleOutputStop, this);

		if (!obs_output_start(slot.output)) {
			const char *err = obs_output_get_last_error(slot.output);
			outError = "Gagal memulai rekaman '" + cfg.label + "'" +
				   (err ? std::string(": ") + err : "");
			obs_log(LOG_ERROR, "[obs-dual-record] %s", outError.c_str());
			return false;
		}

		obs_log(LOG_INFO, "[obs-dual-record] Slot %d: recording started -> %s",
			slotIndex, cfg.outputPath.c_str());
	}

	return true;
}

void DualRecorder::StopSlot(Slot &slot)
{
	if (slot.output) {
		signal_handler_t *sh = obs_output_get_signal_handler(slot.output);
		signal_handler_disconnect(sh, "stop", &DualRecorder::HandleOutputStop, this);

		if (obs_output_active(slot.output)) {
			obs_output_stop(slot.output);

			// WAITING FOR MP4 FLUSH:
			int waitCount = 0;
			while (obs_output_active(slot.output) && waitCount < 100) {
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				waitCount++;
			}
			obs_log(LOG_INFO, "[obs-dual-record] Output finalized and stopped properly (waited %d ms)", waitCount * 50);
		}

		obs_output_release(slot.output);
		slot.output = nullptr;
	}

	if (slot.video_encoder) {
		obs_encoder_release(slot.video_encoder);
		slot.video_encoder = nullptr;
	}

	if (slot.audio_encoder) {
		obs_encoder_release(slot.audio_encoder);
		slot.audio_encoder = nullptr;
	}

	if (slot.view) {
		if (slot.video) {
			obs_view_remove(slot.view);
			slot.video = nullptr;
		}
		obs_view_set_source(slot.view, 0, nullptr);
		obs_view_destroy(slot.view);
		slot.view = nullptr;
	}

	if (slot.scene) {
		obs_scene_release(slot.scene);
		slot.scene = nullptr;
	}

	if (slot.video_source) {
		obs_source_release(slot.video_source);
		slot.video_source = nullptr;
	}

	if (slot.audio_source) {
		for (uint32_t ch = kOutputChannelBase; ch < kOutputChannelBase + 4; ch++) {
			obs_source_t *src = obs_get_output_source(ch);
			if (src) {
				bool match = (src == slot.audio_source);
				obs_source_release(src);
				if (match) {
					obs_set_output_source(ch, nullptr);
					break;
				}
			}
		}
		obs_source_release(slot.audio_source);
		slot.audio_source = nullptr;
	}
}

bool DualRecorder::Start(const SlotConfig &slotA, const SlotConfig &slotB, std::string &outError)
{
	if (recording_) {
		outError = "Sudah ada rekaman yang berjalan.";
		return false;
	}

	obs_log(LOG_INFO, "[obs-dual-record] Starting dual recording...");

	if (!StartSlot(slots_[0], slotA, kMixerIdxSlotA, 0, outError)) {
		StopSlot(slots_[0]);
		StopSlot(slots_[1]);
		return false;
	}

	if (!StartSlot(slots_[1], slotB, kMixerIdxSlotB, 1, outError)) {
		StopSlot(slots_[0]);
		StopSlot(slots_[1]);
		return false;
	}

	recording_ = true;
	obs_log(LOG_INFO, "[obs-dual-record] Dual recording started successfully");
	return true;
}

void DualRecorder::Stop()
{
	if (!recording_)
		return;

	obs_log(LOG_INFO, "[obs-dual-record] Stopping dual recording...");

	StopSlot(slots_[0]);
	StopSlot(slots_[1]);

	recording_ = false;
	obs_log(LOG_INFO, "[obs-dual-record] Dual recording stopped");
}
