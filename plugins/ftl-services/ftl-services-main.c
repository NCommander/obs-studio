#include <util/text-lookup.h>
#include <util/threading.h>
#include <util/platform.h>
#include <util/dstr.h>
#include <obs-module.h>
#include <file-updater/file-updater.h>

#include "ftl-format-ver.h"
#include "lookup-config.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("ftl-services", "en-US")

#define FTL_SERVICES_LOG_STR "[ftl-services plugin] "
#define FTL_SERVICES_VER_STR "ftl-services plugin (libobs " OBS_VERSION ")"

extern struct obs_service_info ftl_beam_service;

static update_info_t *update_info = NULL;

static bool confirm_service_file(void *param, struct file_download_data *file)
{
	if (astrcmpi(file->name, "services.json") == 0) {
		obs_data_t *data;
		int format_version;

		data = obs_data_create_from_json((char*)file->buffer.array);
		if (!data)
			return false;

		format_version = (int)obs_data_get_int(data, "format_version");
		obs_data_release(data);

		if (format_version != FTL_SERVICES_FORMAT_VERSION)
			return false;
	}

	UNUSED_PARAMETER(param);
	return true;
}

bool obs_module_load(void)
{
	char *local_dir = obs_module_file("");
	char *cache_dir = obs_module_config_path("");

	if (cache_dir) {
		update_info = update_info_create(
				FTL_SERVICES_LOG_STR,
				FTL_SERVICES_VER_STR,
				FTL_SERVICES_URL,
				local_dir,
				cache_dir,
				confirm_service_file, NULL);
	}

	bfree(local_dir);
	bfree(cache_dir);

	obs_register_service(&ftl_beam_service);
	return true;
}

void obs_module_unload(void)
{
	update_info_destroy(update_info);
}
