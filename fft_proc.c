/********************************************************
 * This code is ported from the Mediatek SDK source:    *
 * ( ./MTK_APSoC_SDK/source/user/lighttpd-1.4.20/       *
 *   www/wireless/spectrum_chart.shtml )                *
 *******************************************************/

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <math.h>
#include <complex.h>

#include "fft_proc.h"
#include "ubnt.h"

/* FFT */
void fft_rec(unsigned int N, unsigned int offset, unsigned int delta,
             fft_t *x, fft_t *X, fft_t *XX)
{

    unsigned int N2 = N/2;            /* half the number of points in FFT */
    unsigned int k;                   /* generic index */
    double cs, sn;                    /* cosine and sine */
    unsigned int k00, k01, k10, k11;  /* indices for butterflies */
    double tmp0, tmp1;                /* temporary storage */

    if(N != 2)  /* Perform recursive step. */
    {
        /* Calculate two (N/2)-point DFT's. */
        fft_rec(N2, offset, 2*delta, x, XX, X);
        fft_rec(N2, offset+delta, 2*delta, x, XX, X);

        /* Combine the two (N/2)-point DFT's into one N-point DFT. */
        for(k=0; k<N2; k++)
        {
            k00 = offset + k*delta;
            k01 = k00 + N2*delta;
            k10 = offset + 2*k*delta;
            k11 = k10 + delta;
            cs = cos(2*M_PI*k/N);
            sn = sin(2*M_PI*k/N);
            tmp0 = cs * XX->x[k11][0] + sn * XX->x[k11][1];
            tmp1 = cs * XX->x[k11][1] - sn * XX->x[k11][0];
            X->x[k01][0] = XX->x[k10][0] - tmp0;
            X->x[k01][1] = XX->x[k10][1] - tmp1;
            X->x[k00][0] = XX->x[k10][0] + tmp0;
            X->x[k00][1] = XX->x[k10][1] + tmp1;
        }
    }
    else  /* Perform 2-point DFT. */
    {
        k00 = offset;
        k01 = k00 + delta;
        X->x[k01][0] = x->x[k00][0] - x->x[k01][0];
        X->x[k01][1] = x->x[k00][1] - x->x[k01][1];
        X->x[k00][0] = x->x[k00][0] + x->x[k01][0];
        X->x[k00][1] = x->x[k00][1] + x->x[k01][1];
    }
}

void fft(unsigned int N, fft_t *x, fft_t *X)
{

    /* Declare a pointer to scratch space. */
    fft_t tmpFFT;
    int j;
    for (j=0;j<N;j++) {
        tmpFFT.x[j][0] = 0;
        tmpFFT.x[j][1] = 0;
    }

    /* Calculate FFT by a recursion. */
    fft_rec(N, 0, 1, x, X, &tmpFFT);
}

void fftshift(fft_t *x, unsigned int m, unsigned int n)
{
    unsigned int m2, n2;
    unsigned int i, k;
    double tmp13, tmp24;

    m2 = m / 2;    // half of row dimension
    n2 = n / 2;    // half of column dimension

    // interchange entries in 4 quadrants, 1 <--> 3 and 2 <--> 4
    for (i = 0; i < m2; i++)
    {
        for (k = 0; k < n2; k++)
        {
            tmp13            = x->x[i][k];
            x->x[i][k]       = x->x[i+m2][k+n2];
            x->x[i+m2][k+n2] = tmp13;

            tmp24            = x->x[i+m2][k];
            x->x[i+m2][k]    = x->x[i][k+n2];
            x->x[i][k+n2]    = tmp24;
        }
    }
}

unsigned int process_spectrum_data(MTK_SPECTRUM_DATA *SD, mtk_ssd_info_t *pinfo, unsigned int chan_width, unsigned int fc_mhz)
{
    const uint8_t lna_gain_table_le_2_5[4] = { 3, 21, 33, 45 };
    const uint8_t lna_gain_table_gt_2_5[4] = { 9, 21, 33, 45 };
    const uint8_t *lna_gain_table;
    const uint16_t fs_mhz =  20 * (1 << (/*1 + */chan_width));
    const uint16_t dft_size = (1 << (7 + chan_width));
    const uint16_t freq_res_khz = (1000 * fs_mhz) / dft_size;
    const int frac_bit_num = 9; // 9:IQC output, 7:ADC output
    float total_gain;
    // float sample_rate_us = 1.0 / fs_mhz;
    float frac_scale = 1.0 / (1 << frac_bit_num);
    int i, p;
    int gsw_prd_us = 1;
    int gsw_prd_pt = gsw_prd_us * fs_mhz;
    fft_t FFT_IN = { .x = 0 };
    fft_t FFT_OUT = { .x = 0 };
    // float runtime_us = 0.0;
    double complex bins_pwr_per_win[dft_size][dft_size * 2];
    unsigned int no_gsw_cnt = 0;
    unsigned int fft_window_cnt = 0;
    uint8_t band_5g = 0;
    MTK_SPECTRUM_DATA *psd = SD;
    SPECTRAL_SAMP_DATA *pssd = pinfo->pssd;
#ifdef PRINT_TO_FILE
    char filename[32] = {0};
    snprintf(filename, sizeof(filename), "/tmp/dBm_dump_ch_%u.csv", pinfo->current_channel);
    FILE *f = fopen(filename, "w");
    if (f == NULL)
    {
        printf("Error opening file!\n");
        exit(1);
    }
#endif // PRINT_TO_FILE

    debug(MODULE, "%s: \nfs_mhz=%u \ndft_size=%u\nreq_res_khz=%u\n", __func__, fs_mhz, dft_size, freq_res_khz);

    pinfo->window_num = 0;
    if (fc_mhz > BAND_5G_START_FREQ) {
        debug(MODULE, "%s: 5G (%d mhz)\n", __func__, fs_mhz);
        band_5g = 1;
    } else {
        debug(MODULE, "%s: 2.4G (%d mhz)\n", __func__, fs_mhz);
    }
    if (fc_mhz > 2500)
        lna_gain_table = &lna_gain_table_gt_2_5[0];
    else
        lna_gain_table = &lna_gain_table_le_2_5[0];

#ifdef PRINT_TO_FILE
    /* print header */
    fprintf(f, "window_num \\ Freq MHz\t");
    for(i = 1000 * (fc_mhz - fs_mhz/2);
        i <= 1000 * (fc_mhz + fs_mhz/2) - freq_res_khz;
        i += freq_res_khz)
	{
		fprintf(f, "%d.%d\t", (i/1000), (i%1000) );
	}
    fprintf(f, "\n");
#endif // PRINT_TO_FILE

    /* process spectrum data */
    for(i = 0; i < MTK_SPECTRUM_DATA_LEN; i++) {

        if (i > 0) {
            if (((psd+i)->LNA!=(psd+i-1)->LNA)||((psd+i)->LPF!=(psd+i-1)->LPF)) {
                no_gsw_cnt = 0;
            } else {
                no_gsw_cnt++;
            }

        }
        if (no_gsw_cnt >= gsw_prd_pt) {
            fft_window_cnt++;
        } else {
            fft_window_cnt = 0;
        }

        if (fft_window_cnt >= dft_size) {
            fft_window_cnt = 0;
#ifdef PRINT_TO_FILE
            // runtime_us = i * sample_rate_us;
            // fprintf(f, "%lf\t", (double)runtime_us*(double)pow(10, -6));
            fprintf(f, "%d\t", pinfo->window_num);
#endif // PRINT_TO_FILE
            /* total gain is lna gain + lpf gain */
            if (!((psd+i)->LNA >= 0 && (psd+i)->LNA <= 3)) {
                error(MODULE,"LNA out of range %d (0<=LNA<=3)\n",  (psd+i)->LNA);
                return pinfo->window_num;
            } else {
                // printf("DEBUG: %s, lna_gain_table[%d]=%d\n", __func__, (psd+i)->LNA, lna_gain_table[(psd+i)->LNA]);
                total_gain = lna_gain_table[(psd+i)->LNA] + ((psd+i)->LNA - 3) * 2 + 18 - 13;
                total_gain = pow(10,(-0.05 * total_gain));
            }

            for(p = 0; p < dft_size; p++)
            {
                FFT_IN.x[p][0] = (psd+i-dft_size+1+p)->Ival*frac_scale*total_gain;
                FFT_IN.x[p][1] = (psd+i-dft_size+1+p)->Qval*frac_scale*total_gain;

                FFT_OUT.x[p][0] = 0;
                FFT_OUT.x[p][1] = 0;
            }

            fft(dft_size, &FFT_IN, &FFT_OUT);
            fftshift(&FFT_OUT, dft_size, 2);

            // printf("window_num %d: ", pinfo->window_num);
            unsigned int bin_count = 0;
            for(p = 0; p < dft_size; p++)
            {
                bins_pwr_per_win[p][pinfo->window_num] = FFT_OUT.x[p][1] + FFT_OUT.x[p][0] * I;
                (pssd+pinfo->window_num)->bin_pwr[p] = (int16_t) 20 * log10(cabs(bins_pwr_per_win[p][pinfo->window_num]) / dft_size);
#ifdef PRINT_TO_FILE
                 fprintf(f, "%+3d\t", (pssd+pinfo->window_num)->bin_pwr[p]);
#endif // PRINT_TO_FILE
                if (!band_5g) {
                    /* For 2.4G band scan results returned in "one column" (all the rest are equals zero) */
                    if ((pssd+pinfo->window_num)->bin_pwr[p]) {
                        (pssd+pinfo->window_num)->spectral_rssi += (-1 * (UBNT_HISTOGRAM_START_DBM + 3 * chan_width) + (pssd+pinfo->window_num)->bin_pwr[p]);
                        bin_count++;
                    }
                } else {
                    (pssd+pinfo->window_num)->spectral_rssi += (-1 * (DBM_CORRECTION_FACTOR + UBNT_HISTOGRAM_START_DBM + 3 * chan_width) + (pssd+pinfo->window_num)->bin_pwr[p]);
                    // printf( "\t%+3d", (int) (pssd+pinfo->window_num)->bin_pwr[p]);
                }
            }
            (pssd+pinfo->window_num)->bin_pwr_count = p;
            if (band_5g) {
                (pssd+pinfo->window_num)->spectral_rssi /= dft_size;
            } else {
                (pssd+pinfo->window_num)->spectral_rssi /= bin_count;
            }
#ifdef PRINT_TO_FILE
            fprintf(f, "\n");
#endif // PRINT_TO_FILE
            pinfo->window_num++;
        }
    }
#ifdef PRINT_TO_FILE
    fclose(f);
#endif // PRINT_TO_FILE

    return pinfo->window_num;
}
