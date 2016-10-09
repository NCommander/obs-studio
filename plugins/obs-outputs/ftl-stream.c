/******************************************************************************
    Copyright (C) 2014 by Hugh Bailey <obs.jim@gmail.com>

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

#include <obs-module.h>
#include <obs-avc.h>
#include <util/platform.h>
#include <util/circlebuf.h>
#include <util/dstr.h>
#include <util/threading.h>
#include <inttypes.h>
//#include "librtmp/rtmp.h"
//#include "librtmp/log.h"
#include "ftl.h"
#include "flv-mux.h"
#include "net-if.h"

#ifdef _WIN32
#include <Iphlpapi.h>
#else
#include <sys/ioctl.h>
#define INFINITE 0xFFFFFFFF
#endif

#define do_log(level, format, ...) \
	blog(level, "[ftl stream: '%s'] " format, \
			obs_output_get_name(stream->output), ##__VA_ARGS__)

#define warn(format, ...)  do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...)  do_log(LOG_INFO,    format, ##__VA_ARGS__)
#define debug(format, ...) do_log(LOG_DEBUG,   format, ##__VA_ARGS__)

#define OPT_DROP_THRESHOLD "drop_threshold_ms"
#define OPT_MAX_SHUTDOWN_TIME_SEC "max_shutdown_time_sec"
#define OPT_BIND_IP "bind_ip"

//#define TEST_FRAMEDROPS

typedef struct _nalu_t {
	int len;
	int dts_usec;
	int send_marker_bit;
	uint8_t *data;
}nalu_t;

typedef struct _frame_of_nalus_t {
	nalu_t nalus[100];
	int total;
	int complete_frame;
}frame_of_nalus_t;

struct ftl_stream {
	obs_output_t     *output;

	pthread_mutex_t  packets_mutex;
	struct circlebuf packets;
	bool             sent_headers;

	volatile bool    connecting;
	pthread_t        connect_thread;
	pthread_t        status_thread;

	volatile bool    active;
	volatile bool    disconnected;
	pthread_t        send_thread;

	int              max_shutdown_time_sec;

	os_sem_t         *send_sem;
	os_event_t       *stop_event;
	uint64_t         stop_ts;

	struct dstr      path, path_ip;
	uint32_t         channel_id;
	struct dstr      username, password;
	struct dstr      encoder_name;
	struct dstr      bind_ip;

	/* frame drop variables */
	int64_t          drop_threshold_usec;
	int64_t          min_drop_dts_usec;
	int              min_priority;

	int64_t          last_dts_usec;

	uint64_t         total_bytes_sent;
	int              dropped_frames;

	ftl_handle_t	    ftl_handle;
	ftl_ingest_params_t params;
	uint32_t         scale_width, scale_height, width, height;
	frame_of_nalus_t coded_pic_buffer;
};

void log_libftl_messages(ftl_log_severity_t log_level, const char * message);
static bool init_connect(struct ftl_stream *stream);
static void *connect_thread(void *data);
static void *status_thread(void *data);

void log_test(ftl_log_severity_t log_level, const char * message) {
	//fprintf(stderr, "libftl message: %s\n", message);
	blog(log_level, "[ftl stream: '%s']", message);
	return;
}

static const char *ftl_stream_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("FTLStream");
}

static void log_ftl(int level, const char *format, va_list args)
{
//	if (level > RTMP_LOGWARNING)
//		return;

	blogva(LOG_INFO, format, args);
}

static inline size_t num_buffered_packets(struct ftl_stream *stream);

static inline void free_packets(struct ftl_stream *stream)
{
	size_t num_packets;

	pthread_mutex_lock(&stream->packets_mutex);

	num_packets = num_buffered_packets(stream);
	if (num_packets)
		info("Freeing %d remaining packets", (int)num_packets);

	while (stream->packets.size) {
		struct encoder_packet packet;
		circlebuf_pop_front(&stream->packets, &packet, sizeof(packet));
		obs_free_encoder_packet(&packet);
	}
	pthread_mutex_unlock(&stream->packets_mutex);
}

static inline bool stopping(struct ftl_stream *stream)
{
	return os_event_try(stream->stop_event) != EAGAIN;
}

static inline bool connecting(struct ftl_stream *stream)
{
	return os_atomic_load_bool(&stream->connecting);
}

static inline bool active(struct ftl_stream *stream)
{
	return os_atomic_load_bool(&stream->active);
}

static inline bool disconnected(struct ftl_stream *stream)
{
	return os_atomic_load_bool(&stream->disconnected);
}

static void ftl_stream_destroy(void *data)
{
	struct ftl_stream *stream = data;
	ftl_status_t status_code;

	info("ftl_stream_destroy\n");

	if (stopping(stream) && !connecting(stream)) {
		pthread_join(stream->send_thread, NULL);

	} else if (connecting(stream) || active(stream)) {
		if (stream->connecting) {
			info("wait for connect_thread to terminate");
			pthread_join(stream->status_thread, NULL);
			pthread_join(stream->connect_thread, NULL);
			info("wait for connect_thread to terminate: done");
		}

		stream->stop_ts = 0;
		os_event_signal(stream->stop_event);

		if (active(stream)) {
			os_sem_post(stream->send_sem);
			obs_output_end_data_capture(stream->output);
			pthread_join(stream->send_thread, NULL);
		}
	}

	info("ingest destroy");
	if ((status_code = ftl_ingest_destroy(&stream->ftl_handle)) != FTL_SUCCESS) {
		info("Failed to destroy from ingest %d\n", status_code);
	}

	if (stream) {
		free_packets(stream);
		dstr_free(&stream->path);
		dstr_free(&stream->path_ip);
		dstr_free(&stream->username);
		dstr_free(&stream->password);
		dstr_free(&stream->encoder_name);
		dstr_free(&stream->bind_ip);
		os_event_destroy(stream->stop_event);
		os_sem_destroy(stream->send_sem);
		pthread_mutex_destroy(&stream->packets_mutex);
		circlebuf_free(&stream->packets);
		bfree(stream);
	}
}

static void *ftl_stream_create(obs_data_t *settings, obs_output_t *output)
{
	ftl_status_t status_code;	
	struct ftl_stream *stream = bzalloc(sizeof(struct ftl_stream));
	info("ftl_stream_create\n");
	
	stream->output = output;
	pthread_mutex_init_value(&stream->packets_mutex);
	
	ftl_init();

	if (pthread_mutex_init(&stream->packets_mutex, NULL) != 0)
		goto fail;
	if (os_event_init(&stream->stop_event, OS_EVENT_TYPE_MANUAL) != 0)
		goto fail;

	stream->coded_pic_buffer.total = 0;
	stream->coded_pic_buffer.complete_frame = 0;

	UNUSED_PARAMETER(settings);
	return stream;

fail:
	return NULL;
}

static void ftl_stream_stop(void *data, uint64_t ts)
{
	struct ftl_stream *stream = data;
	info("ftl_stream_stop\n");

	if (stopping(stream))
		return;

	if (connecting(stream)) {
		pthread_join(stream->status_thread, NULL);
		pthread_join(stream->connect_thread, NULL);
	}

	stream->stop_ts = ts / 1000ULL;
	os_event_signal(stream->stop_event);

	if (active(stream)) {
		if (stream->stop_ts == 0)
			os_sem_post(stream->send_sem);
	}
}

static inline bool get_next_packet(struct ftl_stream *stream,
		struct encoder_packet *packet)
{
	bool new_packet = false;

	pthread_mutex_lock(&stream->packets_mutex);
	if (stream->packets.size) {
		circlebuf_pop_front(&stream->packets, packet,
				sizeof(struct encoder_packet));
		new_packet = true;
	}
	pthread_mutex_unlock(&stream->packets_mutex);

	return new_packet;
}

static int avc_get_video_frame(struct ftl_stream *stream, struct encoder_packet *packet, bool is_header, size_t idx) {

	int consumed = 0;
	int len = packet->size;
	nalu_t *nalu;

	unsigned char *video_stream = packet->data;

	while (consumed < packet->size) {

		if (stream->coded_pic_buffer.total >= sizeof(stream->coded_pic_buffer.nalus) / sizeof(stream->coded_pic_buffer.nalus[0])) {
			warn("ERROR: cannot continue, nalu buffers are full\n");
			return -1;
		}

		nalu = &stream->coded_pic_buffer.nalus[stream->coded_pic_buffer.total];

		if (is_header) {
			if (consumed == 0) {
				video_stream += 6; //first 6 bytes are some obs header with part of the sps
				consumed += 6;
			}
			else {
				video_stream += 1; //another spacer byte of 0x1
				consumed += 1;
			}

			len = video_stream[0] << 8 | video_stream[1];
			video_stream += 2;
			consumed += 2;
		}
		else {
			len = video_stream[0] << 24 | video_stream[1] << 16 | video_stream[2] << 8 | video_stream[3];

			if (len > (packet->size - consumed)) {
				warn("ERROR: got len of %d but packet only has %d left\n", len, (packet->size - consumed));
			}

			consumed += 4;
			video_stream += 4;
		}

		consumed += len;

		uint8_t nalu_type = video_stream[0] & 0x1F;
		uint8_t nri = (video_stream[0] >> 5) & 0x3;

		int send_marker_bit = (consumed >= packet->size) && !is_header;

		if ((nalu_type != 12 && nalu_type != 6 && nalu_type != 9) || nri) {
			nalu->data = video_stream;
			nalu->len = len;
			nalu->send_marker_bit = 0;
			stream->coded_pic_buffer.total++;
		}

		video_stream += len;
	}

	if (!is_header) {
		stream->coded_pic_buffer.nalus[stream->coded_pic_buffer.total - 1].send_marker_bit = 1;
	}

	return 0;
}

static int send_packet(struct ftl_stream *stream,
		struct encoder_packet *packet, bool is_header, size_t idx)
{
	enum obs_encoder_type type;
	int     recv_size = 0;
	int     ret = 0;
	int bytes_sent = 0;

	if (packet->type == OBS_ENCODER_VIDEO) {

		stream->coded_pic_buffer.total = 0;
		avc_get_video_frame(stream, packet, is_header, idx);

		int i;
		for (i = 0; i < stream->coded_pic_buffer.total; i++) {
			int send_marker_bit = (i + 1) == stream->coded_pic_buffer.total;
			nalu_t *nalu = &stream->coded_pic_buffer.nalus[i];
			bytes_sent += ftl_ingest_send_media(&stream->ftl_handle, FTL_VIDEO_DATA, nalu->data, nalu->len, nalu->send_marker_bit);
		}
	}
	else if (packet->type == OBS_ENCODER_AUDIO) {
		bytes_sent += ftl_ingest_send_media(&stream->ftl_handle, FTL_AUDIO_DATA, packet->data, packet->size, 0);
	}
	else {
		warn("Got packet type %d\n", packet->type);
	}

	stream->total_bytes_sent += bytes_sent;

	obs_free_encoder_packet(packet);
	return ret;
}

static inline bool send_headers(struct ftl_stream *stream);

static void *send_thread(void *data)
{
	struct ftl_stream *stream = data;
	ftl_status_t status_code;

	os_set_thread_name("ftl-stream: send_thread");

	while (os_sem_wait(stream->send_sem) == 0) {
		struct encoder_packet packet;

		if (stopping(stream) && stream->stop_ts == 0) {
			break;
		}

		if (!get_next_packet(stream, &packet))
			continue;

		if (stopping(stream)) {
			if (packet.sys_dts_usec >= (int64_t)stream->stop_ts) {
				obs_free_encoder_packet(&packet);
				break;
			}
		}

		/*sends sps/pps on every key frame as this is typically required for webrtc*/
		if (packet.keyframe) {
			if (!send_headers(stream)) {
				os_atomic_set_bool(&stream->disconnected, true);
				break;
			}
		}

		if (send_packet(stream, &packet, false, packet.track_idx) < 0) {
			os_atomic_set_bool(&stream->disconnected, true);
			break;
		}
	}

	if (disconnected(stream)) {
		info("Disconnected from %s (%s)", stream->path.array, stream->path_ip.array);
	} else {
		info("User stopped the stream");
	}

	if (!stopping(stream)) {
		pthread_detach(stream->send_thread);
		obs_output_signal_stop(stream->output, OBS_OUTPUT_DISCONNECTED);
	} else {
		obs_output_end_data_capture(stream->output);
	}

	info("ingest disconnect");
	if ((status_code = ftl_ingest_disconnect(&stream->ftl_handle)) != FTL_SUCCESS) {
		printf("Failed to disconnect from ingest %d\n", status_code);
	}
	
	free_packets(stream);
	os_event_reset(stream->stop_event);
	os_atomic_set_bool(&stream->active, false);
	stream->sent_headers = false;
	return NULL;
}
/*
static bool send_meta_data(struct ftl_stream *stream, size_t idx, bool *next)
{
	uint8_t *meta_data;
	size_t  meta_data_size;
	bool    success = true;

	*next = flv_meta_data(stream->output, &meta_data,
			&meta_data_size, false, idx);

	if (*next) {
		success = RTMP_Write(&stream->rtmp, (char*)meta_data,
				(int)meta_data_size, (int)idx) >= 0;
		bfree(meta_data);
	}

	return success;
}

static bool send_audio_header(struct ftl_stream *stream, size_t idx,
		bool *next)
{
	obs_output_t  *context  = stream->output;
	obs_encoder_t *aencoder = obs_output_get_audio_encoder(context, idx);
	uint8_t       *header;

	struct encoder_packet packet   = {
		.type         = OBS_ENCODER_AUDIO,
		.timebase_den = 1
	};

	if (!aencoder) {
		*next = false;
		return true;
	}

	obs_encoder_get_extra_data(aencoder, &header, &packet.size);
	packet.data = bmemdup(header, packet.size);
	return send_packet(stream, &packet, true, idx) >= 0;
}
*/

static bool send_video_header(struct ftl_stream *stream)
{
	obs_output_t  *context  = stream->output;
	obs_encoder_t *vencoder = obs_output_get_video_encoder(context);
	uint8_t       *header;
	size_t        size;

	struct encoder_packet packet   = {
		.type         = OBS_ENCODER_VIDEO,
		.timebase_den = 1,
		.keyframe     = true
	};

	obs_encoder_get_extra_data(vencoder, &header, &size);
	packet.size = obs_parse_avc_header(&packet.data, header, size);
	return send_packet(stream, &packet, true, 0) >= 0;
}

static inline bool send_headers(struct ftl_stream *stream)
{
	stream->sent_headers = true;

	if (!send_video_header(stream))
		return false;

	return true;
}

static inline bool reset_semaphore(struct ftl_stream *stream)
{
	os_sem_destroy(stream->send_sem);
	return os_sem_init(&stream->send_sem, 0) == 0;
}

#ifdef _WIN32
#define socklen_t int
#endif

static int init_send(struct ftl_stream *stream)
{
	int ret;
	
	reset_semaphore(stream);

	ret = pthread_create(&stream->send_thread, NULL, send_thread, stream);
	if (ret != 0) {
		//RTMP_Close(&stream->rtmp);
		warn("Failed to create send thread");
		return OBS_OUTPUT_ERROR;
	}

	os_atomic_set_bool(&stream->active, true);

	obs_output_begin_data_capture(stream->output, 0);

	return OBS_OUTPUT_SUCCESS;
}

#ifdef _WIN32
/*
static void win32_log_interface_type(struct ftl_stream *stream)
{
	RTMP *rtmp = &stream->rtmp;
	MIB_IPFORWARDROW route;
	uint32_t dest_addr, source_addr;
	char hostname[256];
	HOSTENT *h;

	if (rtmp->Link.hostname.av_len >= sizeof(hostname) - 1)
		return;

	strncpy(hostname, rtmp->Link.hostname.av_val, sizeof(hostname));
	hostname[rtmp->Link.hostname.av_len] = 0;

	h = gethostbyname(hostname);
	if (!h)
		return;

	dest_addr = *(uint32_t*)h->h_addr_list[0];

	if (rtmp->m_bindIP.addrLen == 0)
		source_addr = 0;
	else if (rtmp->m_bindIP.addr.ss_family == AF_INET)
		source_addr = (*(struct sockaddr_in*)&rtmp->m_bindIP)
			.sin_addr.S_un.S_addr;
	else
		return;

	if (!GetBestRoute(dest_addr, source_addr, &route)) {
		MIB_IFROW row;
		memset(&row, 0, sizeof(row));
		row.dwIndex = route.dwForwardIfIndex;

		if (!GetIfEntry(&row)) {
			uint32_t speed =row.dwSpeed / 1000000;
			char *type;
			struct dstr other = {0};

			if (row.dwType == IF_TYPE_ETHERNET_CSMACD) {
				type = "ethernet";
			} else if (row.dwType == IF_TYPE_IEEE80211) {
				type = "802.11";
			} else {
				dstr_printf(&other, "type %lu", row.dwType);
				type = other.array;
			}

			info("Interface: %s (%s, %lu mbps)", row.bDescr, type,
					speed);

			dstr_free(&other);
		}
	}
}
*/
#endif

static int lookup_ingest_ip(const char *ingest_location, char *ingest_ip) {
	struct hostent *remoteHost;
	struct in_addr addr;
	int retval = -1;
	ingest_ip[0] = '\0';

	remoteHost = gethostbyname(ingest_location);

	if (remoteHost) {
		int i = 0;
		if (remoteHost->h_addrtype == AF_INET)
		{
			while (remoteHost->h_addr_list[i] != 0) {
				addr.s_addr = *(u_long *)remoteHost->h_addr_list[i++];
				blog(LOG_INFO, "IP Address #%d of ingest is: %s\n", i, inet_ntoa(addr));

				/*only use the first ip found*/
				if (strlen(ingest_ip) == 0) {
					strcpy(ingest_ip, inet_ntoa(addr));
					retval = 0;
				}
			}
		}
	}

	return retval;
}

static int try_connect(struct ftl_stream *stream)
{
	ftl_status_t status_code;
	
	if (dstr_is_empty(&stream->path_ip)) {
		warn("URL is empty");
		return OBS_OUTPUT_BAD_PATH;
	}

	info("Connecting to FTL Ingest URL %s (%s)...", stream->path.array, stream->path_ip.array);

	stream->width = (int)obs_output_get_width(stream->output);
	stream->height = (int)obs_output_get_height(stream->output);

	if ((status_code = ftl_ingest_connect(&stream->ftl_handle)) != FTL_SUCCESS) {
		printf("Failed to connect to ingest %d\n", status_code);
		return OBS_OUTPUT_ERROR;
	}

	info("Connection to %s (%s) successful", stream->path.array, stream->path_ip.array);

	pthread_create(&stream->status_thread, NULL, status_thread, stream);

	return init_send(stream);
}

static bool ftl_stream_start(void *data)
{
	struct ftl_stream *stream = data;

	info("ftl_stream_start\n");

	if (!obs_output_can_begin_data_capture(stream->output, 0))
		return false;
	if (!obs_output_initialize_encoders(stream->output, 0))
		return false;

	os_atomic_set_bool(&stream->connecting, true);

	return pthread_create(&stream->connect_thread, NULL, connect_thread,
			stream) == 0;
}

static inline bool add_packet(struct ftl_stream *stream,
		struct encoder_packet *packet)
{
	circlebuf_push_back(&stream->packets, packet,
			sizeof(struct encoder_packet));
	stream->last_dts_usec = packet->dts_usec;
	return true;
}

static inline size_t num_buffered_packets(struct ftl_stream *stream)
{
	return stream->packets.size / sizeof(struct encoder_packet);
}

static void drop_frames(struct ftl_stream *stream)
{
	struct circlebuf new_buf            = {0};
	int              drop_priority      = 0;
	uint64_t         last_drop_dts_usec = 0;
	int              num_frames_dropped = 0;

	debug("Previous packet count: %d", (int)num_buffered_packets(stream));

	circlebuf_reserve(&new_buf, sizeof(struct encoder_packet) * 8);

	while (stream->packets.size) {
		struct encoder_packet packet;
		circlebuf_pop_front(&stream->packets, &packet, sizeof(packet));

		last_drop_dts_usec = packet.dts_usec;

		// do not drop audio data or video keyframes 
		if (packet.type          == OBS_ENCODER_AUDIO ||
		    packet.drop_priority == OBS_NAL_PRIORITY_HIGHEST) {
			circlebuf_push_back(&new_buf, &packet, sizeof(packet));

		} else {
			if (drop_priority < packet.drop_priority)
				drop_priority = packet.drop_priority;

			num_frames_dropped++;
			obs_free_encoder_packet(&packet);
		}
	}

	circlebuf_free(&stream->packets);
	stream->packets           = new_buf;
	stream->min_priority      = drop_priority;
	stream->min_drop_dts_usec = last_drop_dts_usec;

	stream->dropped_frames += num_frames_dropped;
	debug("New packet count: %d", (int)num_buffered_packets(stream));
}

static void check_to_drop_frames(struct ftl_stream *stream)
{
	struct encoder_packet first;
	int64_t buffer_duration_usec;

	if (num_buffered_packets(stream) < 5)
		return;

	circlebuf_peek_front(&stream->packets, &first, sizeof(first));

	//do not drop frames if frames were just dropped within this time
	if (first.dts_usec < stream->min_drop_dts_usec)
		return;

	// if the amount of time stored in the buffered packets waiting to be
	// sent is higher than threshold, drop frames 
	buffer_duration_usec = stream->last_dts_usec - first.dts_usec;

	if (buffer_duration_usec > stream->drop_threshold_usec) {
		drop_frames(stream);
		debug("dropping %" PRId64 " worth of frames",
				buffer_duration_usec);
	}
}

static bool add_video_packet(struct ftl_stream *stream,
		struct encoder_packet *packet)
{
//	check_to_drop_frames(stream);

	// if currently dropping frames, drop packets until it reaches the
	// desired priority 
	if (packet->priority < stream->min_priority) {
		stream->dropped_frames++;
		//return false;
	} else {
		stream->min_priority = 0;
	}

	return add_packet(stream, packet);
}


static void ftl_stream_data(void *data, struct encoder_packet *packet)
{
	struct ftl_stream    *stream = data;

//	info("ftl_stream_data\n");

	struct encoder_packet new_packet;
	bool                  added_packet = false;

	if (disconnected(stream) || !active(stream))
		return;

	if (packet->type == OBS_ENCODER_VIDEO)
		obs_parse_avc_packet(&new_packet, packet);
	else
		obs_duplicate_encoder_packet(&new_packet, packet);

	pthread_mutex_lock(&stream->packets_mutex);

	if (!disconnected(stream)) {
		added_packet = (packet->type == OBS_ENCODER_VIDEO) ?
			add_video_packet(stream, &new_packet) :
			add_packet(stream, &new_packet);
	}

	pthread_mutex_unlock(&stream->packets_mutex);

	if (added_packet)
		os_sem_post(stream->send_sem);
	else
		obs_free_encoder_packet(&new_packet);
}

static void ftl_stream_defaults(obs_data_t *defaults)
{
	UNUSED_PARAMETER(defaults);
	/*
	obs_data_set_default_int(defaults, OPT_DROP_THRESHOLD, 600);
	obs_data_set_default_int(defaults, OPT_MAX_SHUTDOWN_TIME_SEC, 5);
	obs_data_set_default_string(defaults, OPT_BIND_IP, "default");
	*/
	//info("ftl_stream_defaults\n");
}


static obs_properties_t *ftl_stream_properties(void *unused)
{

	//info("ftl_stream_properties\n");
	UNUSED_PARAMETER(unused);

	obs_properties_t *props = obs_properties_create();
		/*
	struct netif_saddr_data addrs = {0};
	obs_property_t *p;

	obs_properties_add_int(props, OPT_DROP_THRESHOLD,
			obs_module_text("RTMPStream.DropThreshold"),
			200, 10000, 100);

	p = obs_properties_add_list(props, OPT_BIND_IP,
			obs_module_text("RTMPStream.BindIP"),
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(p, obs_module_text("Default"), "default");

	netif_get_addrs(&addrs);
	for (size_t i = 0; i < addrs.addrs.num; i++) {
		struct netif_saddr_item item = addrs.addrs.array[i];
		obs_property_list_add_string(p, item.name, item.addr);
	}
	netif_saddr_data_free(&addrs);
*/
	return props;
}


static uint64_t ftl_stream_total_bytes_sent(void *data)
{
	struct ftl_stream *stream = data;

	return stream->total_bytes_sent;
}

static int ftl_stream_dropped_frames(void *data)
{
	struct ftl_stream *stream = data;
	//info("ftl_stream_dropped_frames\n");
	return stream->dropped_frames;
}



/*********************************************************************/
static void *status_thread(void *data)
{
	struct ftl_stream *stream = data;

	ftl_status_msg_t status;
	ftl_status_t status_code;

	while (!disconnected(stream)) {
		if ((status_code = ftl_ingest_get_status(&stream->ftl_handle, &status, INFINITE)) < 0) {
			blog(LOG_INFO, "ftl_ingest_get_status returned %d\n", status_code);
			os_sleep_ms(500);
			continue;
		}

		if (status.type == FTL_STATUS_EVENT && status.msg.event.type == FTL_STATUS_EVENT_TYPE_DISCONNECTED) {
			blog(LOG_INFO, "Disconnected from ingest for reason %d\n", status.msg.event.reason);

			if (status.msg.event.reason == FTL_STATUS_EVENT_REASON_API_REQUEST) {
				break;
			}

			//attempt reconnection
			blog(LOG_WARNING, "Reconnecting to Ingest\n");
			if ((status_code = ftl_ingest_connect(&stream->ftl_handle)) != FTL_SUCCESS) {
				blog(LOG_WARNING, "Failed to connect to ingest %d\n", status_code);
				obs_output_signal_stop(stream->output, OBS_OUTPUT_DISCONNECTED);
				return NULL;
			}
			blog(LOG_WARNING, "Done\n");

		}
		else {
			blog(LOG_INFO, "Status:  Got Status message of type %d\n", status.type);
		}
	}

	blog(LOG_INFO, "status_thread:  Exited");
	pthread_detach(stream->status_thread);
	return NULL;
}

static void *connect_thread(void *data)
{
	struct ftl_stream *stream = data;
	int ret;

	os_set_thread_name("ftl-stream: connect_thread");

	blog(LOG_WARNING, "ftl-stream: connect thread\n");

	if (!init_connect(stream)) {
		obs_output_signal_stop(stream->output, OBS_OUTPUT_BAD_PATH);
		return NULL;
	}

	ret = try_connect(stream);

	if (ret != OBS_OUTPUT_SUCCESS) {
		obs_output_signal_stop(stream->output, ret);
		info("Connection to %s (%s) failed: %d", stream->path.array, stream->path_ip.array, ret);
	}

	if (!stopping(stream))
		pthread_detach(stream->connect_thread);

	os_atomic_set_bool(&stream->connecting, false);
	return NULL;
}

void log_libftl_messages(ftl_log_severity_t log_level, const char * message)
{
	UNUSED_PARAMETER(log_level);
  blog(LOG_WARNING, "[libftl] %s", message);
}

static bool init_connect(struct ftl_stream *stream)
{
	obs_service_t *service;
	obs_data_t *settings;
	const char *bind_ip, *key;
	char tmp_ip[20];
	ftl_status_t status_code;


	info("init_connect\n");

	if (stopping(stream))
		pthread_join(stream->send_thread, NULL);

	free_packets(stream);

	service = obs_output_get_service(stream->output);
	if (!service)
		return false;

	os_atomic_set_bool(&stream->disconnected, false);
	stream->total_bytes_sent = 0;
	stream->dropped_frames   = 0;
	stream->min_drop_dts_usec= 0;
	stream->min_priority     = 0;

	settings = obs_output_get_settings(stream->output);
	obs_encoder_t *video_encoder = obs_output_get_video_encoder(stream->output);
	obs_data_t *video_settings = obs_encoder_get_settings(video_encoder);

	dstr_copy(&stream->path,     obs_service_get_url(service));
	lookup_ingest_ip(stream->path.array, tmp_ip);
	dstr_copy(&stream->path_ip, tmp_ip);
	key = obs_service_get_key(service);

	struct obs_video_info ovi;
	int fps_num = 30, fps_den = 1;
	if (obs_get_video_info(&ovi)) {
		fps_num = ovi.fps_num;
		fps_den = ovi.fps_den;
	}

	struct dstr version;
	dstr_printf(&version, "%d.%d.%d", LIBOBS_API_MAJOR_VER, LIBOBS_API_MINOR_VER, LIBOBS_API_PATCH_VER);

	stream->params.log_func = log_test;
	stream->params.stream_key = (char*)key;
	stream->params.video_codec = FTL_VIDEO_H264;
	stream->params.audio_codec = FTL_AUDIO_OPUS;
	stream->params.ingest_hostname = stream->path_ip.array;
	stream->params.vendor_name = "OBS Studio";
	stream->params.vendor_version = version.array;
	stream->params.fps_num = fps_num;
	stream->params.fps_den = fps_den;
	stream->params.video_kbps = (int)obs_data_get_int(video_settings, "bitrate");


	if ((status_code = ftl_ingest_create(&stream->ftl_handle, &stream->params)) != FTL_SUCCESS) {
		printf("Failed to create ingest handle %d\n", status_code);
		return -1;
	}

	dstr_copy(&stream->username, obs_service_get_username(service));
	dstr_copy(&stream->password, obs_service_get_password(service));
	dstr_depad(&stream->path);
	dstr_free(&version);
	/*	
	stream->drop_threshold_usec =
		(int64_t)obs_data_get_int(settings, OPT_DROP_THRESHOLD) * 1000;
	stream->max_shutdown_time_sec =
		(int)obs_data_get_int(settings, OPT_MAX_SHUTDOWN_TIME_SEC);
*/
	bind_ip = obs_data_get_string(settings, OPT_BIND_IP);
	dstr_copy(&stream->bind_ip, bind_ip);

	obs_data_release(settings);
	return true;
}

// Returns 0 on success
int map_ftl_error_to_obs_error(int status) {

	UNUSED_PARAMETER(status);
	/* Map FTL errors to OBS errors */
#if 0
	int ftl_to_obs_error_code = 0;
	switch (status) {
		case FTL_SUCCESS:
			break;
		case FTL_DNS_FAILURE:
			ftl_to_obs_error_code = OBS_OUTPUT_FTL_DNS_FAILURE;
			break;
		case FTL_CONNECT_ERROR:
			ftl_to_obs_error_code = OBS_OUTPUT_FTL_CONNECT_FAILURE;
			break;
		case FTL_OLD_VERSION:
			ftl_to_obs_error_code = OBS_OUTPUT_FTL_OLD_VERSION;
			break;
		case FTL_STREAM_REJECTED:
			ftl_to_obs_error_code = OBS_OUTPUT_FTL_STREAM_REJECTED;
			break;
		case FTL_UNAUTHORIZED:
			ftl_to_obs_error_code = OBS_OUTPUT_FTL_UNAUTHORIZED;
			break;
		case FTL_AUDIO_SSRC_COLLISION:
			/* SSRC collision, let's back up and try with a new audio SSRC */
			ftl_to_obs_error_code = OBS_OUTPUT_FTL_AUDIO_SSRC_COLLISION;
			break;
		case FTL_VIDEO_SSRC_COLLISION:
			ftl_to_obs_error_code = OBS_OUTPUT_FTL_VIDEO_SSRC_COLLISION;
			break;
		/* Non-specific failures, or internal Tachyon bug */
		default:
			/* Unknown FTL error */
			blog (LOG_ERROR, "tachyon error mapping needs to be updated!");
			ftl_to_obs_error_code = OBS_OUTPUT_ERROR;
	}

	return ftl_to_obs_error_code;
#endif
	return 0;
}
struct obs_output_info ftl_output_info = {
	.id                 = "ftl_output",
	.flags              = OBS_OUTPUT_AV |
	                      OBS_OUTPUT_ENCODED |
	                      OBS_OUTPUT_SERVICE,
	.get_name           = ftl_stream_getname,
	.create             = ftl_stream_create,
	.destroy            = ftl_stream_destroy,
	.start              = ftl_stream_start,
	.stop               = ftl_stream_stop,
	.encoded_packet     = ftl_stream_data,
	.get_defaults       = ftl_stream_defaults,
	.get_properties     = ftl_stream_properties,
	.get_total_bytes    = ftl_stream_total_bytes_sent,
	.get_dropped_frames = ftl_stream_dropped_frames
};
