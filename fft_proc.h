#ifndef FFT_PROC_H
#define FFT_PROC_H


#include "mt_spectr.h"

#define DFT_size_MAX 512
#define DBM_CORRECTION_FACTOR -5

/* investigation options */
// #define PRINT_TO_FILE

typedef struct fft_t { double x[DFT_size_MAX][2]; } fft_t;

unsigned int process_spectrum_data(MTK_SPECTRUM_DATA *SD, mtk_ssd_info_t *pinfo, unsigned int chan_width, unsigned int fc_mhz);

#endif //FFT_PROC_H
