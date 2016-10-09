#include <util/platform.h>
#include <util/dstr.h>
#include <obs-module.h>
#include <jansson.h>

static void fill_servers(obs_property_t *servers_prop, json_t *service, const char *name);
static void initialize_output(json_t *root,
	obs_data_t *video_settings, obs_data_t *audio_settings);

struct ftl_beam {
	char *server, *key;
};

static const char *ftl_beam_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Beam FTL Service");
}

static void ftl_beam_update(void *data, obs_data_t *settings)
{
	struct ftl_beam *service = data;

	bfree(service->server);
	bfree(service->key);

	service->server = bstrdup(obs_data_get_string(settings, "server"));
	service->key    = bstrdup(obs_data_get_string(settings, "key"));
}

static void ftl_beam_destroy(void *data)
{
	struct ftl_beam *service = data;

	bfree(service->server);
	bfree(service->key);
	bfree(service);
}

static void *ftl_beam_create(obs_data_t *settings, obs_service_t *service)
{
	struct ftl_beam *data = bzalloc(sizeof(struct ftl_beam));
	ftl_beam_update(data, settings);

	UNUSED_PARAMETER(service);
	return data;
}

static json_t *open_json_file(const char *file)
{
	char         *file_data = os_quick_read_utf8_file(file);
	json_error_t error;
	json_t       *root;
	json_t       *list;
	int          format_ver;

	if (!file_data)
		return NULL;

	root = json_loads(file_data, JSON_REJECT_DUPLICATES, &error);
	bfree(file_data);

	if (!root) {
		blog(LOG_WARNING, "ftl-beam.c: [open_json_file] "
			"Error reading JSON file (%d): %s",
			error.line, error.text);
		return NULL;
	}

	return root;
}

static json_t *get_ingest_servers(void)
{
	char *file;
	json_t *root = NULL, *ingests = NULL;

	file = obs_module_config_path("services.json");
	if (file) {
		root = open_json_file(file);
		bfree(file);
	}

	if (!root) {
		file = obs_module_file("services.json");
		if (file) {
			root = open_json_file(file);
			bfree(file);
		}
	}

	ingests = json_object_get(root, "ingests");
	if (ingests)
		json_incref(ingests);
	json_decref(root);

	if (!ingests) {
		blog(LOG_WARNING, "ftl-beam.c: [open_json_file] "
			"No ingests list");
		return NULL;
	}

	return ingests;
}

static obs_properties_t *ftl_beam_properties(void *unused)
{
	json_t *ingests = NULL;
	UNUSED_PARAMETER(unused);

	obs_properties_t *ppts = obs_properties_create();
	obs_property_t *p;

	obs_properties_add_list(ppts, "server", obs_module_text("Server"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	ingests = get_ingest_servers();
	
	if (ingests) {
		fill_servers(obs_properties_get(ppts, "server"), ingests, NULL);
	}

	obs_properties_add_text(ppts, "key", obs_module_text("StreamKey"),
			OBS_TEXT_PASSWORD);
	return ppts;
}

static const char *ftl_beam_url(void *data)
{
	struct ftl_beam *service = data;
	return service->server;
}

static const char *ftl_beam_key(void *data)
{
	struct ftl_beam *service = data;
	return service->key;
}

static json_t *open_services_file(void)
{
	char *file;
	json_t *root = NULL;

	file = obs_module_config_path("services.json");
	if (file) {
		root = open_json_file(file);
		bfree(file);
	}

	if (!root) {
		file = obs_module_file("services.json");
		if (file) {
			root = open_json_file(file);
			bfree(file);
		}
	}

	return root;
}

static void ftl_beam_apply_settings(void *data,
		obs_data_t *video_settings, obs_data_t *audio_settings)
{
	struct ftl_beam *service = data;

	json_t             *root = open_services_file();

	if (root) {
		initialize_output(root, video_settings,
				audio_settings);
		json_decref(root);
	}
}

static void apply_video_encoder_settings(obs_data_t *settings,
	json_t *recommended)
{
	json_t *item = json_object_get(recommended, "keyint");
	if (item && json_is_integer(item)) {
		int keyint = (int)json_integer_value(item);
		obs_data_set_int(settings, "keyint_sec", keyint);
	}

	obs_data_set_string(settings, "rate_control", "CBR");

	item = json_object_get(recommended, "profile");
	if (item && json_is_string(item)) {
		const char *profile = json_string_value(item);
		obs_data_set_string(settings, "profile", profile);
	}

	item = json_object_get(recommended, "max video bitrate");
	if (item && json_is_integer(item)) {
		int max_bitrate = (int)json_integer_value(item);
		if (obs_data_get_int(settings, "bitrate") > max_bitrate) {
			obs_data_set_int(settings, "bitrate", max_bitrate);
			obs_data_set_int(settings, "buffer_size", max_bitrate);
		}
	}

	item = json_object_get(recommended, "x264opts");
	if (item && json_is_string(item)) {
		const char *x264_settings = json_string_value(item);
		const char *cur_settings =
			obs_data_get_string(settings, "x264opts");
		struct dstr opts;

		dstr_init_copy(&opts, cur_settings);
		if (!dstr_is_empty(&opts))
			dstr_cat(&opts, " ");
		dstr_cat(&opts, x264_settings);

		obs_data_set_string(settings, "x264opts", opts.array);
		dstr_free(&opts);
	}
}

static void apply_audio_encoder_settings(obs_data_t *settings,
	json_t *recommended)
{
	json_t *item;

	item = json_object_get(recommended, "max audio bitrate");
	if (item && json_is_integer(item)) {
		int max_bitrate = (int)json_integer_value(item);
		if (obs_data_get_int(settings, "bitrate") > max_bitrate)
			obs_data_set_int(settings, "bitrate", max_bitrate);
	}

	item = json_object_get(recommended, "audio sample rate");
	if (item && json_is_integer(item)) {
		int sample_rate = (int)json_integer_value(item);
		if (obs_data_get_int(settings, "sample_rate") != sample_rate)
			obs_data_set_int(settings, "sample_rate", sample_rate);
	}
}

static void initialize_output(json_t *root,
	obs_data_t *video_settings, obs_data_t *audio_settings)
{
	json_t        *recommended;

	recommended = json_object_get(root, "recommended");
	if (!recommended)
		return;

	if (video_settings)
		apply_video_encoder_settings(video_settings, recommended);
	if (audio_settings)
		apply_audio_encoder_settings(audio_settings, recommended);
}

static inline const char *get_string_val(json_t *service, const char *key)
{
	json_t *str_val = json_object_get(service, key);
	if (!str_val || !json_is_string(str_val))
		return NULL;

	return json_string_value(str_val);
}

static void fill_servers(obs_property_t *servers_prop, json_t *ingests, const char *name) {
	json_t *ingest;
	size_t index;

	obs_property_list_clear(servers_prop);

	json_array_foreach(ingests, index, ingest) {
		const char *name = get_string_val(ingest, "name");
		const char *host = get_string_val(ingest, "host");

		if (!name || !host)
			continue;

		obs_property_list_add_string(servers_prop, name, host);
	}
}

struct obs_service_info ftl_beam_service = {
	.id             = "ftl_beam",
	.get_name       = ftl_beam_name,
	.create         = ftl_beam_create,
	.destroy        = ftl_beam_destroy,
	.update         = ftl_beam_update,
	.get_properties = ftl_beam_properties,
	.get_url        = ftl_beam_url,
	.get_key        = ftl_beam_key,
	.apply_encoder_settings = ftl_beam_apply_settings
};