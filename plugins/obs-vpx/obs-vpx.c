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
#include "vpx/vpx_image.h"
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
	vpx_codec_ctx_t 	vpx_ctx;
	vpx_codec_iface_t	*vpx_iface;
	vpx_codec_enc_cfg_t	vpx_enc_cfg;
	vpx_codec_flags_t 	vpx_enc_flags;

	bool				is_img_allocated;
	vpx_img_fmt_t 		img_fmt;
	vpx_image_t 		img;

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
	struct obs_vpx *obsvpx = data;

	if (obsvpx) {
		if (obsvpx->is_img_allocated == true) {
			vpx_img_free(&obsvpx->img);
		}
	}
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

/**
 * libvpx uses a common structure to define configuration parameters between
 * all of the codecs (VP8-VP10) it supports. However, it doesn't mean that all
 * codecs support all options, so depending on what we support defines what
 * we set.
 *
 * Right now this only supports VP8, but I've marked the VP8 specific options
 * that I'm aware of in the source so this can be updated if and when VP9
 * can actually encode in realtime without a machine catching fire
 **/

static void update_params(struct obs_vpx *obsvpx,obs_data_t *settings)
{
	video_t *video = obs_encoder_video(obsvpx->encoder);
	const struct video_output_info *voi = video_output_get_info(video);
	struct video_scale_info info;

	vpx_codec_enc_cfg_t encoder_config = obsvpx->vpx_enc_cfg;

	/* Configuration information from OBS */
	int width        = (int)obs_encoder_get_width(obsvpx->encoder);
	int height       = (int)obs_encoder_get_height(obsvpx->encoder);

	/* Settings common to all codecs */
	encoder_config.g_w = width;
	encoder_config.g_h = height;

	/**
	 * I'm not sure if this a bug, or just plain weirdness from x264, but
	 * VPx requires that the framerate numerator be greater then the the
	 * denominator.
	 *
	 * x264 appears to work the other way: this is what I get in terms of
	 * debug info from x264:
	 *
	 * fps_num:     30
	 * fps_den:     1
	 *
	 * As such, we'll flip OBSes values to they're upside down so libvpx gets
	 * values it considers sane.
	 **/

	encoder_config.g_timebase.den = voi->fps_num;
	encoder_config.g_timebase.num = voi->fps_den;

	/* VP8 doesn't use colorspaces; VP9 and 10 do; fill that info here  */

	/**
	 * libvpx defines a LOT of different image formats. However, the images
	 * supported depend on the codec. VP8 is very limited; here's the
	 * actual code that says what's allowed for VP8
	 *
	 *  From vp8/vp8_cx_iface.c:267
	 *  switch (img->fmt)
 	 *	{
	 *	case VPX_IMG_FMT_YV12:
	 *	case VPX_IMG_FMT_I420:
	 *	case VPX_IMG_FMT_VPXI420:
	 *	case VPX_IMG_FMT_VPXYV12:
	 * 		break;
	 *	default:
	 *		ERROR("Invalid image format. Only YV12 and I420 images are supported");
     *  }
	 *
	 * As of writing, OBS supports NV12, I420, and I444 output, but it pops a
	 * warning when anything but NV12 is selected that it will decrease
	 * performance and said settings only exist for recording. For an alpha
	 * this is fine, but I need to discuss with obs-devel how best to handle
	 * this.
	 **/

	obsvpx->img_fmt = VPX_IMG_FMT_I420;

	info("settings:\n"
	     "\tfps_num:     %d\n"
	     "\tfps_den:     %d\n"
	     "\twidth:       %d\n"
	     "\theight:      %d\n"
		 "\timg_fmt      %d\n",
		 encoder_config.g_timebase.num,
		 encoder_config.g_timebase.den,
		 encoder_config.g_w,
		 encoder_config.g_h,
		 obsvpx->img_fmt);
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
	 *
	 * 4. Initialize the image buffer
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

	/* Step 3: Configure based on the type of codec we're using */
	update_params(obsvpx, settings);

	/* Step 4: Create an image buffer for the encoder */
	if (obsvpx->is_img_allocated == true) {
		vpx_img_free(&obsvpx->img);
	}

	/**
	 * Magic number explanation: 32
	 *
	 * The last number in vpx_img_alloc refers to the alignment of
	 * the resulting memory block. Example documentation uses 1, but
	 * this causes degraded performance because it misaligns the stack
	 * (and on non-x86 platforms, would cause a bus fault).
	 *
	 * Found via these links.
	 * https://code.google.com/p/webm/issues/detail?id=441:
	 * https://chromium-review.googlesource.com/#/c/23091/1/vpxenc.c
	 **/

	vpx_img_alloc(&obsvpx->img, obsvpx->img_fmt, obsvpx->vpx_enc_cfg.g_w,
										 	 	 obsvpx->vpx_enc_cfg.g_h,
												 32);
	return true;

	fail:
	return false;
}

static void obs_vpx_video_info(void *data, struct video_scale_info *info);


static bool obs_vpx_update(void *data, obs_data_t *settings)
{
	struct obs_vpx *obsvpx = data;
	bool success = update_settings(obsvpx, settings);

	if (!success) {
		warn("reconfiguration of obs-vpx failed\n");
		return false;
	}

	return true;
}

static void *obs_vpx_create(obs_data_t *settings, obs_encoder_t *encoder)
{
	struct obs_vpx *obsvpx = bzalloc(sizeof(struct obs_vpx));
	obsvpx->is_img_allocated = false;
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

/* VP8 is very limited in what it will accept */
static inline bool valid_format(enum video_format format)
{
	return format == VIDEO_FORMAT_I420;
}

static void obs_vpx_video_info(void *data, struct video_scale_info *info)
{
	struct obs_vpx *obsvpx = data;
	enum video_format pref_format;

	pref_format = obs_encoder_get_preferred_video_format(obsvpx->encoder);

	if (!valid_format(pref_format)) {
		pref_format = valid_format(info->format) ?
			info->format : VIDEO_FORMAT_NV12;
	}

	info->format = pref_format;
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
