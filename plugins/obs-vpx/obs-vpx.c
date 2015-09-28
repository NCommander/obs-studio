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

#define do_log(level, format, ...) \
	blog(level, "[vpx encoder: '%s'] " format, \
			obs_encoder_get_name(obsvpx->encoder), ##__VA_ARGS__)

#define warn(format, ...)  do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...)  do_log(LOG_INFO,    format, ##__VA_ARGS__)
#define debug(format, ...) do_log(LOG_DEBUG,   format, ##__VA_ARGS__)

/* ------------------------------------------------------------------------- */

struct obs_vpx {
	obs_encoder_t          *encoder;

	DARRAY(uint8_t)        packet_data;

	uint8_t                *extra_data;
	uint8_t                *sei;

	size_t                 extra_data_size;
	size_t                 sei_size;

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

}

static obs_properties_t *obs_vpx_props(void *unused)
{
	UNUSED_PARAMETER(unused);

	obs_properties_t *props = obs_properties_create();

	return props;
}

static void obs_vpx_video_info(void *data, struct video_scale_info *info);


static bool obs_vpx_update(void *data, obs_data_t *settings)
{

}

static void *obs_vpx_create(obs_data_t *settings, obs_encoder_t *encoder)
{
	struct obs_vpx *obsvpx = bzalloc(sizeof(struct obs_vpx));
	obsvpx->encoder = encoder;

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
