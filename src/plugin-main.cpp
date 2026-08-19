/*
Dual Record for OBS
Plugin untuk merekam 2 aplikasi meeting (mis. Zoom + Google Meet) sekaligus,
masing-masing ke file .mp4 terpisah dengan audio yang tidak saling bentrok.
*/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include "DualRecordDock.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static DualRecordDock *g_dock = nullptr;

static void on_frontend_event(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING && g_dock) {
		g_dock->OnOBSFinishedLoading();
	}
}

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "Dual Record plugin loaded (version %s)", PLUGIN_VERSION);

	g_dock = new DualRecordDock();
	obs_frontend_add_dock_by_id("dual_record_dock", "Dual Record", g_dock);

	obs_frontend_add_event_callback(on_frontend_event, nullptr);

	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	obs_log(LOG_INFO, "Dual Record plugin unloaded");
}
