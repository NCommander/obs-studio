/******************************************************************************
		Copyright (C) 2015 by Michael Casadevall <michael@mcprohosting.com>

		This program is free software: you can redistribute it and/or modify
		it under the terms of the GNU General Public License as published by
		the Free Software Foundation, either version 2 of the License, or
		(at your option) any later version.

		This program is distributed in the hope that it will be useful,

		but WITHOUT ANY WARRANTY; without even the implied warranty of
		MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
		GNU General Public License for more details.

		You should have received a copy of the GNU General Public License
		along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include <stdio.h>
#include <util/dstr.h>
#include <util/darray.h>
#include <util/platform.h>
#include <obs-module.h>

#include "vpx/vpx_encoder.h"
#include "vpx/vp8cx.h"

#define do_log(level, format, ...) \
	blog(level, "[vpx encoder: '%s'] " format, \
			obs_encoder_get_name(obsvpx->encoder), ##__VA_ARGS__)

#define error(format, ...)  do_log(LOG_ERROR, format, ##__VA_ARGS__)
#define warn(format, ...)  do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...)  do_log(LOG_INFO,    format, ##__VA_ARGS__)
#define debug(format, ...) do_log(LOG_DEBUG,   format, ##__VA_ARGS__)

/* ------------------------------------------------------------------------- */

struct obs_vpx {
	obs_encoder_t		*encoder;
	vpx_codec_ctx_t 	*vpx_ctx;
	vpx_codec_iface_t	*vpx_iface;
	vpx_codec_enc_cfg_t	*vpx_enc_cfg;
	vpx_codec_flags_t 	*vpx_enc_flags;

	DARRAY(uint8_t)		packet_data;

	uint8_t 			*extra_data;
	uint8_t 			*sei;

	size_t				extra_data_size;
	size_t 				sei_size;

	os_performance_token_t *performance_token;
};

/* ------------------------------------------------------------------------- */

static const char *obs_vpx_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "vpx";
}

static void obs_vpx_destroy(void *data)
{

}

static void obs_vpx_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "codec", "vp8");
}

#define TEXT_CODEC    obs_module_text("Codec")

static obs_properties_t *obs_vpx_props(void *unused)
{
	UNUSED_PARAMETER(unused);

	obs_properties_t *props = obs_properties_create();
	obs_property_t *list;

	/**
	 * For codex, we only return VP8 for now. We can encode VP9 with roughly
	 * the same setup, but VP9 is a very bad choice for realtime encoding
	 */

	list = obs_properties_add_list(props, "codec", TEXT_CODEC,
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(list, "vp8", "vp8");

	return props;
}

static bool update_settings(struct obs_vpx *obsvpx, obs_data_t *settings)
{
	char *codec		= bstrdup(obs_data_get_string(settings, "codec"));
	vpx_codec_iface_t* encoder;
	vpx_codec_err_t error_code;

	/* Dump out configuration information to console */
	blog(LOG_INFO, "---------------------------------");
	if (codec && *codec)	info("codec: %s", codec);

	/**
	 * To setup the VP8 encoder, we need to do the following:
	 *
	 * 1. Point the correct codex initializer to vpx_codec_iface
	 *
	 * 2. Call vpx_codec_enc_config_default, and then populate the structure
	 *    with all the settings we have interfaces for
	 *
	 * 3. Initialize the encoder with the codex and config structs
	 */

	/* Step 1: Get the codex initializer */

	if (strncmp (codec, "vp8", 3) == 0) {
		encoder = vpx_codec_vp8_cx();
	} else {
		error("Unknown codex configuration!");
		goto fail;
	}

	/* Step 2: Build a default config */
	error_code = vpx_codec_enc_config_default(encoder,
											  &obsvpx->vpx_enc_cfg,
											  0);

	if (error_code != VPX_CODEC_OK) {
		error("Building default config for libvpx failed");
		goto fail;
	}
	return true;

	fail:
	return false;
}

static void obs_vpx_video_info(void *data, struct video_scale_info *info);


static bool obs_vpx_update(void *data, obs_data_t *settings)
{

}

static void *obs_vpx_create(obs_data_t *settings, obs_encoder_t *encoder)
{
	struct obs_vpx *obsvpx = bzalloc(sizeof(struct obs_vpx));
	obsvpx->encoder = encoder;

	/* Unlike h264, we don't have an initialization step */
	if (!update_settings(obsvpx, settings)) {
		warn("vpx failed to load settings; something probably is wrong");
	}

	/* MC: The prototype declares void; why are we returning an object? */
	return obsvpx;
}

static bool obs_vpx_encode(void *data, struct encoder_frame *frame,
		struct encoder_packet *packet, bool *received_packet)
{

}

static bool obs_vpx_extra_data(void *data, uint8_t **extra_data, size_t *size)
{

}

static bool obs_vpx_sei(void *data, uint8_t **sei, size_t *size)
{

}

static void obs_vpx_video_info(void *data, struct video_scale_info *info)
{

}

struct obs_encoder_info obs_vpx_encoder = {
	.id             = "obs_vpx",
	.type           = OBS_ENCODER_VIDEO,
	.codec          = "vpx",
	.get_name       = obs_vpx_getname,
	.create         = obs_vpx_create,
	.destroy        = obs_vpx_destroy,
	.encode         = obs_vpx_encode,
	.update         = obs_vpx_update,
	.get_properties = obs_vpx_props,
	.get_defaults   = obs_vpx_defaults,
	.get_extra_data = obs_vpx_extra_data,
	.get_sei_data   = obs_vpx_sei,
	.get_video_info = obs_vpx_video_info
};
