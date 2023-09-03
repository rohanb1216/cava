#include "cavacore.h"
#ifndef M_PI
#define M_PI 3.1415926535897932385
#endif
#include <fftw3.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct cava_plan *cava_init(int number_of_bars, unsigned int rate, int channels, int autosens,
                            double noise_reduction, int low_cut_off, int high_cut_off) {
    struct cava_plan *p = malloc(sizeof(struct cava_plan));
    p->status = 0;

    // sanity checks:
    if (channels < 1 || channels > 2) {
        snprintf(p->error_message, 1024,
                 "cava_init called with illegal number of channels: %d, number of channels "
                 "supported are "
                 "1 and 2",
                 channels);
        p->status = -1;
        return p;
    }
    if (rate < 1 || rate > 384000) {
        snprintf(p->error_message, 1024, "cava_init called with illegal sample rate: %d\n", rate);
        p->status = -1;
        return p;
    }

    int buffer_size = 4096;

    if (rate > 8125 && rate <= 16250)
        buffer_size *= 2;
    else if (rate > 16250 && rate <= 32500)
        buffer_size *= 4;
    else if (rate > 32500 && rate <= 75000)
        buffer_size *= 8;
    else if (rate > 75000 && rate <= 150000)
        buffer_size *= 16;
    else if (rate > 150000 && rate <= 300000)
        buffer_size *= 32;
    else if (rate > 300000)
        buffer_size *= 64;

    if (number_of_bars < 1) {
        snprintf(p->error_message, 1024,
                 "cava_init called with illegal number of bars: %d, number of channels must be "
                 "positive integer\n",
                 number_of_bars);
        p->status = -1;
        return p;
    }

    if (number_of_bars > buffer_size / 2 + 1) {
        snprintf(p->error_message, 1024,
                 "cava_init called with illegal number of bars: %d, for %d sample rate number of "
                 "bars can't be more than %d\n",
                 number_of_bars, rate, buffer_size / 2 + 1);
        p->status = -1;
        return p;
    }
    if (low_cut_off < 1 || high_cut_off < 1) {
        snprintf(p->error_message, 1024, "low_cut_off must be a positive value\n");
        p->status = -1;
        return p;
    }
    if (low_cut_off >= high_cut_off) {
        snprintf(p->error_message, 1024, "high_cut_off must be a higher than low_cut_off\n");
        p->status = -1;
        return p;
    }
    if ((unsigned int)high_cut_off > rate / 2) {
        snprintf(p->error_message, 1024,
                 "high_cut_off can't be higher than sample rate / 2. (Nyquist Sampling Theorem)\n");
        p->status = -1;
        return p;
    }

    p->number_of_bars = number_of_bars;
    p->audio_channels = channels;
    p->rate = rate;
    p->autosens = 1;
    p->sens_init = 1;
    p->sens = 1.0;
    p->autosens = autosens;
    p->framerate = 75;
    p->frame_skip = 1;
    p->noise_reduction = noise_reduction;

    p->FFTbufferSize = buffer_size;

    p->input_buffer_size = p->FFTbufferSize * channels;

    p->input_buffer = (double *)malloc(p->input_buffer_size * sizeof(double));

    p->FFTbuffer_lower_cut_off = (int *)malloc((number_of_bars + 1) * sizeof(int));
    p->FFTbuffer_upper_cut_off = (int *)malloc((number_of_bars + 1) * sizeof(int));
    p->eq = (double *)malloc((number_of_bars + 1) * sizeof(double));
    p->cut_off_frequency = (float *)malloc((number_of_bars + 1) * sizeof(float));

    p->cava_fall = (double *)malloc(number_of_bars * channels * sizeof(double));
    p->cava_mem = (double *)malloc(number_of_bars * channels * sizeof(double));
    p->cava_peak = (double *)malloc(number_of_bars * channels * sizeof(double));
    p->prev_cava_out = (double *)malloc(number_of_bars * channels * sizeof(double));

    // Hann Window calculate multipliers
    p->hann_multiplier = (double *)malloc(p->FFTbufferSize * sizeof(double));

    for (int i = 0; i < p->FFTbufferSize; i++) {
        p->hann_multiplier[i] = 0.5 * (1 - cos(2 * M_PI * i / (p->FFTbufferSize - 1)));
    }

    // TREBLE
    p->in_l = fftw_alloc_real(p->FFTbufferSize);
    p->in_l_raw = fftw_alloc_real(p->FFTbufferSize);
    p->out_l = fftw_alloc_complex(p->FFTbufferSize / 2 + 1);
    p->plan_l = fftw_plan_dft_r2c_1d(p->FFTbufferSize, p->in_l, p->out_l, FFTW_MEASURE);

    memset(p->in_l, 0, sizeof(double) * p->FFTbufferSize);
    memset(p->in_l_raw, 0, sizeof(double) * p->FFTbufferSize);
    memset(p->out_l, 0, (p->FFTbufferSize / 2 + 1) * sizeof(fftw_complex));
    if (p->audio_channels == 2) {

        p->in_r = fftw_alloc_real(p->FFTbufferSize);
        p->in_r_raw = fftw_alloc_real(p->FFTbufferSize);
        p->out_r = fftw_alloc_complex(p->FFTbufferSize / 2 + 1);

        p->plan_r = fftw_plan_dft_r2c_1d(p->FFTbufferSize, p->in_r, p->out_r, FFTW_MEASURE);

        memset(p->in_r, 0, sizeof(double) * p->FFTbufferSize);
        memset(p->in_r_raw, 0, sizeof(double) * p->FFTbufferSize);
        memset(p->out_r, 0, (p->FFTbufferSize / 2 + 1) * sizeof(fftw_complex));
    }

    memset(p->input_buffer, 0, sizeof(double) * p->input_buffer_size);

    memset(p->cava_fall, 0, sizeof(int) * number_of_bars * channels);
    memset(p->cava_mem, 0, sizeof(double) * number_of_bars * channels);
    memset(p->cava_peak, 0, sizeof(double) * number_of_bars * channels);
    memset(p->prev_cava_out, 0, sizeof(double) * number_of_bars * channels);

    // process: calculate cutoff frequencies and eq
    int lower_cut_off = low_cut_off;
    int upper_cut_off = high_cut_off;
    int bass_cut_off = 100;
    int treble_cut_off = 500;

    // calculate frequency constant (used to distribute bars across the frequency band)
    double frequency_constant = log10((float)lower_cut_off / (float)upper_cut_off) /
                                (1 / ((float)p->number_of_bars + 1) - 1);

    float *relative_cut_off = (float *)malloc((p->number_of_bars + 1) * sizeof(float));

    p->bass_cut_off_bar = -1;
    p->treble_cut_off_bar = -1;
    int first_treble_bar = 0;
    int *bar_buffer = (int *)malloc((p->number_of_bars + 1) * sizeof(int));

    for (int n = 0; n < p->number_of_bars + 1; n++) {
        double bar_distribution_coefficient = frequency_constant * (-1);
        bar_distribution_coefficient +=
            ((float)n + 1) / ((float)p->number_of_bars + 1) * frequency_constant;
        p->cut_off_frequency[n] = upper_cut_off * pow(10, bar_distribution_coefficient);

        if (n > 0) {
            if (p->cut_off_frequency[n - 1] >= p->cut_off_frequency[n] &&
                p->cut_off_frequency[n - 1] > lower_cut_off)
                p->cut_off_frequency[n] =
                    p->cut_off_frequency[n - 1] +
                    (p->cut_off_frequency[n - 1] - p->cut_off_frequency[n - 2]);
        }

        relative_cut_off[n] = p->cut_off_frequency[n] / (p->rate / 2);
        // remember nyquist!, per my calculations this should be rate/2
        // and nyquist freq in M/2 but testing shows it is not...
        // or maybe the nq freq is in M/4

        p->eq[n] = pow(p->cut_off_frequency[n], 1);

        // the numbers that come out of the FFT are verry high
        // the EQ is used to "normalize" them by dividing with this very huge number
        p->eq[n] /= pow(2, 29);

        p->eq[n] /= log2(p->FFTbufferSize);

        p->FFTbuffer_lower_cut_off[n] = relative_cut_off[n] * (p->FFTbufferSize / 2);

        if (p->FFTbuffer_lower_cut_off[n] > p->FFTbufferSize / 2) {
            p->FFTbuffer_lower_cut_off[n] = p->FFTbufferSize / 2;
        }

        if (n > 0) {
            p->FFTbuffer_upper_cut_off[n - 1] = p->FFTbuffer_lower_cut_off[n] - 1;

            // pushing the spectrum up if the exponential function gets "clumped" in the
            // bass and caluclating new cut off frequencies
            if (p->FFTbuffer_lower_cut_off[n] <= p->FFTbuffer_lower_cut_off[n - 1]) {

                // check if there is room for more first
                int room_for_more = 0;

                if (p->FFTbuffer_lower_cut_off[n - 1] + 1 < p->FFTbufferSize / 2 + 1)
                    room_for_more = 1;

                if (room_for_more) {
                    // push the spectrum up
                    p->FFTbuffer_lower_cut_off[n] = p->FFTbuffer_lower_cut_off[n - 1] + 1;
                    p->FFTbuffer_upper_cut_off[n - 1] = p->FFTbuffer_lower_cut_off[n] - 1;

                    // calculate new cut off frequency

                    relative_cut_off[n] =
                        (float)(p->FFTbuffer_lower_cut_off[n]) / ((float)p->FFTbufferSize / 2);

                    p->cut_off_frequency[n] = relative_cut_off[n] * ((float)p->rate / 2);
                }
            }
        } else {
            if (p->FFTbuffer_upper_cut_off[n - 1] <= p->FFTbuffer_lower_cut_off[n - 1])
                p->FFTbuffer_upper_cut_off[n - 1] = p->FFTbuffer_lower_cut_off[n - 1] + 1;
        }
    }
    free(bar_buffer);
    free(relative_cut_off);
    return p;
}

void cava_execute(double *cava_in, int new_samples, double *cava_out, struct cava_plan *p) {

    // do not overflow
    if (new_samples > p->input_buffer_size) {
        new_samples = p->input_buffer_size;
    }

    int silence = 1;
    if (new_samples > 0) {
        p->framerate -= p->framerate / 64;
        p->framerate += (double)((p->rate * p->audio_channels * p->frame_skip) / new_samples) / 64;
        p->frame_skip = 1;
        // shifting input buffer
        for (uint16_t n = p->input_buffer_size - 1; n >= new_samples; n--) {
            p->input_buffer[n] = p->input_buffer[n - new_samples];
        }

        // fill the input buffer
        for (uint16_t n = 0; n < new_samples; n++) {
            p->input_buffer[new_samples - n - 1] = cava_in[n];
            if (cava_in[n]) {
                silence = 0;
            }
        }
    } else {
        p->frame_skip++;
    }

    // fill the bass, mid and treble buffers

    for (uint16_t n = 0; n < p->FFTbufferSize; n++) {
        if (p->audio_channels == 2) {
            p->in_r_raw[n] = p->input_buffer[n * 2];
            p->in_l_raw[n] = p->input_buffer[n * 2 + 1];
        } else {
            p->in_l_raw[n] = p->input_buffer[n];
        }
    }

    // Hann Window
    for (int i = 0; i < p->FFTbufferSize; i++) {
        p->in_l[i] = p->hann_multiplier[i] * p->in_l_raw[i];
        if (p->audio_channels == 2)
            p->in_r[i] = p->hann_multiplier[i] * p->in_r_raw[i];
    }

    // process: execute FFT and sort frequency bands
    fftw_execute(p->plan_l);
    if (p->audio_channels == 2) {
        fftw_execute(p->plan_r);
    }

    // process: separate frequency bands
    for (int n = 0; n < p->number_of_bars; n++) {

        double temp_l = 0;
        double temp_r = 0;

        // process: add upp FFT values within bands
        for (int i = p->FFTbuffer_lower_cut_off[n]; i <= p->FFTbuffer_upper_cut_off[n]; i++) {

            temp_l += hypot(p->out_l[i][0], p->out_l[i][1]);
            if (p->audio_channels == 2)
                temp_r += hypot(p->out_r[i][0], p->out_r[i][1]);
        }

        // getting average multiply with eq
        temp_l /= p->FFTbuffer_upper_cut_off[n] - p->FFTbuffer_lower_cut_off[n] + 1;
        temp_l *= p->eq[n];
        cava_out[n] = temp_l;

        if (p->audio_channels == 2) {
            temp_r /= p->FFTbuffer_upper_cut_off[n] - p->FFTbuffer_lower_cut_off[n] + 1;
            temp_r *= p->eq[n];
            cava_out[n + p->number_of_bars] = temp_r;
        }
    }

    // applying sens or getting max value
    if (p->autosens) {
        for (int n = 0; n < p->number_of_bars * p->audio_channels; n++) {
            cava_out[n] *= p->sens;
        }
    }
    // process [smoothing]
    int overshoot = 0;

    for (int n = 0; n < p->number_of_bars * p->audio_channels; n++) {

        p->cava_mem[n] = cava_out[n];
        if (p->autosens) {
            double diff = 1 - cava_out[n];
            if (diff < 0)
                diff = 0;
            double div = 1 / (diff + 1);
            p->cava_mem[n] = p->cava_mem[n] * (1 - div / 20);

            // check if we overshoot target height
            if (cava_out[n] > 1.0) {
                overshoot = 1;
            }
        }
    }

    // calculating automatic sense adjustment
    if (p->autosens) {
        if (overshoot) {
            p->sens = p->sens * 0.98;
            p->sens_init = 0;
        } else {
            if (!silence) {
                p->sens = p->sens * 1.002;
                if (p->sens_init)
                    p->sens = p->sens * 1.1;
            }
        }
    }
}

void cava_destroy(struct cava_plan *p) {

    free(p->input_buffer);

    free(p->hann_multiplier);
    free(p->eq);
    free(p->cut_off_frequency);
    free(p->FFTbuffer_lower_cut_off);
    free(p->FFTbuffer_upper_cut_off);
    free(p->cava_fall);
    free(p->cava_mem);
    free(p->cava_peak);
    free(p->prev_cava_out);

    fftw_free(p->in_l);
    fftw_free(p->in_l_raw);
    fftw_free(p->out_l);
    fftw_destroy_plan(p->plan_l);

    if (p->audio_channels == 2) {

        fftw_free(p->in_r);
        fftw_free(p->out_r);
        fftw_free(p->in_r_raw);
        fftw_destroy_plan(p->plan_r);
    }
}
