#include <alsa/asoundlib.h>

#include <alloca.h>

#include <getopt.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define PI 3.1415926535897932384626433

#define ABORT(fn, err) \
    do { \
        fprintf(stderr, "ALSA error: %s: %s\n", #fn, snd_strerror(err)); \
        exit(EXIT_FAILURE); \
    } while (0)

#define CHECKED(fn, ...) \
    do { \
        int err = fn(__VA_ARGS__); \
        if (err < 0) { \
            ABORT(fn, err); \
        } \
    } while (0)

typedef int16_t sample;

void help(char const *argv0) {
    printf(
        "Usage: %s [OPTION]...\n"
        "Play an infinite sine wave tone through ALSA\n"
        "\n"
        "Options are:\n"
        "  -d DEVICE  Set ALSA device name for playback (default: \"default\")\n"
        "  -f FREQ    Set tone frequency in Hz (default: 440)\n"
        "  -h         Show this help\n"
        "  -r FREQ    Set output sample rate in Hz (default: 44100)\n"
        "  -v         Enable verbose output on stderr\n"
        , argv0
    );
}

int main(int argc, char **argv) {
    char const *device = "default";
    float frequency_hz = 440.0f;
    unsigned int rate_hz = 44100;
    bool verbose = false;

    while (1) {
        int opt = getopt(argc, argv, "d:hf:r:v");
        if (opt < 0) {
            break;
        }
        char *endptr;
        switch (opt) {
            case 'd':
                device = optarg;
                break;
            case 'f':
                frequency_hz = strtod(optarg, &endptr);
                if (endptr == optarg) {
                    help(argv[0]);
                    fprintf(stderr, "invalid float for -f: %s", optarg);
                    return EXIT_FAILURE;
                }
                break;
            case 'h':
                help(argv[0]);
                return EXIT_SUCCESS;
            case 'r':
                rate_hz = strtol(optarg, &endptr, 10);
                if (endptr == optarg) {
                    help(argv[0]);
                    fprintf(stderr, "invalid integer for -r: %s", optarg);
                    return EXIT_FAILURE;
                }
                break;
            case 'v':
                verbose = true;
                break;
            default:
                help(argv[0]);
                return EXIT_FAILURE;
        }
    }

    snd_output_t *output = NULL;
    if (verbose) {
        CHECKED(snd_output_stdio_attach, &output, stderr, 0);
    }

    snd_pcm_t *pcm = NULL;
    CHECKED(snd_pcm_open, &pcm, device, SND_PCM_STREAM_PLAYBACK, 0);

    if (verbose) {
        snd_pcm_dump(pcm, output);
    }

    snd_pcm_hw_params_t *hw_params;
    snd_pcm_hw_params_alloca(&hw_params);
    unsigned int period_time_us = 1000000;
    unsigned int buffer_time_us = period_time_us * 3;
    CHECKED(snd_pcm_hw_params_any, pcm, hw_params);
    CHECKED(snd_pcm_hw_params_set_format, pcm, hw_params, SND_PCM_FORMAT_S16);
    CHECKED(snd_pcm_hw_params_set_channels, pcm, hw_params, 1);
    CHECKED(snd_pcm_hw_params_set_rate_near, pcm, hw_params, &rate_hz, NULL);
    CHECKED(snd_pcm_hw_params_set_buffer_time_near, pcm, hw_params, &buffer_time_us, NULL);
    CHECKED(snd_pcm_hw_params_set_period_time_near, pcm, hw_params, &period_time_us, NULL);
    CHECKED(snd_pcm_hw_params, pcm, hw_params);
    if (verbose) {
        fprintf(stderr, "Using sample rate %u Hz, buffer time %d us, period time %d us\n",
            rate_hz, buffer_time_us, period_time_us);
    }

    // Create a buffer to hold exactly one period of samples. To avoid
    // confusion with ALSA's internal buffer, we call this a "clip".
    snd_pcm_uframes_t clip_size_frames;
    CHECKED(snd_pcm_hw_params_get_period_size, hw_params, &clip_size_frames, NULL);
    unsigned int clip_size_bytes = clip_size_frames * sizeof(sample);
    sample *clip = malloc(clip_size_bytes);

    // Round our target frequency so that an integer number of waves fits
    // inside the clip. This avoids sine calculations during playback because
    // we can just loop the same clip seamlessly.
    float clip_time_s = (float) clip_size_frames / (float) rate_hz;
    float waves_per_clip = frequency_hz * clip_time_s;
    frequency_hz = clip_time_s * roundf(waves_per_clip);
    if (verbose) {
        fprintf(stderr, "Using rounded frequency %f Hz\n", frequency_hz);
    }

    // Fill the clip with a sine wave.
    for (unsigned int i = 0; i < clip_size_frames; i++) {
        float sin = sinf((float) i / (float) rate_hz * frequency_hz * 2.0 * PI);
        clip[i] = (sample) (sin * 0x7FFF);
    }

    while (1) {
        snd_pcm_sframes_t result = snd_pcm_writei(pcm, clip, clip_size_frames);
        if (result < 0) {
            if (result == -EAGAIN) {
                // Should not usually happen since we requested blocking writes.
                continue;
            } else if (result == -EPIPE) {
                // Buffer underrun.
                CHECKED(snd_pcm_prepare, pcm);
            } else if (result == -ESTRPIPE) {
                // Stream suspended.
                while (true) {
                    int err = snd_pcm_resume(pcm);
                    if (err < 0) {
                        if (err == -EAGAIN) {
                            sleep(1);
                            continue;
                        } else {
                            ABORT(snd_pcm_resume, err);
                        }
                    }
                    break;
                }
                CHECKED(snd_pcm_prepare, pcm);
            } else {
                ABORT(snd_pcm_writei, result);
            }
        }
    }

    return EXIT_SUCCESS;
}
