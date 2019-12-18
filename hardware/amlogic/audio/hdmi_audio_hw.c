/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "audio_hw_hdmi"
#define LOG_NDEBUG 0
//#define LOG_NDEBUG_FUNCTION
#ifdef LOG_NDEBUG_FUNCTION
#define LOGFUNC(...) ((void)0)
#else
#define LOGFUNC(...) (ALOGD(__VA_ARGS__))
#endif
//#define DEBUG_HWSYNC_PASSTHROUGH
#ifndef DEBUG_HWSYNC_PASSTHROUGH
#define DEBUG(...) ((void)0)
#else
#define DEBUG(...) (ALOGD(__VA_ARGS__))
#endif
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <utils/Timers.h>
#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>
#include <linux/ioctl.h>
#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>
#include <sound/asound.h>
#include <tinyalsa/asoundlib.h>

#include <audio_utils/resampler.h>
#include <audio_utils/echo_reference.h>
#include <hardware/audio_effect.h>
#include <audio_effects/effect_aec.h>

#include "audio_hw.h"
#include "audio_hwsync.h"
#include "hdmi_audio_hw.h"
#include "audio_hw_profile.h"
#include "audio_hw_utils.h"
extern int  aml_audio_hwsync_find_frame(struct aml_stream_out *out, const void *in_buffer, size_t in_bytes, uint64_t *cur_pts, int *outsize);
extern int  spdifenc_write(const void *buffer, size_t numBytes);
extern uint64_t  spdifenc_get_total();

extern int spdifenc_init(struct pcm *mypcm);

struct pcm_config pcm_config_out = {
    .channels = 2,
    .rate = MM_FULL_POWER_SAMPLING_RATE,
    .period_size = DEFAULT_PERIOD_SIZE,
    .period_count = PLAYBACK_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

struct pcm_config pcm_config_in = {
    .channels = 2,
    .rate = MM_FULL_POWER_SAMPLING_RATE,
    .period_size = DEFAULT_PERIOD_SIZE,
    .period_count = PLAYBACK_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

static void select_output_device(struct aml_audio_device *adev);
static void select_input_device(struct aml_audio_device *adev);
static int adev_set_voice_volume(struct audio_hw_device *dev, float volume);
static int do_input_standby(struct aml_stream_in *in);
static int do_output_standby(struct aml_stream_out *out);

extern void aml_audio_hwsync_clear_status(struct aml_stream_out *out);

static void select_output_device(struct aml_audio_device *adev)
{
    LOGFUNC("%s(mode=%d, out_device=%#x)", __FUNCTION__, adev->mode,
            adev->out_device);
}

static void select_input_device(struct aml_audio_device *adev)
{
    int mic_in = adev->in_device & AUDIO_DEVICE_IN_BUILTIN_MIC;
    int headset_mic = adev->in_device & AUDIO_DEVICE_IN_WIRED_HEADSET;
    LOGFUNC("~~~~ %s : in_device(%#x), mic_in(%#x), headset_mic(%#x)",
            __func__, adev->in_device, mic_in, headset_mic);
    return;
}

static void
force_all_standby(struct aml_audio_device *adev)
{
    struct aml_stream_in *in;
    struct aml_stream_out *out;

    LOGFUNC("%s(%p)", __FUNCTION__, adev);

    if (adev->active_output) {
        out = adev->active_output;
        pthread_mutex_lock(&out->lock);
        do_output_standby(out);
        pthread_mutex_unlock(&out->lock);
    }

    if (adev->active_input) {
        in = adev->active_input;
        pthread_mutex_lock(&in->lock);
        do_input_standby(in);
        pthread_mutex_unlock(&in->lock);
    }
}

static void
select_mode(struct aml_audio_device *adev)
{
    LOGFUNC("%s(out_device=%#x)", __FUNCTION__, adev->out_device);
    LOGFUNC("%s(in_device=%#x)", __FUNCTION__, adev->in_device);
    return;
    force_all_standby(adev);
    /* force earpiece route for in call state if speaker is the
    only currently selected route. This prevents having to tear
    down the modem PCMs to change route from speaker to earpiece
    after the ringtone is played, but doesn't cause a route
    change if a headset or bt device is already connected. If
    speaker is not the only thing active, just remove it from
    the route. We'll assume it'll never be used initally during
    a call. This works because we're sure that the audio policy
    manager will update the output device after the audio mode
    change, even if the device selection did not change. */
    if ((adev->out_device & AUDIO_DEVICE_OUT_ALL) == AUDIO_DEVICE_OUT_SPEAKER) {
        adev->in_device = AUDIO_DEVICE_IN_BUILTIN_MIC & ~AUDIO_DEVICE_BIT_IN;
    } else {
        adev->out_device &= ~AUDIO_DEVICE_OUT_SPEAKER;
    }
    select_output_device(adev);
    select_input_device(adev);
    return;
}


static int
check_output_stream(struct aml_stream_out *out)
{
    int ret = 0;
    unsigned int card = CARD_AMLOGIC_DEFAULT;
    unsigned int port = PORT_MM;
    int ext_card;
    ext_card = get_external_card(0);
    if (ext_card < 0) {
        card = CARD_AMLOGIC_DEFAULT;
    } else {
        card = ext_card;
    }
    out->config.start_threshold = PERIOD_SIZE * 2;
    out->config.avail_min = 0;  //SHORT_PERIOD_SIZE;
    return 0;
}

/* must be called with hw device and output stream mutexes locked */
static int start_output_stream(struct aml_stream_out *out)
{
    struct aml_audio_device *adev = out->dev;
    int card = CARD_AMLOGIC_DEFAULT;
    int port = PORT_MM;
    int ret = 0;
    int codec_type = get_codec_type(out->format);
    if (out->format == AUDIO_FORMAT_PCM && out->config.rate > 48000 && (out->flags & AUDIO_OUTPUT_FLAG_DIRECT)) {
        ALOGI("start output stream for high sample rate pcm for direct mode\n");
        codec_type = TYPE_PCM_HIGH_SR;
    }
    if (codec_type == AUDIO_FORMAT_PCM && out->config.channels >= 6 && (out->flags & AUDIO_OUTPUT_FLAG_DIRECT)) {
        ALOGI("start output stream for multi-channel pcm for direct mode\n");
        codec_type = TYPE_MULTI_PCM;
    }
    adev->active_output = out;
    if (adev->mode != AUDIO_MODE_IN_CALL) {
        /* FIXME: only works if only one output can be active at a time */
        select_output_device(adev);
    }
    LOGFUNC("%s(adev->out_device=%#x, adev->mode=%d)", __FUNCTION__,
            adev->out_device, adev->mode);
    card = get_aml_card();
    if (card < 0) {
        ALOGE("hdmi get aml card id failed \n");
        card = CARD_AMLOGIC_DEFAULT;
    }
    port = get_spdif_port();
    if (port < 0) {
        ALOGE("hdmi get aml card port  failed \n");
        card = PORT_MM;
    }
    ALOGI("hdmi sound card id %d,device id %d \n", card, port);
    if (out->config.channels == 6) {
        ALOGI("round 6ch to 8 ch output \n");
        /* our hw only support 8 channel configure,so when 5.1,hw mask the last two channels*/
        sysfs_set_sysfs_str("/sys/class/amhdmitx/amhdmitx0/aud_output_chs", "6:7");
        out->config.channels = 8;
    }
    /*
    8 channel audio only support 32 byte mode,so need convert them to
    PCM_FORMAT_S32_LE
    */
    if (out->config.channels == 8) {
        port = 0;
        out->config.format = PCM_FORMAT_S32_LE;
        adev->out_device = AUDIO_DEVICE_OUT_SPEAKER;
        ALOGI("[%s %d]8CH format output: set port/0 adev->out_device/%d\n",
              __FUNCTION__, __LINE__, AUDIO_DEVICE_OUT_SPEAKER);
    }
    LOGFUNC("------------open on board audio-------");
    if (getprop_bool("media.libplayer.wfd")) {
        out->config.period_size = PERIOD_SIZE;
    }
    switch (out->format) {
    case AUDIO_FORMAT_E_AC3:
        out->config.period_size = PERIOD_SIZE * 2;
        out->write_threshold = PLAYBACK_PERIOD_COUNT * PERIOD_SIZE * 2;
        out->config.start_threshold = PLAYBACK_PERIOD_COUNT * PERIOD_SIZE * 2;
        break;
    case AUDIO_FORMAT_DTS_HD:
    case AUDIO_FORMAT_TRUEHD:
        out->config.period_size = PERIOD_SIZE * 4 * 2;
        out->write_threshold = PLAYBACK_PERIOD_COUNT * PERIOD_SIZE * 4 * 2;
        out->config.start_threshold = PLAYBACK_PERIOD_COUNT * PERIOD_SIZE * 4 * 2;
        break;
    case AUDIO_FORMAT_PCM:
    default:
        out->config.period_size = PERIOD_SIZE;
        out->write_threshold = PLAYBACK_PERIOD_COUNT * PERIOD_SIZE;
        out->config.start_threshold = PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
    }
    out->config.avail_min = 0;
    if (codec_type != TYPE_DTS_HD)
        set_codec_type(codec_type);
    ALOGI("channels=%d---format=%d---period_count%d---period_size%d---rate=%d---",
          out->config.channels, out->config.format, out->config.period_count,
          out->config.period_size, out->config.rate);
    out->pcm = pcm_open(card, port, PCM_OUT /*| PCM_MMAP | PCM_NOIRQ */ ,
                        &(out->config));
    if (!pcm_is_ready(out->pcm)) {
        ALOGE("cannot open pcm_out driver: %s", pcm_get_error(out->pcm));
        pcm_close(out->pcm);
        adev->active_output = NULL;
        return -ENOMEM;
    }
#if 1
    if (codec_type_is_raw_data(codec_type) && !(out->flags & AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO)) {
        spdifenc_init(out->pcm);
        out->spdif_enc_init_frame_write_sum = out->frame_write_sum;
    }
#endif
    //out->frame_write_sum=0;
    out->codec_type = codec_type;
    out->bytes_write_total = 0;
    if (adev->hw_sync_mode == 1) {
        LOGFUNC("start_output_stream with hw sync enable\n");
    }
    return 0;
}

static int
check_input_parameters(uint32_t sample_rate, int format, int channel_count)
{
    LOGFUNC("%s(sample_rate=%d, format=%d, channel_count=%d)", __FUNCTION__,
            sample_rate, format, channel_count);

    if (format != AUDIO_FORMAT_PCM_16_BIT) {
        return -EINVAL;
    }

    if ((channel_count < 1) || (channel_count > 2)) {
        return -EINVAL;
    }

    switch (sample_rate) {
    case 8000:
    case 11025:
    case 16000:
    case 22050:
    case 24000:
    case 32000:
    case 44100:
    case 48000:
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

static size_t
get_input_buffer_size(uint32_t sample_rate, int format, int channel_count)
{
    size_t size;
    size_t device_rate;

    LOGFUNC("%s(sample_rate=%d, format=%d, channel_count=%d)", __FUNCTION__,
            sample_rate, format, channel_count);

    if (check_input_parameters(sample_rate, format, channel_count) != 0) {
        return 0;
    }

    /* take resampling into account and return the closest majoring
     multiple of 16 frames, as audioflinger expects audio buffers to
     be a multiple of 16 frames */
    size = (pcm_config_in.period_size * sample_rate) / pcm_config_in.rate;
    size = ((size + 15) / 16) * 16;

    return size * channel_count * sizeof(short);
}



static uint32_t
out_get_sample_rate(const struct audio_stream *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    if (out->config.rate > 0) {
        return out->config.rate;
    } else {
        return DEFAULT_OUT_SAMPLING_RATE;
    }
}

static int
out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    LOGFUNC("%s(%p, %d)", __FUNCTION__, stream, rate);

    return 0;
}

static size_t
out_get_buffer_size(const struct audio_stream *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;

    LOGFUNC("%s(out->config.rate=%d)", __FUNCTION__, out->config.rate);

    /* take resampling into account and return the closest majoring
     * multiple of 16 frames, as audioflinger expects audio buffers to
     * be a multiple of 16 frames
     */
    size_t size;
    switch (out->format) {
    case AUDIO_FORMAT_AC3:
    case AUDIO_FORMAT_DTS:
        if (out->flags & AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO) {
            size = 4 * PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
        } else {
            size = PLAYBACK_PERIOD_COUNT * PERIOD_SIZE / 2;
        }
        break;
    case AUDIO_FORMAT_E_AC3:
        if (out->flags & AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO) {
            size = 16 * PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
        } else {
            size = PERIOD_SIZE;    //2*PLAYBACK_PERIOD_COUNT*PERIOD_SIZE;
        }
        break;
    case AUDIO_FORMAT_DTS_HD:
    case AUDIO_FORMAT_TRUEHD:
        if (out->flags & AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO) {
            size = 16 * PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
        } else {
            size = 4 * PLAYBACK_PERIOD_COUNT * PERIOD_SIZE;
        }
        break;
    case AUDIO_FORMAT_PCM:
    default:
        size = PERIOD_SIZE;
    }
    size = ((size + 15) / 16) * 16;
    size = size * audio_stream_out_frame_size(&out->stream);
    DEBUG("format %x,buffer size %d\n", out->format, size);
    return size;
}

static audio_channel_mask_t
out_get_channels(const struct audio_stream *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    if (out->multich > 2) {
        if (out->multich == 6) {
            return AUDIO_CHANNEL_OUT_5POINT1;
        } else if (out->multich == 8) {
            return AUDIO_CHANNEL_OUT_7POINT1;
        }
    }
    if (out->config.channels == 1) {
        return AUDIO_CHANNEL_OUT_MONO;
    } else {
        return AUDIO_CHANNEL_OUT_STEREO;
    }
}

static audio_format_t
out_get_format(const struct audio_stream *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;

    return out->format;
}

static int
out_set_format(struct audio_stream *stream, int format)
{
    LOGFUNC("%s(%p)", __FUNCTION__, stream);

    return 0;
}

/* must be called with hw device and output stream mutexes locked */
static int
do_output_standby(struct aml_stream_out *out)
{
    struct aml_audio_device *adev = out->dev;
    if (!out->standby) {
        pcm_close(out->pcm);
        out->pcm = NULL;

        adev->active_output = 0;

        /* if in call, don't turn off the output stage. This will
         be done when the call is ended */
        if (adev->mode != AUDIO_MODE_IN_CALL) {
            /* FIXME: only works if only one output can be active at a time */

            //reset_mixer_state(adev->ar);
        }
        out->standby = 1;
    }
    ALOGI("clear out pause status\n");
    out->pause_status = false;
    return 0;
}

static int
out_standby(struct audio_stream *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    int status;

    LOGFUNC("%s(%p),out %p", __FUNCTION__, stream, out);

    pthread_mutex_lock(&out->dev->lock);
    pthread_mutex_lock(&out->lock);
    status = do_output_standby(out);
    set_codec_type(TYPE_PCM);
/* clear the hdmitx channel config to default */
    if (out->multich == 6) {
        sysfs_set_sysfs_str("/sys/class/amhdmitx/amhdmitx0/aud_output_chs", "0:0");
    }
    if (out->format != AUDIO_FORMAT_DTS_HD)
        set_codec_type(TYPE_PCM);
    pthread_mutex_unlock(&out->lock);
    pthread_mutex_unlock(&out->dev->lock);
    return status;
}

static int
out_dump(const struct audio_stream *stream, int fd)
{
    LOGFUNC("%s(%p, %d)", __FUNCTION__, stream, fd);
    return 0;
}
static int
out_flush(const struct audio_stream *stream)
{
    LOGFUNC("%s(%p)", __FUNCTION__, stream);
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&out->lock);
    do_output_standby(out);
    out->spdif_enc_init_frame_write_sum =  0;
    out->frame_write_sum  = 0;
    out->frame_skip_sum = 0;
    out->skip_frame = 3;
    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&out->lock);
    return 0;
}

static int
out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    struct aml_stream_in *in;
    struct str_parms *parms;
    char *str;
    char value[64] = {0};
    int ret, val = 0;
    bool force_input_standby = false;

    LOGFUNC("%s(kvpairs(%s), out_device=%#x)", __FUNCTION__, kvpairs,
            adev->out_device);
    parms = str_parms_create_str(kvpairs);

    ret =
        str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value,
                          sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        pthread_mutex_lock(&adev->lock);
        pthread_mutex_lock(&out->lock);
        if (((adev->out_device & AUDIO_DEVICE_OUT_ALL) != val) && (val != 0)) {
            if (out == adev->active_output) {
                do_output_standby(out);
                /* a change in output device may change the microphone selection */
                if (adev->active_input &&
                    adev->active_input->source ==
                    AUDIO_SOURCE_VOICE_COMMUNICATION) {
                    force_input_standby = true;
                }
                /* force standby if moving to/from HDMI */
                if (((val & AUDIO_DEVICE_OUT_AUX_DIGITAL) ^
                     (adev->out_device & AUDIO_DEVICE_OUT_AUX_DIGITAL)) ||
                    ((val & AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET) ^
                     (adev->out_device & AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET))) {
                    do_output_standby(out);
                }
            }
            adev->out_device &= ~AUDIO_DEVICE_OUT_ALL;
            adev->out_device |= val;
            select_output_device(adev);
        }
        pthread_mutex_unlock(&out->lock);
        if (force_input_standby) {
            in = adev->active_input;
            pthread_mutex_lock(&in->lock);
            do_input_standby(in);
            pthread_mutex_unlock(&in->lock);
        }
        pthread_mutex_unlock(&adev->lock);
        goto exit;
    }
    int sr = 0;
    ret = str_parms_get_int(parms, AUDIO_PARAMETER_STREAM_SAMPLING_RATE, &sr);
    if (ret >= 0) {
        if (sr > 0) {
            ALOGI("audio hw sampling_rate change from %d to %d \n",
                  DEFAULT_OUT_SAMPLING_RATE, sr);
            DEFAULT_OUT_SAMPLING_RATE = sr;
            pcm_config_out.rate = DEFAULT_OUT_SAMPLING_RATE;
            out->config.rate = DEFAULT_OUT_SAMPLING_RATE;
            pthread_mutex_lock(&adev->lock);
            pthread_mutex_lock(&out->lock);
            if (!out->standby && (out == adev->active_output)) {
                //do_output_standby (out);
                //start_output_stream (out);
                //out->standby = 0;
            }
            pthread_mutex_unlock(&adev->lock);
            pthread_mutex_unlock(&out->lock);

        }
        goto exit;
    }
    int frame_size = 0;
    ret =
        str_parms_get_int(parms, AUDIO_PARAMETER_STREAM_FRAME_COUNT,
                          &frame_size);
    if (ret >= 0) {
        if (frame_size > 0) {
            ALOGI("audio hw frame size change from %d to %d \n", PERIOD_SIZE,
                  frame_size);
            PERIOD_SIZE = frame_size;
            pcm_config_out.period_size = PERIOD_SIZE;
            out->config.period_size = PERIOD_SIZE;
            pthread_mutex_lock(&adev->lock);
            pthread_mutex_lock(&out->lock);
            if (!out->standby && (out == adev->active_output)) {
                //do_output_standby (out);
                //start_output_stream (out);
                //out->standby = 0;
            }
            pthread_mutex_unlock(&adev->lock);
            pthread_mutex_unlock(&out->lock);

        }
        goto exit;
    }

    ret = str_parms_get_str(parms, "hw_av_sync", value, sizeof(value));
    if (ret >= 0) {
        int hw_sync_id = atoi(value);
        unsigned char sync_enable = (hw_sync_id == 12345678) ? 1 : 0;
        ALOGI("(%p %p)set hw_sync_id %d,%s hw sync mode\n",
              out, adev->active_output, hw_sync_id, sync_enable ? "enable" : "disable");
        pthread_mutex_lock(&adev->lock);
        pthread_mutex_lock(&out->lock);
        adev->hw_sync_mode = sync_enable;
        out->frame_write_sum = 0;
        out->frame_skip_sum = 0;
        /* clear up previous playback output status */
        if (!out->standby && (out == adev->active_output)) {
            do_output_standby(out);
        }
        pthread_mutex_unlock(&adev->lock);
        pthread_mutex_unlock(&out->lock);
        goto exit;
    }
    ret = str_parms_get_str(parms, "hdmi_arc_ad", value, sizeof(value));
    if (ret >= 0) {
        int r;
        ALOGI("(%p %p)set hdmi_arc_ad %s\n",
              out, adev->active_output, value);
        pthread_mutex_lock(&adev->lock);
        pthread_mutex_lock(&out->lock);
        int i;
        char temp[7] = {0};
        unsigned ad = 0;
        ALOGI("size of ad %d\n", strlen(value));
        for (i = 0; i < strlen(value); i = i + 6) {
            temp[6] = '\0';
            memcpy(temp, value + i, 6);
            ad = 0;
            r = sscanf(temp, "%x", &ad);
            if (r != 1) {
                ALOGE("sscanf failed\n");
            }
            adev->hdmi_arc_ad[i] = ad;
            ALOGI("hdmi arc support audio ad code %x,index %d\n", ad, i / 6);
        }
        pthread_mutex_unlock(&adev->lock);
        pthread_mutex_unlock(&out->lock);
        goto exit;
    }
exit:
    str_parms_destroy(parms);
    return ret;
}
static char *
out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    char *cap = NULL;
    char *para = NULL;
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    ALOGI("out_get_parameters %s,out %p\n", keys, out);
    if (strstr(keys, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES)) {
        if (out->out_device & AUDIO_DEVICE_OUT_HDMI_ARC) {
            cap = (char *)get_hdmi_arc_cap(adev->hdmi_arc_ad, HDMI_ARC_MAX_FORMAT, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES);
        } else {
            cap = (char *)get_hdmi_sink_cap(AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES);
        }
        if (cap) {
            para = strdup(cap);
            free(cap);
        } else {
            para = strdup("");
        }
        ALOGI("%s\n", para);
        return para;
    } else if (strstr(keys, AUDIO_PARAMETER_STREAM_SUP_CHANNELS)) {
        if (out->out_device & AUDIO_DEVICE_OUT_HDMI_ARC) {
            cap = (char *)get_hdmi_arc_cap(adev->hdmi_arc_ad, HDMI_ARC_MAX_FORMAT, AUDIO_PARAMETER_STREAM_SUP_CHANNELS);
        } else {
            cap = (char *)get_hdmi_sink_cap(AUDIO_PARAMETER_STREAM_SUP_CHANNELS);
        }
        if (cap) {
            para = strdup(cap);
            free(cap);
        } else {
            para = strdup("");
        }
        ALOGI("%s\n", para);
        return para;
    } else if (strstr(keys, AUDIO_PARAMETER_STREAM_SUP_FORMATS)) {
        if (out->out_device & AUDIO_DEVICE_OUT_HDMI_ARC) {
            cap = (char *)get_hdmi_arc_cap(adev->hdmi_arc_ad, HDMI_ARC_MAX_FORMAT, AUDIO_PARAMETER_STREAM_SUP_FORMATS);
        } else {
            cap = (char *)get_hdmi_sink_cap(AUDIO_PARAMETER_STREAM_SUP_FORMATS);
        }
        if (cap) {
            para = strdup(cap);
            free(cap);
        } else {
            para = strdup("");
        }
        ALOGI("%s\n", para);
        return para;
    }
    return strdup("");
}
static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    uint32_t whole_latency;
    uint32_t ret;
    snd_pcm_sframes_t frames = 0;
    whole_latency = (out->config.period_size * out->config.period_count * 1000) / out->config.rate;
    if (!out->pcm || !pcm_is_ready(out->pcm)) {
        return whole_latency;
    }
    ret = pcm_ioctl(out->pcm, SNDRV_PCM_IOCTL_DELAY, &frames);
    if (ret < 0) {
        return whole_latency;
    }
    if (out->format == AUDIO_FORMAT_E_AC3) {
        frames /= 4;
    }
    return (frames * 1000) / out->config.rate;

}
static int
out_set_volume(struct audio_stream_out *stream, float left, float right)
{
    return -ENOSYS;
}

static int out_pause(struct audio_stream_out *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    int r = 0;
    LOGFUNC("(%p)out_pause", out);
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&out->lock);
    if (out->standby || out->pause_status == true) {
        goto exit;
    }
    if (pcm_is_ready(out->pcm)) {
        r = pcm_ioctl(out->pcm, SNDRV_PCM_IOCTL_PAUSE, 1);
        if (r < 0) {
            ALOGE("cannot pause channel\n");
        } else {
            r = 0;
        }
    }
    if (out->dev->hw_sync_mode) {
        sysfs_set_sysfs_str(TSYNC_EVENT, "AUDIO_PAUSE");
    }
    ALOGI("set out pause status\n");
    out->pause_status = true;
exit:
    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&out->lock);
    return r;
}

static int out_resume(struct audio_stream_out *stream)
{
    LOGFUNC("out_resume");
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&out->lock);
    int r = 0;
    if (out->standby || out->pause_status == false) {
        goto exit;
    }
    if (pcm_is_ready(out->pcm)) {
        r = pcm_ioctl(out->pcm, SNDRV_PCM_IOCTL_PAUSE, 0);
        if (r < 0) {
            ALOGE("cannot resume channel\n");
        } else {
            r = 0;
        }
    }
    if (out->dev->hw_sync_mode) {
        sysfs_set_sysfs_str(TSYNC_EVENT, "AUDIO_RESUME");
    }
    ALOGI("clear out pause status\n");
    out->pause_status = false;
exit:
    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&out->lock);
    return r;
}

static ssize_t
out_write(struct audio_stream_out *stream, const void *buffer, size_t bytes)
{
    int ret = 0;
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    size_t frame_size = audio_stream_out_frame_size(stream);
    size_t in_frames = bytes / frame_size;
    bool force_input_standby = false;
    size_t out_frames = 0;
    void *buf;
    uint i, total_len;
    char prop[PROPERTY_VALUE_MAX];
    int codec_type = out->codec_type;
    int samesource_flag = 0;
    uint32_t latency_frames;
    uint64_t total_frame = 0;
    audio_hwsync_t  *p_hwsync = &adev->hwsync;
    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
    * on the output stream mutex - e.g. executing select_mode() while holding the hw device
    * mutex
    */
    out->bytes_write_total += bytes;
    DEBUG("out %p,dev %p out_write total size %lld\n", out, adev, out->bytes_write_total);
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&out->lock);
    if (out->pause_status == true) {
        pthread_mutex_unlock(&adev->lock);
        pthread_mutex_unlock(&out->lock);
        ALOGI("call out_write when pause status,size %d,(%p)\n", bytes, out);
        return 0;
    }
    if ((out->standby) && adev->hw_sync_mode) {
        /*
        there are two types of raw data come to hdmi  audio hal
        1) compressed audio data without IEC61937 wrapped
        2) compressed audio data  with IEC61937 wrapped (typically from amlogic amadec source)
        we use the AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO to distiguwish the two cases.
        */
        if ((codec_type == TYPE_AC3 || codec_type == TYPE_EAC3)  && (out->flags & AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO)) {
            spdifenc_init(out->pcm);
            out->spdif_enc_init_frame_write_sum = out->frame_write_sum;
        }
        // todo: check timestamp header PTS discontinue for new sync point after seek
        aml_audio_hwsync_clear_status(out);
        out->spdif_enc_init_frame_write_sum = out->frame_write_sum;
    }
    if (out->standby) {
        ret = start_output_stream(out);
        if (ret != 0) {
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }
        out->standby = 0;
        /* a change in output device may change the microphone selection */
        if (adev->active_input &&
            adev->active_input->source == AUDIO_SOURCE_VOICE_COMMUNICATION) {
            force_input_standby = true;
        }
    }
    void *write_buf = NULL;
    int  hwsync_cost_bytes = 0;
    if (adev->hw_sync_mode == 1) {
        uint64_t  cur_pts = 0xffffffff;
        int outsize = 0;
        char tempbuf[128];
        DEBUG("before aml_audio_hwsync_find_frame bytes %d\n", bytes);
        hwsync_cost_bytes = aml_audio_hwsync_find_frame(out, buffer, bytes, &cur_pts, &outsize);
        DEBUG("after aml_audio_hwsync_find_frame bytes remain %d,cost %d,outsize %d,pts %llx\n",
              bytes - hwsync_cost_bytes, hwsync_cost_bytes, outsize, cur_pts);
        //TODO,skip 3 frames after flush, to tmp fix seek pts discontinue issue.need dig more
        // to find out why seek ppint pts frame is remained after flush.WTF.
        if (out->skip_frame > 0) {
            out->skip_frame--;
            ALOGI("skip pts@%llx,cur frame size %d,cost size %d\n", cur_pts, outsize, hwsync_cost_bytes);
            pthread_mutex_unlock(&adev->lock);
            pthread_mutex_unlock(&out->lock);
            return hwsync_cost_bytes;
        }
        if (cur_pts != 0xffffffff && outsize > 0) {
            // if we got the frame body,which means we get a complete frame.
            //we take this frame pts as the first apts.
            //this can fix the seek discontinue,we got a fake frame,which maybe cached before the seek
            if (p_hwsync->first_apts_flag == false) {
                p_hwsync->first_apts_flag = true;
                p_hwsync->first_apts = cur_pts;
                sprintf(tempbuf, "AUDIO_START:0x%lx", cur_pts & 0xffffffff);
                ALOGI("tsync -> %s,frame size %d", tempbuf, outsize);
                if (sysfs_set_sysfs_str(TSYNC_EVENT, tempbuf) == -1) {
                    ALOGE("set AUDIO_START failed \n");
                }
            } else {
                unsigned long apts;
                unsigned long latency = out_get_latency(out) * 90;
                // check PTS discontinue, which may happen when audio track switching
                // discontinue means PTS calculated based on first_apts and frame_write_sum
                // does not match the timestamp of next audio samples
                if (cur_pts >  latency) {
                    apts = (unsigned long)cur_pts - latency;
                } else {
                    apts = 0;
                }
                if (0) { //abs(cur_pts -apts) > APTS_DISCONTINUE_THRESHOLD) {
                    ALOGI("HW sync PTS discontinue, 0x%llx->0x%llx(from header) diff %llx,last apts %llx(from header)",
                          apts, cur_pts, abs(cur_pts - apts), p_hwsync->last_apts_from_header);
                    p_hwsync->first_apts = cur_pts;
                    sprintf(tempbuf, "AUDIO_TSTAMP_DISCONTINUITY:0x%lx", cur_pts);
                    if (sysfs_set_sysfs_str(TSYNC_EVENT, tempbuf) == -1) {
                        ALOGE("unable to open file %s,err: %s", TSYNC_EVENT, strerror(errno));
                    }
                } else {
                    unsigned long pcr = 0;
                    if (get_sysfs_int16(TSYNC_PCRSCR, &pcr) == 0) {
                        uint32_t apts_cal = apts & 0xffffffff;
                        if (abs(pcr - apts) < SYSTIME_CORRECTION_THRESHOLD) {
                            // do nothing
                        }
                        // limit the gap handle to 0.5~5 s.
                        else if ((apts - pcr) > APTS_DISCONTINUE_THRESHOLD_MIN && (apts - pcr) < APTS_DISCONTINUE_THRESHOLD_MAX) {
                            int insert_size = 0;
                            int once_write_size = 0;
                            if (out->codec_type == TYPE_EAC3) {
                                insert_size = abs(apts - pcr) / 90 * 48 * 4 * 4;
                            } else {
                                insert_size = abs(apts - pcr) / 90 * 48 * 4;
                            }
                            insert_size = insert_size & (~63);
                            ALOGI("audio gap %d ms ,need insert data %d\n", abs(apts - pcr) / 90, insert_size);
                            char *insert_buf = (char*)malloc(8192);
                            if (insert_buf == NULL) {
                                ALOGE("malloc size failed \n");
                                pthread_mutex_unlock(&adev->lock);
                                goto exit;
                            }
                            memset(insert_buf, 0, 8192);
                            while (insert_size > 0) {
                                once_write_size = insert_size > 8192 ? 8192 : insert_size;
                                ret = pcm_write(out->pcm, (void *) insert_buf, once_write_size);
                                if (ret != 0) {
                                    ALOGE("pcm write failed\n");
                                    free(insert_buf);
                                    pthread_mutex_unlock(&adev->lock);
                                    goto exit;
                                }
                                insert_size -= once_write_size;
                            }
                            free(insert_buf);
                        }
                        //audio pts smaller than pcr,need skip frame.
                        else if ((pcr - apts) > APTS_DISCONTINUE_THRESHOLD_MIN && (pcr - apts) < APTS_DISCONTINUE_THRESHOLD_MAX) {
                            //we assume one frame duration is 32 ms for DD+(6 blocks X 1536 frames,48K sample rate)
                            if (out->codec_type == TYPE_EAC3 && outsize > 0) {
                                ALOGI("audio slow 0x%x,skip frame @pts 0x%llx,pcr 0x%x,cur apts 0x%x\n", (pcr - apts), cur_pts, pcr, apts);
                                out->frame_skip_sum  +=   1536;
                                bytes =   outsize;
                                pthread_mutex_unlock(&adev->lock);
                                goto exit;
                            }
                        } else {
                            sprintf(tempbuf, "0x%lx", apts);
                            ALOGI("tsync -> reset pcrscr 0x%x -> 0x%x, %s big,diff %d ms", pcr, apts, apts > pcr ? "apts" : "pcr", abs(apts - pcr) / 90);
#if 0
                            int ret_val = sysfs_set_sysfs_str(TSYNC_APTS, tempbuf);
                            if (ret_val == -1) {
                                ALOGE("unable to open file %s,err: %s", TSYNC_APTS, strerror(errno));
                            }
#endif
                        }
                    }
                }
            }
        }
        if (outsize > 0) {
            in_frames = outsize / frame_size;
            write_buf = p_hwsync->hw_sync_body_buf;
        } else {
            bytes = hwsync_cost_bytes;
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }
    } else {
        write_buf = (void *) buffer;
    }
    pthread_mutex_unlock(&adev->lock);
    out_frames = in_frames;
    buf = (void *) write_buf;
    if (getprop_bool("media.hdmihal.outdump")) {
        FILE *fp1 = fopen("/data/tmp/hdmi_audio_out.pcm", "a+");
        if (fp1) {
            int flen = fwrite((char *)buffer, 1, out_frames * frame_size, fp1);
            LOGFUNC("flen = %d---outlen=%d ", flen, out_frames * frame_size);
            fclose(fp1);
        } else {
            LOGFUNC("could not open file:/data/hdmi_audio_out.pcm");
        }
    }
    if (codec_type_is_raw_data(out->codec_type) && !(out->flags & AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO)) {
        //here to do IEC61937 pack
        DEBUG("IEC61937 write size %d,hw_sync_mode %d,flag %x\n", out_frames * frame_size, adev->hw_sync_mode, out->flags);
        if (out->codec_type  > 0) {
            // compressed audio DD/DD+
            bytes = spdifenc_write((void *) buf, out_frames * frame_size);
            //need return actual size of this burst write
            if (adev->hw_sync_mode == 1) {
                bytes = hwsync_cost_bytes;
            }
            DEBUG("spdifenc_write return %d\n", bytes);
            if (out->codec_type == TYPE_EAC3) {
                out->frame_write_sum = spdifenc_get_total() / 16 + out->spdif_enc_init_frame_write_sum;
            } else {
                out->frame_write_sum = spdifenc_get_total() / 4 + out->spdif_enc_init_frame_write_sum;
            }
            DEBUG("out %p,spdifenc_get_total() / 4 %lld\n", out, spdifenc_get_total() / 16);
        }
        goto exit;
    }
    if (!out->standby) {
        if (out->multich == 8) {
            int *p32 = NULL;
            short *p16 = (short *) buf;
            short *p16_temp;
            int i, NumSamps;
            NumSamps = out_frames * frame_size / sizeof(short);
            p32 = malloc(NumSamps * sizeof(int));
            if (p32 != NULL) {
                //here to swap the channnl data here
                //actual now:L,missing,R,RS,RRS,,LS,LRS,missing
                //expect L,C,R,RS,RRS,LRS,LS,LFE (LFE comes from to center)
                //actual  audio data layout  L,R,C,none/LFE,LRS,RRS,LS,RS
                p16_temp = (short *) p32;
                for (i = 0; i < NumSamps; i = i + 8) {
                    p16_temp[0 + i]/*L*/ = p16[0 + i];
                    p16_temp[1 + i]/*R*/ = p16[1 + i];
                    p16_temp[2 + i] /*LFE*/ = p16[3 + i];
                    p16_temp[3 + i] /*C*/ = p16[2 + i];
                    p16_temp[4 + i] /*LS*/ = p16[6 + i];
                    p16_temp[5 + i] /*RS*/ = p16[7 + i];
                    p16_temp[6 + i] /*LRS*/ = p16[4 + i];
                    p16_temp[7 + i]/*RRS*/ = p16[5 + i];
                }
                memcpy(p16, p16_temp, NumSamps * sizeof(short));
                for (i = 0; i < NumSamps; i++) { //suppose 16bit/8ch PCM
                    p32[i] = p16[i] << 16;
                }
                ret = pcm_write(out->pcm, (void *) p32, NumSamps * 4);
                free(p32);
            }
        } else if (out->multich == 6) {
            int *p32 = NULL;
            short *p16 = (short *) buf;
            short *p16_temp;
            int i, j, NumSamps, real_samples;
            real_samples = out_frames * frame_size / sizeof(short);
            NumSamps = real_samples * 8 / 6;
            //ALOGI("6ch to 8 ch real %d, to %d,bytes %d,frame size %d\n",real_samples,NumSamps,bytes,frame_size);
            p32 = malloc(NumSamps * sizeof(int));
            if (p32 != NULL) {
                p16_temp = (short *) p32;
                for (i = 0; i < real_samples; i = i + 6) {
                    p16_temp[0 + i]/*L*/ = p16[0 + i];
                    p16_temp[1 + i]/*R*/ = p16[1 + i];
                    p16_temp[2 + i] /*LFE*/ = p16[3 + i];
                    p16_temp[3 + i] /*C*/ = p16[2 + i];
                    p16_temp[4 + i] /*LS*/ = p16[4 + i];
                    p16_temp[5 + i] /*RS*/ = p16[5 + i];
                }
                memcpy(p16, p16_temp, real_samples * sizeof(short));
                memset(p32, 0, NumSamps * sizeof(int));
                for (i = 0, j = 0; j < NumSamps; i = i + 6, j = j + 8) { //suppose 16bit/8ch PCM
                    p32[j] = p16[i] << 16;
                    p32[j + 1] = p16[i + 1] << 16;
                    p32[j + 2] = p16[i + 2] << 16;
                    p32[j + 3] = p16[i + 3] << 16;
                    p32[j + 4] = p16[i + 4] << 16;
                    p32[j + 5] = p16[i + 5] << 16;
                }
                ret = pcm_write(out->pcm, (void *) p32, NumSamps * 4);
                free(p32);
            }
        } else {
#if 0
            codec_type =
                get_sysfs_int("/sys/class/audiodsp/digital_codec");
            samesource_flag =
                get_sysfs_int("/sys/class/audiodsp/audio_samesource");
            if (out->last_codec_type > 0 && codec_type != out->last_codec_type) {
                samesource_flag = 1;
            }
            if (samesource_flag == 1 && codec_type) {
                ALOGI
                ("to disable same source,need reset alsa,last %d,type %d,same source flag %d ,\n",
                 out->last_codec_type, codec_type, samesource_flag);
                out->last_codec_type = codec_type;
                pcm_stop(out->pcm);
            }
#endif
            DEBUG("write size %d\n", out_frames * frame_size);
            ret = pcm_write(out->pcm, (void *) buf, out_frames * frame_size);
            if (ret == 0) {
                out->frame_write_sum += out_frames;
            }
        }
    }
exit:
    total_frame = out->frame_write_sum + out->frame_skip_sum;
    latency_frames = out_get_latency(out) * out->config.rate / 1000;
    if (total_frame >= latency_frames) {
        out->last_frames_postion = total_frame - latency_frames;
    } else {
        out->last_frames_postion = total_frame;
    }
    pthread_mutex_unlock(&out->lock);
    if (ret != 0) {
        usleep(bytes * 1000000 / audio_stream_out_frame_size(stream) /
               out_get_sample_rate(&stream->common));
    }
    return bytes;
}

static int
out_get_render_position(const struct audio_stream_out *stream,
                        uint32_t * dsp_frames)
{
    LOGFUNC("%s(%p, %p)", __FUNCTION__, stream, dsp_frames);
    return -EINVAL;
}

static int
out_add_audio_effect(const struct audio_stream *stream,
                     effect_handle_t effect)
{
    LOGFUNC("%s(%p, %p)", __FUNCTION__, stream, effect);
    return 0;
}

static int
out_remove_audio_effect(const struct audio_stream *stream,
                        effect_handle_t effect)
{
    return 0;
}

static int
out_get_next_write_timestamp(const struct audio_stream_out *stream,
                             int64_t * timestamp)
{
    return -EINVAL;
}
static int out_get_presentation_position(const struct audio_stream_out *stream, uint64_t *frames, struct timespec *timestamp)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
#if 1
    if (frames != NULL) {
        *frames = out->last_frames_postion;
    }
    DEBUG("%p,*frames %lld\n", out, *frames);
    if (timestamp != NULL) {
        clock_gettime(CLOCK_MONOTONIC, timestamp);
    }
#else
#define TIME_TO_MS(time) ((uint64_t)time->tv_sec * 1000 + time->tv_nsec/1000000ULL)

    if (timestamp != NULL) {
        clock_gettime(CLOCK_MONOTONIC, timestamp);
        if (out->last_frames_pos == 0) {

            ALOGI("first frame pos \n");
            if (frames != NULL) {
                *frames  =  out->last_frames_pos;
            }
            out->last_frames_pos = TIME_TO_MS(timestamp) * 48;
        } else {
            if (frames != NULL) {
                *frames  = TIME_TO_MS(timestamp) * 48 - out->last_frames_pos;
            }
            ALOGI("pos %lld,first %lld\n", *frames, out->last_frames_pos);
        }

    }
#undef  TIME_TO_MS
#endif
    return 0;
}
/** audio_stream_in implementation **/

/* must be called with hw device and input stream mutexes locked */
static int
start_input_stream(struct aml_stream_in *in)
{
    int ret = 0;
    unsigned int card = CARD_AMLOGIC_DEFAULT;
    unsigned int port = PORT_MM;
    struct aml_audio_device *adev = in->dev;
    LOGFUNC
    ("%s(need_echo_reference=%d, channels=%d, rate=%d, requested_rate=%d, mode= %d)",
     __FUNCTION__, in->need_echo_reference, in->config.channels,
     in->config.rate, in->requested_rate, adev->mode);
    adev->active_input = in;
    if (adev->mode != AUDIO_MODE_IN_CALL) {
        adev->in_device &= ~AUDIO_DEVICE_IN_ALL;
        adev->in_device |= in->device;
        select_input_device(adev);
    }
    PERIOD_SIZE = DEFAULT_PERIOD_SIZE;
    in->config.period_size = PERIOD_SIZE;
    /* this assumes routing is done previously */
    in->pcm = pcm_open(card, port, PCM_IN, &in->config);
    if (!pcm_is_ready(in->pcm)) {
        ALOGE("cannot open pcm_in driver: %s", pcm_get_error(in->pcm));
        pcm_close(in->pcm);
        adev->active_input = NULL;
        return -ENOMEM;
    }
    ALOGI("pcm_open in: card(%d), port(%d)", card, port);
    return 0;
}

static int
check_input_stream(struct aml_stream_in *in)
{
    int ret = 0;
    unsigned int card = CARD_AMLOGIC_BOARD;
    unsigned int port = 0;
    int ext_card;
    ext_card = get_external_card(1);
    if (ext_card < 0) {
        card = CARD_AMLOGIC_BOARD;
    } else {
        card = ext_card;
    }
    /* this assumes routing is done previously */
    in->pcm = pcm_open(card, port, PCM_IN, &in->config);
    if (!pcm_is_ready(in->pcm)) {
        ALOGE("check_input_stream:cannot open pcm_in driver: %s",
              pcm_get_error(in->pcm));
        pcm_close(in->pcm);
        return -ENOMEM;
    }
    pcm_close(in->pcm);
    return 0;
}

static uint32_t
in_get_sample_rate(const struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;

    LOGFUNC("%s(%p)", __FUNCTION__, stream);
    return in->requested_rate;
}

static int
in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    LOGFUNC("%s(%p, %d)", __FUNCTION__, stream, rate);
    return 0;
}

static size_t
in_get_buffer_size(const struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;

    LOGFUNC("%s(%p)", __FUNCTION__, stream);
    return get_input_buffer_size(in->config.rate,
                                 AUDIO_FORMAT_PCM_16_BIT, in->config.channels);
}

static audio_channel_mask_t
in_get_channels(const struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    if (in->config.channels == 1) {
        return AUDIO_CHANNEL_IN_MONO;
    } else {
        return AUDIO_CHANNEL_IN_STEREO;
    }
}

static audio_format_t
in_get_format(const struct audio_stream *stream)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int
in_set_format(struct audio_stream *stream, audio_format_t format)
{
    LOGFUNC("%s(%p, %d)", __FUNCTION__, stream, format);
    return 0;
}

/* must be called with hw device and input stream mutexes locked */
static int
do_input_standby(struct aml_stream_in *in)
{
    struct aml_audio_device *adev = in->dev;
    LOGFUNC("%s(%p)", __FUNCTION__, in);
    if (!in->standby) {
        pcm_close(in->pcm);
        in->pcm = NULL;
        adev->active_input = 0;
        if (adev->mode != AUDIO_MODE_IN_CALL) {
            adev->in_device &= ~AUDIO_DEVICE_IN_ALL;
            select_input_device(adev);
        }
        in->standby = 1;
    }
    return 0;
}

static int
in_standby(struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    int status;
    LOGFUNC("%s(%p)", __FUNCTION__, stream);
    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    status = do_input_standby(in);
    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return status;
}

static int
in_dump(const struct audio_stream *stream, int fd)
{
    LOGFUNC("%s(%p, %d)", __FUNCTION__, stream, fd);
    return 0;
}

static int
in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *adev = in->dev;
    struct str_parms *parms;
    char *str;
    char value[32];
    int ret, val = 0;
    bool do_standby = false;
    LOGFUNC("%s(%p, %s)", __FUNCTION__, stream, kvpairs);
    parms = str_parms_create_str(kvpairs);
    ret =
        str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_INPUT_SOURCE, value,
                          sizeof(value));
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&in->lock);
    if (ret >= 0) {
        val = atoi(value);
        /* no audio source uses val == 0 */
        if ((in->source != val) && (val != 0)) {
            in->source = val;
            do_standby = true;
        }
    }
    ret =
        str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value,
                          sizeof(value));
    if (ret >= 0) {
        val = atoi(value) & ~AUDIO_DEVICE_BIT_IN;
        if ((in->device != val) && (val != 0)) {
            in->device = val;
            do_standby = true;
        }
    }
    if (do_standby) {
        do_input_standby(in);
    }
    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&adev->lock);
    str_parms_destroy(parms);
    return ret;
}

static char *
in_get_parameters(const struct audio_stream *stream, const char *keys)
{
    return strdup("");
}

static int
in_set_gain(struct audio_stream_in *stream, float gain)
{
    LOGFUNC("%s(%p, %f)", __FUNCTION__, stream, gain);
    return 0;
}
static ssize_t
in_read(struct audio_stream_in *stream, void *buffer, size_t bytes)
{
    int ret = 0;
    int i = 0;
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *adev = in->dev;
    size_t frames_rq = bytes / audio_stream_in_frame_size(stream);
    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
    * on the input stream mutex - e.g. executing select_mode() while holding the hw device
    * mutex
    */
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&in->lock);
    if (in->standby) {
        ret = start_input_stream(in);
        if (ret == 0) {
            in->standby = 0;
        }
    }
    pthread_mutex_unlock(&adev->lock);
    ret = pcm_read(in->pcm, buffer, bytes);
    if (ret > 0) {
        ret = 0;
    }
    if (ret == 0 && adev->mic_mute) {
        LOGFUNC("%s(adev->mic_mute = %d)", __FUNCTION__, adev->mic_mute);
        memset(buffer, 0, bytes);
    }
exit:
    if (ret < 0)
        usleep(bytes * 1000000 / audio_stream_in_frame_size(stream) /
               in_get_sample_rate(&stream->common));
    pthread_mutex_unlock(&in->lock);
    return bytes;
}

static uint32_t
in_get_input_frames_lost(struct audio_stream_in *stream)
{
    return 0;
}

static int
adev_open_output_stream(struct audio_hw_device *dev,
                        audio_io_handle_t handle,
                        audio_devices_t devices,
                        audio_output_flags_t flags,
                        struct audio_config *config,
                        struct audio_stream_out **stream_out)
{
    int ret;
    int digital_codec;          //digital_codec
    struct aml_audio_device *ladev = (struct aml_audio_device *) dev;
    struct aml_stream_out *out;
    int channel_count = popcount(config->channel_mask);
    ALOGE("%s(devices=0x%04x,format=%d, chnum=0x%04x, SR=%d,io handle %d, flags = 0x%x )",
            __FUNCTION__, devices, config->format, channel_count,
            config->sample_rate, handle, flags);
    out = (struct aml_stream_out *) calloc(1, sizeof(struct aml_stream_out));
    if (!out) {
        return -ENOMEM;
    }
    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = NULL;//out_add_audio_effect;
    out->stream.common.remove_audio_effect = NULL;//out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
    out->stream.pause = out_pause;
    out->stream.resume = out_resume;
    out->stream.get_presentation_position = out_get_presentation_position;
    out->stream.flush = out_flush;
    out->config = pcm_config_out;
    digital_codec = get_codec_type(config->format);
    if (digital_codec == TYPE_EAC3) {
        out->config.period_size = pcm_config_out.period_size * 2;
    } else if (digital_codec == TYPE_TRUE_HD || digital_codec == TYPE_DTS_HD) {
        out->config.period_size = pcm_config_out.period_size * 4 * 2;
    }
    if (channel_count > 2) {
        ALOGI("[adev_open_output_stream]: out/%p channel/%d\n", out,
              channel_count);
        out->multich = channel_count;
        out->config.channels = channel_count;
    }
    if (codec_type_is_raw_data(digital_codec)) {
        ALOGI("for raw audio output,force alsa stereo output\n");
        out->config.channels = 2;
        out->multich = 2;
    }
    /* if 2ch high sample rate PCM audio goes to direct output, set the required sample rate which needed by AF */
    if ((flags & AUDIO_OUTPUT_FLAG_DIRECT) && config->sample_rate > 0) {
        out->config.rate = config->sample_rate;
    }
    out->format = config->format;
    out->dev = ladev;
    out->standby = 1;
    ladev->hw_sync_mode = false;
    ladev->hwsync.first_apts_flag = false;
    out->frame_write_sum = 0;
    if (flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC) {
        ALOGI("Output stream open with AUDIO_OUTPUT_FLAG_HW_AV_SYNC");
    }
    if (audio_is_raw_data(config->format) || (flags & AUDIO_OUTPUT_FLAG_DIRECT)) {
        if (config->format == 0) {
            config->format = AUDIO_FORMAT_AC3;
            out->format = AUDIO_FORMAT_AC3;
        }
    }
    if (config->sample_rate == 0) {
        out->config.rate = config->sample_rate = 48000;
    }
    if (audio_is_raw_data(config->format)) {
        out->config.rate = config->sample_rate;
    }
    LOGFUNC("%s(devices=0x%04x,format=0x%x, chmask=0x%04x, SR=%d)",
            __FUNCTION__, devices, config->format, config->channel_mask,
            config->sample_rate);
    out->flags = flags;
    out->out_device = devices;
    *stream_out = &out->stream;
    if (devices & AUDIO_DEVICE_OUT_HDMI_ARC) {
        ALOGI("ARC stream %p\n", out);
        memset(ladev->hdmi_arc_ad, 0, sizeof(ladev->hdmi_arc_ad));
    }
    return 0;
err_open:
    free(out);
    *stream_out = NULL;
    return ret;
}

static void
adev_close_output_stream(struct audio_hw_device *dev,
                         struct audio_stream_out *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    ALOGE("%s(%p, %p)", __FUNCTION__, dev, stream);
    out_standby(&stream->common);
    if (out->buffer) {
        free(out->buffer);
    }
    free(stream);
}

static int
adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    LOGFUNC("%s(%p, %s)", __FUNCTION__, dev, kvpairs);
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;
    return 0;
}

static char *
adev_get_parameters(const struct audio_hw_device *dev, const char *keys)
{
    LOGFUNC("%s(%p, %s)", __FUNCTION__, dev, keys);
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    if (!strcmp(keys, AUDIO_PARAMETER_HW_AV_EAC3_SYNC)) {
        return strdup("true");
    }
    return strdup("");
}

static int
adev_init_check(const struct audio_hw_device *dev)
{
    LOGFUNC("%s(%p)", __FUNCTION__, dev);
    return 0;
}

static int
adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;
    LOGFUNC("%s(%p, %f)", __FUNCTION__, dev, volume);
    return 0;
}

static int
adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    LOGFUNC("%s(%p, %f)", __FUNCTION__, dev, volume);
    return -ENOSYS;
}

static int
adev_get_master_volume(struct audio_hw_device *dev, float *volume)
{
    return -ENOSYS;
}

static int
adev_set_master_mute(struct audio_hw_device *dev, bool muted)
{
    return -ENOSYS;
}

static int
adev_get_master_mute(struct audio_hw_device *dev, bool * muted)
{
    return -ENOSYS;
}

static int
adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode)
{
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;
    LOGFUNC("%s(%p, %d)", __FUNCTION__, dev, mode);
    pthread_mutex_lock(&adev->lock);
    if (adev->mode != mode) {
        adev->mode = mode;
        select_mode(adev);
    }
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static int
adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;
    LOGFUNC("%s(%p, %d)", __FUNCTION__, dev, state);
    adev->mic_mute = state;
    return 0;
}

static int
adev_get_mic_mute(const struct audio_hw_device *dev, bool * state)
{
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;

    LOGFUNC("%s(%p, %p)", __FUNCTION__, dev, state);
    *state = adev->mic_mute;
    return 0;

}

static size_t
adev_get_input_buffer_size(const struct audio_hw_device *dev,
                           const struct audio_config *config)
{
    size_t size;
    int channel_count = popcount(config->channel_mask);
    LOGFUNC("%s(%p, %d, %d, %d)", __FUNCTION__, dev, config->sample_rate,
            config->format, channel_count);
    if (check_input_parameters
        (config->sample_rate, config->format, channel_count) != 0) {
        return 0;
    }
    return get_input_buffer_size(config->sample_rate,
                                 config->format, channel_count);

}

static int
adev_open_input_stream(struct audio_hw_device *dev,
                       audio_io_handle_t handle,
                       audio_devices_t devices,
                       struct audio_config *config,
                       struct audio_stream_in **stream_in)
{
    struct aml_audio_device *ladev = (struct aml_audio_device *) dev;
    struct aml_stream_in *in;
    int ret;
    int channel_count = popcount(config->channel_mask);
    LOGFUNC("**********%s(%#x, %d, 0x%04x, %d)", __FUNCTION__,
            devices, config->format, config->channel_mask,
            config->sample_rate);
    if (check_input_parameters(config->sample_rate, config->format, channel_count) != 0) {
        return -EINVAL;
    }
    in = (struct aml_stream_in *) calloc(1, sizeof(struct aml_stream_in));
    if (!in) {
        return -ENOMEM;
    }
    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = in_get_format;
    in->stream.common.set_format = in_set_format;
    in->stream.common.standby = in_standby;
    in->stream.common.dump = in_dump;
    in->stream.common.set_parameters = in_set_parameters;
    in->stream.common.get_parameters = in_get_parameters;
    in->stream.common.add_audio_effect = NULL;//in_add_audio_effect;
    in->stream.common.remove_audio_effect = NULL;//in_remove_audio_effect;
    in->stream.set_gain = in_set_gain;
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;
    in->requested_rate = config->sample_rate;
    memcpy(&in->config, &pcm_config_in, sizeof(pcm_config_in));
    ret = check_input_stream(in);
    if (ret < 0) {
        ALOGE("fail to open input stream, change channel count from %d to %d",
              in->config.channels, channel_count);
        in->config.channels = channel_count;
    }
    if (in->config.channels == 1) {
        config->channel_mask = AUDIO_CHANNEL_IN_MONO;
    } else if (in->config.channels == 2) {
        config->channel_mask = AUDIO_CHANNEL_IN_STEREO;
    } else {
        ALOGE("Bad value of channel count : %d", in->config.channels);
    }
    in->buffer = malloc(in->config.period_size *
                        audio_stream_in_frame_size(&in->stream));
    if (!in->buffer) {
        ret = -ENOMEM;
        goto err_open;
    }
    in->dev = ladev;
    in->standby = 1;
    in->device = devices & ~AUDIO_DEVICE_BIT_IN;
    *stream_in = &in->stream;
    return 0;
err_open:
    free(in);
    *stream_in = NULL;
    return ret;
}

static void
adev_close_input_stream(struct audio_hw_device *dev,
                        struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;

    LOGFUNC("%s(%p, %p)", __FUNCTION__, dev, stream);
    in_standby(&stream->common);
    free(stream);
    return;
}

static int
adev_dump(const audio_hw_device_t * device, int fd)
{
    LOGFUNC("%s(%p, %d)", __FUNCTION__, device, fd);
#if 0
    struct aml_audio_device *adev = (struct aml_audio_device *) device;
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    audio_hwsync_t  *p_hwsync = &adev->hwsync;
    dprintf(fd, "Out %p dump:\n", out);
    dprintf(fd, "frame write sum %lld,spdif_enc_init_frame_write_sum %lld\n",
            out->frame_write_sum, out->spdif_enc_init_frame_write_sum);
    dprintf(fd, "HWSYNC status:\n");
    dprintf(fd, "hwsync enable:%d\n", adev->hw_sync_mode);
    dprintf(fd, "hw_sync_state:%d\n", p_hwsync->hw_sync_state);
    dprintf(fd, "first_apts_flag:%d\n", p_hwsync->first_apts_flag);
    dprintf(fd, "first_apts:%llx\n", p_hwsync->first_apts);
    dprintf(fd, "last_apts_from_header:%llx\n", p_hwsync->last_apts_from_header);
    dprintf(fd, "first_apts_flag:%d\n", p_hwsync->first_apts_flag);
    dprintf(fd, "hw_sync_frame_size:%d\n", p_hwsync->hw_sync_frame_size);
#endif
    return 0;
}

static int
adev_close(hw_device_t * device)
{
    struct aml_audio_device *adev = (struct aml_audio_device *) device;

    LOGFUNC("%s(%p)", __FUNCTION__, device);
    free(device);
    return 0;
}


static int
adev_open(const hw_module_t * module, const char *name,
          hw_device_t ** device)
{
    struct aml_audio_device *adev;
    int ret;
    LOGFUNC("%s(%p, %s, %p)", __FUNCTION__, module, name, device);
    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0) {
        return -EINVAL;
    }

    adev = calloc(1, sizeof(struct aml_audio_device));
    if (!adev) {
        return -ENOMEM;
    }
    adev->hw_device.common.tag = HARDWARE_DEVICE_TAG;
    adev->hw_device.common.version = AUDIO_DEVICE_API_VERSION_2_0;
    adev->hw_device.common.module = (struct hw_module_t *) module;
    adev->hw_device.common.close = adev_close;
    //adev->hw_device.get_supported_devices = adev_get_supported_devices;
    adev->hw_device.init_check = adev_init_check;
    adev->hw_device.set_voice_volume = adev_set_voice_volume;
    adev->hw_device.set_master_volume = adev_set_master_volume;
    adev->hw_device.get_master_volume = adev_get_master_volume;
    adev->hw_device.set_master_mute = adev_set_master_mute;
    adev->hw_device.get_master_mute = adev_get_master_mute;
    adev->hw_device.set_mode = adev_set_mode;
    adev->hw_device.set_mic_mute = adev_set_mic_mute;
    adev->hw_device.get_mic_mute = adev_get_mic_mute;
    adev->hw_device.set_parameters = adev_set_parameters;
    adev->hw_device.get_parameters = adev_get_parameters;
    adev->hw_device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->hw_device.open_output_stream = adev_open_output_stream;
    adev->hw_device.close_output_stream = adev_close_output_stream;
    adev->hw_device.open_input_stream = adev_open_input_stream;
    adev->hw_device.close_input_stream = adev_close_input_stream;
    adev->hw_device.dump = adev_dump;
    /* Set the default route before the PCM stream is opened */
    adev->mode = AUDIO_MODE_NORMAL;
    adev->out_device = AUDIO_DEVICE_OUT_AUX_DIGITAL;
    adev->in_device = AUDIO_DEVICE_IN_BUILTIN_MIC & ~AUDIO_DEVICE_BIT_IN;
    select_output_device(adev);
    *device = &adev->hw_device.common;
    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "aml HDMI audio HW HAL",
        .author = "amlogic, Corp.",
        .methods = &hal_module_methods,
    },
};
