#ifndef __UBNT_H__
#define __UBNT_H__

#define MODULE "ubnt-rf-env"

#include "libubnt/libubnt.h"
#include "libubnt/status.h"
#include "libubnt/wireless.h"
#include "libubnt/rfscan.h"
#include "libubnt/log.h"


#define line()          printf("----------------------------------------------------\n")

/* debug */
#define UBNT_WIFI_DBG 1

/* 11AC will have max of 512 bins */
#define MAX_NUM_BINS 512
#define MAX_NUM_CHANNELS 256
#define ARRAY_SIZE(ar) (sizeof(ar)/sizeof(ar[0]))
#define MAX(x, y) (((x) > (y)) ? (x) : (y))

#define FILE_NAME_LEN 64

struct chan_info {
    uint16_t channel;
    uint8_t  bw;
    uint16_t freq_center;
#if UBNT_WIFI_CHAN_UTIL_RAW_CNTS
    struct channel_properties chan_properties;
#endif
};

typedef struct spectral_samp_data {
    int16_t     spectral_rssi;                                  /* indicates RSSI */
    uint8_t     spectral_max_exp;                               /* indicates the max exp */
    uint16_t    bin_pwr_count;                                  /* indicates the number of FFT bins */
    int16_t     bin_pwr[MAX_NUM_BINS];                          /* contains FFT magnitudes */
    int16_t     noise_floor;                                    /* indicates the current noise floor */
    uint32_t    ch_width;                                       /* Channel width 20/40/80 MHz */
} SPECTRAL_SAMP_DATA, *P_SPECTRAL_SAMP_DATA;

struct channel_bw_item {
    uint16_t channel;
    uint8_t bw;
};

struct ubnt_channel_info {
    uint16_t index;
    uint16_t mac_ch_idx;                                         /* maximum channel index */
    struct channel_bw_item *ch_list;                             /* channel list */
};

typedef struct mtk_ssd_info {
    uint8_t max_channels;
    uint8_t channels_in_bw;
    uint8_t current_channel;
    uint8_t current_bw;
    uint8_t channel_index;
    uint16_t window_num;
    struct chan_info *chan_list;
    char   *radio_ifname;
    SPECTRAL_SAMP_DATA *pssd;
} mtk_ssd_info_t;


/**
 * enum nl80211_band - Frequency band
 * @NL80211_BAND_2GHZ: 2.4 GHz ISM band
 * @NL80211_BAND_5GHZ: around 5 GHz band (4.9 - 5.7 GHz)
 * @NL80211_BAND_60GHZ: around 60 GHz band (58.32 - 64.80 GHz)
 */
enum nl80211_band {
	NL80211_BAND_2GHZ,
	NL80211_BAND_5GHZ,
#if 0 //BAND_60GHZ_SUPPORT
	NL80211_BAND_60GHZ,
#endif
};

enum bandwith {
    BW_20 = 0,
    BW_40,
    BW_80,
    BW_160,
};

#define MAX_BW_2G BW_40
#define MAX_BW_5G BW_160
#define BW_QTY(band_5g) ((band_5g) ? MAX_BW_5G : MAX_BW_2G)

struct ubnt_spectral_info *get_usi_p(void);
int ieee80211_channel_to_frequency(int chan, enum nl80211_band band);
json_t* prepare_spectrum_table(void);
int ubnt_get_best_channels(const char* radio_ifname, struct channel_bw *best_channels, int num_best_channels);
void ubnt_process_channel_data(uint16_t channel, uint8_t bw);
void ubnt_set_channel_utilization(uint16_t channel, uint8_t bw, uint8_t utilization);

int ubnt_populate_chan_list(char *interface, mtk_ssd_info_t *pinfo, enum nl80211_band band_5g);
void ubnt_init(uint8_t max_channels, struct chan_info *chan_list, enum nl80211_band band_5g);
void ubnt_cleanup(mtk_ssd_info_t *pinfo);

void ubnt_process_spectral_data(mtk_ssd_info_t *pinfo, uint16_t sample_idx);

#endif //__UBNT_H__
