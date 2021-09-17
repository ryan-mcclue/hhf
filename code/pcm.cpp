// SPDX-License-Identifier: zlib-acknowledgement
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <unistd.h>

#include <sys/ioctl.h>

#include <errno.h>
#include <string.h>

#include <linux/version.h>
#include <sound/asound.h>

#define MY_SAMPLE_FORMAT        SNDRV_PCM_FORMAT_S16_LE
#define MY_SAMPLES_PER_FRAME    2
#define MY_FRAMES_PER_SECOND    48000
#define MY_PLAYBACK_DURATION    1

#define ARRAY_SIZE(array)   (sizeof(array) / sizeof(array[0]))

void print_help(const char *prog_name)
{
    printf("%s CDEV\n", prog_name);
    printf("  CDEV: The path to ALSA PCM character device.\n");
}

static const char *const class_labels[] = {
    [SNDRV_PCM_CLASS_GENERIC]   = "generic",
    [SNDRV_PCM_CLASS_MULTI]     = "multi",
    [SNDRV_PCM_CLASS_MODEM]     = "modem",
    [SNDRV_PCM_CLASS_DIGITIZER] = "digitizer"
};
static const char *const subclass_labels[] = {
    [SNDRV_PCM_SUBCLASS_GENERIC_MIX]    = "generic-mix",
    [SNDRV_PCM_SUBCLASS_MULTI_MIX]      = "multi-mix"
};
static const char *const direction_labels[] = {
    [SNDRV_PCM_STREAM_PLAYBACK] = "playback",
    [SNDRV_PCM_STREAM_CAPTURE]  = "capture"
};

static const char *const param_labels[] = {
    [SNDRV_PCM_HW_PARAM_ACCESS]         = "access",
    [SNDRV_PCM_HW_PARAM_FORMAT]         = "format",
    [SNDRV_PCM_HW_PARAM_SUBFORMAT]      = "subformat",
    [SNDRV_PCM_HW_PARAM_SAMPLE_BITS]    = "sample-bits",
    [SNDRV_PCM_HW_PARAM_FRAME_BITS]     = "frame-bits",
    [SNDRV_PCM_HW_PARAM_CHANNELS]       = "channels",
    [SNDRV_PCM_HW_PARAM_RATE]           = "rate",
    [SNDRV_PCM_HW_PARAM_PERIOD_TIME]    = "period-time",
    [SNDRV_PCM_HW_PARAM_PERIOD_SIZE]    = "period-size",
    [SNDRV_PCM_HW_PARAM_PERIOD_BYTES]   = "period-bytes",
    [SNDRV_PCM_HW_PARAM_PERIODS]        = "periods",
    [SNDRV_PCM_HW_PARAM_BUFFER_TIME]    = "buffer-time",
    [SNDRV_PCM_HW_PARAM_BUFFER_SIZE]    = "buffer-size",
    [SNDRV_PCM_HW_PARAM_BUFFER_BYTES]   = "buffer-bytes",
    [SNDRV_PCM_HW_PARAM_TICK_TIME]      = "tick-time",
};

static const char *const access_labels[] = {
    [SNDRV_PCM_ACCESS_MMAP_INTERLEAVED]     = "mmap-interleaved",
    [SNDRV_PCM_ACCESS_MMAP_NONINTERLEAVED]  = "mmap-noninterleaved",
    [SNDRV_PCM_ACCESS_MMAP_COMPLEX]         = "mmap-complex",
    [SNDRV_PCM_ACCESS_RW_INTERLEAVED]       = "readwrite-interleaved",
    [SNDRV_PCM_ACCESS_RW_NONINTERLEAVED]    = "readwrite-noninterleaved",
};

static const char *const format_labels[] = {
    [SNDRV_PCM_FORMAT_S8]               = "s8",
    [SNDRV_PCM_FORMAT_U8]               = "u8",
    [SNDRV_PCM_FORMAT_S16_LE]           = "s16-le",
    [SNDRV_PCM_FORMAT_S16_BE]           = "s16-be",
    [SNDRV_PCM_FORMAT_U16_LE]           = "u16-le",
    [SNDRV_PCM_FORMAT_U16_BE]           = "u16-be",
    [SNDRV_PCM_FORMAT_S24_LE]           = "s24-le",
    [SNDRV_PCM_FORMAT_S24_BE]           = "s24-be",
    [SNDRV_PCM_FORMAT_U24_LE]           = "u24-le",
    [SNDRV_PCM_FORMAT_U24_BE]           = "u24-be",
    [SNDRV_PCM_FORMAT_S32_LE]           = "s32-le",
    [SNDRV_PCM_FORMAT_S32_BE]           = "s32-be",
    [SNDRV_PCM_FORMAT_U32_LE]           = "u32-le",
    [SNDRV_PCM_FORMAT_U32_BE]           = "u32-be",
    [SNDRV_PCM_FORMAT_FLOAT_LE]         = "float-le",
    [SNDRV_PCM_FORMAT_FLOAT_BE]         = "float-be",
    [SNDRV_PCM_FORMAT_FLOAT64_LE]       = "float64-be",
    [SNDRV_PCM_FORMAT_FLOAT64_BE]       = "float64-le",
    [SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE]   = "iec958subframe-le",
    [SNDRV_PCM_FORMAT_IEC958_SUBFRAME_BE]   = "iec958subframe-be",
    [SNDRV_PCM_FORMAT_MU_LAW]           = "mu-law",
    [SNDRV_PCM_FORMAT_A_LAW]            = "a-law",
    [SNDRV_PCM_FORMAT_IMA_ADPCM]        = "ima-adpcm",
    [SNDRV_PCM_FORMAT_MPEG]             = "mpg",
    [SNDRV_PCM_FORMAT_GSM]              = "gsm",
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,16,0)
    [SNDRV_PCM_FORMAT_S20_LE]           = "s20-le",
    [SNDRV_PCM_FORMAT_S20_BE]           = "s20-be",
    [SNDRV_PCM_FORMAT_U20_LE]           = "u20-le",
    [SNDRV_PCM_FORMAT_U20_BE]           = "u20-be",
#endif
    /* Entries for 25-30 are absent. */
    [SNDRV_PCM_FORMAT_SPECIAL]          = "special",
    [SNDRV_PCM_FORMAT_S24_3LE]          = "s24-3le",
    [SNDRV_PCM_FORMAT_S24_3BE]          = "s24-3be",
    [SNDRV_PCM_FORMAT_U24_3LE]          = "u24-3le",
    [SNDRV_PCM_FORMAT_U24_3BE]          = "u24-3be",
    [SNDRV_PCM_FORMAT_S20_3LE]          = "s20-3le",
    [SNDRV_PCM_FORMAT_S20_3BE]          = "s20-3be",
    [SNDRV_PCM_FORMAT_U20_3LE]          = "u20-3le",
    [SNDRV_PCM_FORMAT_U20_3BE]          = "u20-3be",
    [SNDRV_PCM_FORMAT_S18_3LE]          = "s18-3le",
    [SNDRV_PCM_FORMAT_S18_3BE]          = "s18-3be",
    [SNDRV_PCM_FORMAT_U18_3LE]          = "u18-3le",
    [SNDRV_PCM_FORMAT_U18_3BE]          = "u18-3be",
    [SNDRV_PCM_FORMAT_G723_24]          = "g723-24",
    [SNDRV_PCM_FORMAT_G723_24_1B]       = "g723-241b",
    [SNDRV_PCM_FORMAT_G723_40]          = "g723-40",
    [SNDRV_PCM_FORMAT_G723_40_1B]       = "g723-401b",
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
    [SNDRV_PCM_FORMAT_DSD_U8]           = "dsd-u8",
    [SNDRV_PCM_FORMAT_DSD_U16_LE]       = "dsd-u16-le",
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0)
    [SNDRV_PCM_FORMAT_DSD_U32_LE]       = "dsd-u32-le",
    [SNDRV_PCM_FORMAT_DSD_U16_BE]       = "dsd-u16-be",
    [SNDRV_PCM_FORMAT_DSD_U32_BE]       = "dsd-u23-be",
#endif
};

static const char *const subformat_labels[] = {
    [SNDRV_PCM_SUBFORMAT_STD] = "std",
};

static const char *const flag_labels[] = {
    [0] = "noresample",
    [1] = "export-buffer",
    [2] = "no-period-wakeup",
};

static const char *const info_labels[] = {
    [0]     = "mmap",
    [1]     = "mmap-valid",
    [2]     = "double",
    [3]     = "batch",
    [4]     = "interleaved",
    [5]     = "non-interleaved",
    [6]     = "complex",
    [7]     = "block-transfer",
    [8]     = "overrange",
    [9]     = "resume",
    [10]    = "pause",
    [11]    = "half-duplex",
    [12]    = "joint-duplex",
    [13]    = "sync-start",
    [14]    = "no-period-wakeup",
    [15]    = "has-wall-clock",
    [16]    = "has-link-atime",
    [17]    = "has-link-absolute-atime",
    [18]    = "has-link-estimated-atime",
    [19]    = "has-link-synchronized-atime",
    /* Does not be disclosed to userspace. */
    [20]    = "drain-trigger",
    [21]    = "fifo-in-frames",
};

static const int info_flags[] = {
    [0]     = SNDRV_PCM_INFO_MMAP,
    [1]     = SNDRV_PCM_INFO_MMAP_VALID,
    [2]     = SNDRV_PCM_INFO_DOUBLE,
    [3]     = SNDRV_PCM_INFO_BATCH,
    [4]     = SNDRV_PCM_INFO_INTERLEAVED,
    [5]     = SNDRV_PCM_INFO_NONINTERLEAVED,
    [6]     = SNDRV_PCM_INFO_COMPLEX,
    [7]     = SNDRV_PCM_INFO_BLOCK_TRANSFER,
    [8]     = SNDRV_PCM_INFO_OVERRANGE,
    [9]     = SNDRV_PCM_INFO_RESUME,
    [10]    = SNDRV_PCM_INFO_PAUSE,
    [11]    = SNDRV_PCM_INFO_HALF_DUPLEX,
    [12]    = SNDRV_PCM_INFO_JOINT_DUPLEX,
    [13]    = SNDRV_PCM_INFO_SYNC_START,
    [14]    = SNDRV_PCM_INFO_NO_PERIOD_WAKEUP,
    [15]    = SNDRV_PCM_INFO_HAS_WALL_CLOCK,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0)
    [16]    = SNDRV_PCM_INFO_HAS_LINK_ATIME,
    [17]    = SNDRV_PCM_INFO_HAS_LINK_ABSOLUTE_ATIME,
    [18]    = SNDRV_PCM_INFO_HAS_LINK_ESTIMATED_ATIME,
    [19]    = SNDRV_PCM_INFO_HAS_LINK_SYNCHRONIZED_ATIME,
#endif
    /* Does not be disclosed to userspace. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,0,0)
    [20]    = SNDRV_PCM_INFO_DRAIN_TRIGGER,
#endif
    [21]    = SNDRV_PCM_INFO_FIFO_IN_FRAMES,
};

static const char *const state_labels[] = {
    [SNDRV_PCM_STATE_OPEN] = "open",
    [SNDRV_PCM_STATE_SETUP] = "setup",
    [SNDRV_PCM_STATE_PREPARED] = "prepared",
    [SNDRV_PCM_STATE_RUNNING] = "running",
    [SNDRV_PCM_STATE_XRUN] = "xrun",
    [SNDRV_PCM_STATE_DRAINING] = "draining",
    [SNDRV_PCM_STATE_PAUSED] = "paused",
    [SNDRV_PCM_STATE_SUSPENDED] = "suspended",
    [SNDRV_PCM_STATE_DISCONNECTED] = "disconnected",
};

static unsigned int get_mask_count(void)
{
    return SNDRV_PCM_HW_PARAM_LAST_MASK - SNDRV_PCM_HW_PARAM_FIRST_MASK + 1;
}

static unsigned int get_interval_count(void)
{
    return SNDRV_PCM_HW_PARAM_LAST_INTERVAL - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL + 1;
}

static unsigned int mask_index_to_type(unsigned int index)
{
    return index + SNDRV_PCM_HW_PARAM_FIRST_MASK;
}

static unsigned int interval_index_to_type(unsigned int index)
{
    return index + SNDRV_PCM_HW_PARAM_FIRST_INTERVAL;
}

static unsigned int mask_type_to_index(unsigned int type)
{
    return type - SNDRV_PCM_HW_PARAM_FIRST_MASK;
}

static unsigned int interval_type_to_index(unsigned int type)
{
    return type - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL;
}

static struct snd_mask *refer_mask(struct snd_pcm_hw_params *params, unsigned int type)
{
    return &params->masks[mask_type_to_index(type)];
}

static struct snd_interval *refer_interval(struct snd_pcm_hw_params *params, unsigned int type)
{
    return &params->intervals[interval_type_to_index(type)];
}

static void change_mask(struct snd_pcm_hw_params *params, unsigned int type, unsigned int pos,
                        bool enable, bool exclusive)
{
    unsigned int index = pos / sizeof(params->masks[0].bits[0]);
    unsigned int offset = pos % sizeof(params->masks[0].bits[0]);
    struct snd_mask *mask = refer_mask(params, type);

    if (exclusive) {
        int i;

        for (i = 0; i < ARRAY_SIZE(mask->bits); ++i)
            mask->bits[i] = 0x00;
    }

    mask->bits[index] &= ~(1 << offset);
    if (enable)
        mask->bits[index] |= (1 << offset);

    params->rmask |= 1 << type;
}

static void change_interval(struct snd_pcm_hw_params *params, unsigned int type,
                            unsigned int min, unsigned int max, bool openmin, bool openmax,
                            bool integer, bool empty)
{
    struct snd_interval *interval = refer_interval(params, type);

    interval->min = min;
    interval->max = max;
    interval->openmin = (int)openmin;
    interval->openmax = (int)openmax;
    interval->integer = (int)integer;
    interval->empty = (int)empty;

    params->rmask |= 1 << type;
}

static void dump_mask_param(const struct snd_pcm_hw_params *hw_params,
                            unsigned int type, const char *const labels[],
                            unsigned int label_entries)
{
    const struct snd_mask *mask;
    unsigned int index;
    int i, j;

    if (type < SNDRV_PCM_HW_PARAM_FIRST_MASK ||
        type > SNDRV_PCM_HW_PARAM_LAST_MASK ||
        type >= ARRAY_SIZE(param_labels))
        return;
    printf("    %s:\n", param_labels[type]);

    mask = refer_mask((struct snd_pcm_hw_params *)hw_params, type);

    for (i = 0; i < ARRAY_SIZE(mask->bits); ++i) {
        for (j = 0; j < sizeof(mask->bits[0]) * 8; ++j) {
            index = i * sizeof(mask->bits[0]) * 8 + j;
            if (index >= label_entries)
                return;
            if ((mask->bits[i] & (1 << j)) && labels[index] != NULL)
                printf("      %s\n", labels[index]);
        }
    }
}

static void dump_interval_param(const struct snd_pcm_hw_params *hw_params,
                                unsigned int type)
{
    const struct snd_interval *interval;

    if (type < SNDRV_PCM_HW_PARAM_FIRST_INTERVAL ||
        type > SNDRV_PCM_HW_PARAM_LAST_INTERVAL ||
        type >= ARRAY_SIZE(param_labels))
        return;
    printf("    %s:\n", param_labels[type]);

    interval = refer_interval((struct snd_pcm_hw_params *)hw_params, type);

    printf("      %c%u, %u%c, ",
           interval->openmin ? '(' : '[', interval->min,
           interval->max, interval->openmax ? ')' : ']');
    if (interval->integer > 0)
        printf("integer, ");
    if (interval->empty > 0)
        printf("empty, ");
    printf("\n");
}

static int dump_hw_params_cap(const struct snd_pcm_hw_params *hw_params)
{
    int i;

    printf("  Changed parameters:\n");

    for (i = 0; i < ARRAY_SIZE(param_labels); ++i) {
        if (hw_params->cmask & (1 << i))
            printf("    %s\n", param_labels[i]);
    }

    printf("  Runtime parameters:\n");

    dump_mask_param(hw_params, SNDRV_PCM_HW_PARAM_ACCESS, access_labels,
                    ARRAY_SIZE(access_labels));
    dump_mask_param(hw_params, SNDRV_PCM_HW_PARAM_FORMAT, format_labels,
                    ARRAY_SIZE(format_labels));
    dump_mask_param(hw_params, SNDRV_PCM_HW_PARAM_SUBFORMAT, subformat_labels,
                    ARRAY_SIZE(subformat_labels));

    dump_interval_param(hw_params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS);
    dump_interval_param(hw_params, SNDRV_PCM_HW_PARAM_FRAME_BITS);
    dump_interval_param(hw_params, SNDRV_PCM_HW_PARAM_CHANNELS);
    dump_interval_param(hw_params, SNDRV_PCM_HW_PARAM_RATE);
    dump_interval_param(hw_params, SNDRV_PCM_HW_PARAM_PERIOD_TIME);
    dump_interval_param(hw_params, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
    dump_interval_param(hw_params, SNDRV_PCM_HW_PARAM_PERIODS);
    dump_interval_param(hw_params, SNDRV_PCM_HW_PARAM_BUFFER_TIME);
    dump_interval_param(hw_params, SNDRV_PCM_HW_PARAM_BUFFER_SIZE);
    dump_interval_param(hw_params, SNDRV_PCM_HW_PARAM_BUFFER_BYTES);
    dump_interval_param(hw_params, SNDRV_PCM_HW_PARAM_TICK_TIME);

    if (hw_params->flags > 0) {
        printf("      flags: \n");
        for (i = 0; i < ARRAY_SIZE(flag_labels); ++i) {
            if (hw_params->flags & (1 << i))
                printf("  %s\n", flag_labels[i]);
        }
    }

    printf("    info:\n");
    for (i = 0; i < ARRAY_SIZE(info_flags); ++i) {
        if (hw_params->info & info_flags[i])
            printf("      %s\n", info_labels[i]);
    }

    if (hw_params->msbits > 0)
        printf("      most-significant-bits:    %u\n", hw_params->msbits);

    if (hw_params->rate_num > 0 && hw_params->rate_den > 0) {
        printf("      rate_num: %u\n", hw_params->rate_num);
        printf("      rate_den: %u\n", hw_params->rate_den);
    }

    return 0;
}

static void initialize_hw_params(struct snd_pcm_hw_params *params)
{
    unsigned int type;
    int i;

    for (i = 0; i < get_mask_count(); i++) {
        int j;

        type = mask_index_to_type(i);
        for (j = 0; j < SNDRV_MASK_MAX; ++j)
            change_mask(params, type, j, true, false);
    }

    for (i = 0; i < get_interval_count(); i++) {
        type = interval_index_to_type(i);
        change_interval(params, type, 0, UINT_MAX, false, false, false, false);
    }

    params->cmask = 0;
    params->info = 0;
}

int configure_hardware(int fd, unsigned int access, snd_pcm_format_t sample_format,
                       unsigned int samples_per_frame, unsigned int frames_per_second,
                       unsigned int *frames_per_buffer, unsigned int *frames_per_period)
{
    struct snd_pcm_hw_params params = {0};
    struct snd_interval *interval;
    int err;

    initialize_hw_params(&params);

    err = ioctl(fd, SNDRV_PCM_IOCTL_HW_REFINE, &params);
    if (err < 0) {
        printf("Fail to request HW_REFINE: %s\n", strerror(errno));
        return err;
    }

    printf("Available hardware parameters:\n");
    dump_hw_params_cap(&params);

    change_mask(&params, SNDRV_PCM_HW_PARAM_ACCESS, access, true, true);
    change_mask(&params, SNDRV_PCM_HW_PARAM_FORMAT, sample_format, true, true);
    change_mask(&params, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD, true, true);

    change_interval(&params, SNDRV_PCM_HW_PARAM_CHANNELS, samples_per_frame, samples_per_frame,
                    false, false, true, false);
    change_interval(&params, SNDRV_PCM_HW_PARAM_RATE, frames_per_second, frames_per_second,
                    false, false, true, false);

    err = ioctl(fd, SNDRV_PCM_IOCTL_HW_PARAMS, &params);
    if (err < 0) {
        printf("Fail to request HW_PARAMS: %s\n", strerror(errno));
        return err;
    }

    printf("Current hardware parameters:\n");
    dump_hw_params_cap(&params);

    interval = refer_interval(&params, SNDRV_PCM_HW_PARAM_BUFFER_SIZE);
    *frames_per_buffer = interval->min;

    interval = refer_interval(&params, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
    *frames_per_period = interval->min;

    return 0;
}

static int configure_software(int fd, unsigned int frames_per_buffer, unsigned int frames_per_period)
{
    struct snd_pcm_sw_params params = {0};
    int err;

    // We can start transmission by hand.
    params.start_threshold = frames_per_buffer * 2 / 3;

    // We should keep frames one third of intermediate buffer to avoid stopping transmission automatically.
    params.stop_threshold = frames_per_buffer / 3; 

    // Relevant to time to wake from poll wait but not used.
    params.avail_min = frames_per_period;

    // No need to fill with silence frame.
    params.silence_threshold = 0;
    params.silence_size = 0;

    // Timestamping is not required in this program.
    params.tstamp_mode = SNDRV_PCM_TSTAMP_NONE;
    params.proto = 0;
    params.tstamp_type = SNDRV_PCM_TSTAMP_TYPE_GETTIMEOFDAY;

    // Nothing in kernel space.
    params.period_step = 1;
    params.sleep_min = 1;

    err = ioctl(fd, SNDRV_PCM_IOCTL_SW_PARAMS, &params);
    if (err < 0) {
        printf("Fail to request SW_PARAMS: %s\n", strerror(errno));
        return err;
    }

    return 0;
}

static int prepare_hardware(int fd)
{
    struct snd_xferi xfer = {0};
    int err;

    err = ioctl(fd, SNDRV_PCM_IOCTL_PREPARE);
    if (err < 0) {
        printf("Fail to request PREPARE: %s\n", strerror(errno));
        return err;
    }

    return 0;
}

static int transfer_frames(int fd, uint8_t *buf, unsigned int frames_per_buffer)
{
    unsigned int accumulate_frame_count;
    struct snd_pcm_status status = {0};
    struct snd_xferi xfer = {0};
    bool at_first_iteration;
    int err;

    err = ioctl(fd, SNDRV_PCM_IOCTL_STATUS, &status);
    if (err < 0) {
        printf("Fail to request STATUS: %s\n", strerror(errno));
        return err;
    }
    printf("This should be prepared: %s\n", state_labels[status.state]);

    // Supply initial frames here.
    xfer.result = 0;
    xfer.buf = buf;
    xfer.frames = frames_per_buffer * 2 / 3;
    err = ioctl(fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &xfer);
    if (err < 0) {
        printf("Fail to request initial WRITEI_FRAMES: %s\n", strerror(errno));
        return err;
    }
    accumulate_frame_count = xfer.result;
    printf("Initial frames are copied to intermediate buffer: %ld\n", xfer.result);

    //err = ioctl(fd, SNDRV_PCM_IOCTL_STATUS, &status);
    //if (err < 0) {
    //    printf("Fail to request STATUS: %s\n", strerror(errno));
    //    return err;
    //}
    //printf("This should be prepared as well: %s\n", state_labels[status.state]);

    //err = ioctl(fd, SNDRV_PCM_IOCTL_START);
    //if (err < 0) {
    //    printf("Fail to request START: %s\n", strerror(errno));
    //    return err;
    //}

    err = ioctl(fd, SNDRV_PCM_IOCTL_STATUS, &status);
    if (err < 0) {
        printf("Fail to request STATUS: %s\n", strerror(errno));
        return err;
    }
    printf("This should be running: %s\n", state_labels[status.state]);

    at_first_iteration = true;
    while (accumulate_frame_count < MY_FRAMES_PER_SECOND * MY_PLAYBACK_DURATION) {
        unsigned int rest_of_intermediate_buffer;
        unsigned int frames_copied_to_intermediate_buffer;

        err = ioctl(fd, SNDRV_PCM_IOCTL_STATUS, &status);
        if (err < 0) {
            printf("Fail to request STATUS: %s\n", strerror(errno));
            break;
        }

        printf("  State of intermediate buffer:\n");
        rest_of_intermediate_buffer = status.avail;
        if (at_first_iteration) {
            printf("    %lu frames are transferred to device since starting.\n",
                   status.avail + xfer.result - frames_per_buffer);
            at_first_iteration = false;
        }
        printf("    %u frames are waiting to transfer\n",
               frames_per_buffer - rest_of_intermediate_buffer);

        // Keep frames as half of intermediate buffer.
        xfer.result = 0;
        xfer.buf = buf;
        xfer.frames = frames_per_buffer / 2;

        // This operation can block user process till all of given frames are copied to
        // intermediate buffer.
        err = ioctl(fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &xfer);
        if (err < 0) {
            printf("Fail to request WRITEI_FRAMES: %s\n", strerror(errno));
            break;
        }
        frames_copied_to_intermediate_buffer = (unsigned int)xfer.result;

        printf("    %u frames are transferred to device\n",
               frames_copied_to_intermediate_buffer - rest_of_intermediate_buffer);
        printf("    %u frames are copied from userspace\n", (unsigned int)xfer.result);
        accumulate_frame_count += xfer.result;
    }

    if (err >= 0) {
        // For playback direction, need to wait till all of frames in intermediate buffer are
        // actually transferred. This blocks the user process.
        int e = ioctl(fd, SNDRV_PCM_IOCTL_DRAIN);
        if (e < 0)
            printf("Fail to request DRAIN: %s\n", strerror(errno));
    }

    printf("%u frames are copied and transferred\n", accumulate_frame_count);

    return err;
}

static int free_hardware(int fd)
{
    int err;

    err = ioctl(fd, SNDRV_PCM_IOCTL_HW_FREE);
    if (err < 0) {
        printf("Fail to request HW_FREE: %s\n", strerror(errno));
        return err;
    }

    return 0;
}

int main(int argc, const char **argv)
{
    const char *cdev;
    int fd;
    int protocol_version;
    unsigned int frames_per_buffer;
    unsigned int frames_per_period;
    uint8_t *buf;
    int err;

    if (argc < 2) {
        print_help(argv[0]);
        return EXIT_FAILURE;
    }
    cdev = argv[1];

    fd = open(cdev, O_RDWR);
    if (fd < 0) {
        printf("Fail to open '%s': %s\n", cdev, strerror(errno));
        return EXIT_FAILURE;
    }

    err = ioctl(fd, SNDRV_PCM_IOCTL_PVERSION, &protocol_version);
    if (err < 0) {
        printf("Fail to request PVERSION: %s\n", strerror(errno));
        goto end_open;
    }

    printf("Current protocol version over PCM interface: %d.%d.%d\n",
           SNDRV_PROTOCOL_MAJOR(protocol_version),
           SNDRV_PROTOCOL_MINOR(protocol_version),
           SNDRV_PROTOCOL_MICRO(protocol_version));

    err = configure_hardware(fd, SNDRV_PCM_ACCESS_RW_INTERLEAVED,
                             MY_SAMPLE_FORMAT,
                             MY_SAMPLES_PER_FRAME,
                             MY_FRAMES_PER_SECOND,
                             &frames_per_buffer, &frames_per_period);
    if (err < 0)
        goto end_open;

    //err = configure_software(fd, frames_per_buffer, frames_per_period);
    //if (err < 0)
    //    goto end_open;

    buf = calloc(2 * 2, frames_per_buffer);
    if (buf == NULL)
        goto end_open;

    err = prepare_hardware(fd);
    if (err < 0)
        goto end_buf;

    err = transfer_frames(fd, buf, frames_per_buffer);

    err = free_hardware(fd);
end_buf:
    free(buf);
end_open:
    close(fd);

    if (err < 0)
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
