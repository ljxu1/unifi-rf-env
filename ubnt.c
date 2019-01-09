/*
 * Ubiquiti RF Environment tool
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include <stdbool.h>
#include <ctype.h>
#include <getopt.h>

#include "ubnt.h"
#include "fft_proc.h"
#include "mt_spectr.h"

/* static var */
static struct ubnt_spectral_info usi;

struct ubnt_spectral_info *get_usi_p(void) {
    return &usi;
}

/* Perform json output */
json_t* prepare_spectrum_table(void)
{
    return prepare_spectrum_table_usi(&usi);
}

int ieee80211_channel_to_frequency(int chan, enum nl80211_band band)
{
    /* see 802.11 17.3.8.3.2 and Annex J
     * there are overlapping channel numbers in 5GHz and 2GHz bands */
    if (chan <= 0)
        return 0; /* not supported */
    switch (band) {
    case NL80211_BAND_2GHZ:
        if (chan == 14)
            return 2484;
        else if (chan < 14)
            return 2407 + chan * 5;
        break;
    case NL80211_BAND_5GHZ:
        if (chan >= 182 && chan <= 196)
            return 4000 + chan * 5;
        else
            return 5000 + chan * 5;
        break;
#if BAND_60GHZ_SUPPORT
    case NL80211_BAND_60GHZ:
        if (chan < 5)
            return 56160 + chan * 2160;
        break;
#endif
    default:
        ;
    }
    return 0; /* not supported */
}

int ubnt_get_best_channels(const char* radio_ifname, struct channel_bw *best_channels, int num_best_channels)
{
    return get_best_channels(&usi, radio_ifname, best_channels, num_best_channels);
}

void ubnt_calculate_interference(struct ubnt_spectral_stats *uss)
{
    int i;

    if (!uss->total_samples) {
        uss->interference += UBNT_HISTOGRAM_START_DBM + 3 * uss->chan_width;
        return;
    }

    uint32_t wifi_samples = 0;
    for (i = 0; i < UBNT_RSSI_HISTOGRAM_SIZE; i++) {
        wifi_samples += uss->normalized_rssi_histogram[i];
        if (wifi_samples >=
                UBNT_INTERFERENCE_POWER_PERCENTILE) {
            break;
        }
    }
    /* we want to count upto the bin just short of the
       required area under the power spectral density
       function. when we break out of the for loop above,
       i points to the bin that caused the area to exceed
       the specified percentile. so, i - 1 would point to
       the bin we are interested in except when i is 0.
     */
    uss->interference = (i) ? (((i - 1) * 2) + 1) : 1;
    /* convert interference based on RSSI to dBm based on noise-floor */
    uss->interference = ubnt_convert_to_dbm(uss->interference, uss->chan_width);
#if UBNT_WIFI_DBG
    printf("interference %d, chan %d, bw %d, wifi_samples %u, "
            "total_samples %u\n", uss->interference, uss->channel,
            uss->chan_width, wifi_samples, uss->total_samples);
#endif
}

void ubnt_normalize_rssi_histogram(struct ubnt_spectral_stats *uss)
{
    int i;

    if (!uss->total_samples) {
        memset(uss->normalized_rssi_histogram, 0, sizeof(uss->normalized_rssi_histogram));
        return;
    }

    for (i = 0; i < UBNT_RSSI_HISTOGRAM_SIZE; i++)
        uss->normalized_rssi_histogram[i] = (uss->rssi_histogram[i] * 100) / uss->total_samples;
}

void ubnt_process_channel_data(uint16_t channel, uint8_t bw)
{
    uint16_t i;
    for (i = 0; i < usi.count; i++) {
        struct ubnt_spectral_stats *uss = &usi.table[i];
        if ((channel == uss->channel) && (bw == uss->chan_width)) {

            ubnt_normalize_rssi_histogram(uss);

            // note that this calculation depends on the normalized histogram.
            ubnt_calculate_interference(uss);
        }
    }
}

void ubnt_set_channel_utilization(uint16_t channel, uint8_t bw, uint8_t utilization)
{
    uint16_t i;
    for (i = 0; i < usi.count; i++) {
        struct ubnt_spectral_stats *uss = &usi.table[i];
        if ((channel == uss->channel) && (bw == uss->chan_width)) {
            debug(MODULE, "%s: ch:%d bw:%d cu:%d\n", __func__, channel, bw, utilization);
            uss->utilization = utilization;
        }
    }
}

int ubnt_populate_chan_list(char *interface, mtk_ssd_info_t *pinfo, enum nl80211_band band_5g)
{
    // struct chan_info *chan_info_list = &pinfo->chan_list[0];
    uint8_t bw_num = BW_QTY(band_5g) + 1;
    uint8_t channels5G[] = DEF_5G_CHANNELS_20;
    uint8_t channels2G[] = DEF_2G_CHANNEL_20;
    uint8_t *channels;
    uint8_t i, j, bw;


    if (band_5g) {
        debug(MODULE, "band 5g...");
        pinfo->channels_in_bw = ARRAY_SIZE(channels5G);
        channels = channels5G;
    } else {
        debug(MODULE, "band 2g...");
        pinfo->channels_in_bw = ARRAY_SIZE(channels2G);
        channels = channels2G;
    }
    pinfo->chan_list = (struct chan_info *)malloc(bw_num * pinfo->channels_in_bw * sizeof(struct chan_info));
    if (!pinfo->chan_list) {
        info(MODULE, "malloc failed to alloc channnel list");
        perror("malloc");
        return -1;
    }
    debug(MODULE, "interface: %s; channels_in_bw: %d\n", interface, pinfo->channels_in_bw);

    /* sort the channels on the basis of bw */
    for (i=0, j=0; i < pinfo->channels_in_bw; i++) {
        uint8_t current_channel = channels[i];
        if(!set_channel(interface, current_channel)) {
            current_channel = get_current_channel(interface);
            debug(MODULE, "current_channel: %d\n", current_channel);
            if (channels[i] == current_channel) {
                channels[j] = current_channel;
                j++;
            } else {
                warn(MODULE, "current_channel (%d) !=  channels[i] (%d)\n", current_channel, channels[i]);
            }
        } else {
            error(MODULE, "set_channel - fail\n");
        }
    }
    pinfo->channels_in_bw = j;

    debug(MODULE, "channels_in_bw: %d\n", pinfo->channels_in_bw);
    for(i = 0; i < pinfo->channels_in_bw; i++) {
        printf("ch: %d\n", channels[i]);
    }

    for (j = 0, bw = 0; bw < bw_num; bw++) {
        for (i = 0; i < pinfo->channels_in_bw; i++) {
            // ignore the last channel (165) in 5G for 40 & 80 MHz
            if (channels[i] == 165 /*channels[channels_in_bw-1]*/ && bw > 0 && band_5g)
                continue;
            pinfo->chan_list[j].channel = channels[i];
            pinfo->chan_list[j].freq_center = ieee80211_channel_to_frequency(channels[i], band_5g);
            pinfo->chan_list[j++].bw = bw;
        }
    }
    pinfo->max_channels = j;

    return 0;
}

// void print_usi_table(void)
// {
//     int i;

//     printf("%s - START!\n", __func__);
//     for (i=0; i<usi.count; i++) {
//         printf("!!!! Ch:%d,\tBW:%d,\tF:%d\n",
//         usi.table[i].channel,
//         usi.table[i].chan_width,
//         usi.table[i].freq_center);
//     }
// }

void ubnt_init(uint8_t max_channels, struct chan_info *chan_list, enum nl80211_band band_5g)
{
    int i;

    usi.table = (struct ubnt_spectral_stats *)malloc(sizeof(struct ubnt_spectral_stats) * max_channels);
    usi.count = max_channels;
    memset(usi.table, 0, sizeof(struct ubnt_spectral_stats) * usi.count);
    debug(MODULE, "max_channels: %d\n", usi.count);

    for (i = 0; i < usi.count; i++) {
        usi.table[i].channel = chan_list[i].channel;
        usi.table[i].chan_width = chan_list[i].bw;
        usi.table[i].freq_center = chan_list[i].freq_center;
#if UBNT_WIFI_DBG
        debug(MODULE, "channel %d, bw %d, center freq %d", chan_list[i].channel,
                chan_list[i].bw, chan_list[i].freq_center);
#endif
    }
    /* Initialize the spectral table */
    if (band_5g) {
        usi.width = UBNT_RSSI_SPECTRUM_WIDTH_5G;
    } else {
        usi.width = UBNT_RSSI_SPECTRUM_WIDTH_2G;
    }
    // print_usi_table();
    usi.rssi_histograms_counts = (uint32_t*)malloc(usi.width * sizeof(uint32_t));
    if (usi.rssi_histograms_counts == NULL) {
        error(MODULE, "UOH, not enough memory!!!");
        return;
    }
    memset(usi.rssi_histograms_counts, 0, usi.width * sizeof(uint32_t));
    usi.rssi_histograms = (uint32_t**)malloc(usi.width * sizeof(uint32_t*));
    if (usi.rssi_histograms == NULL) {
        error(MODULE, "UOH, not enough memory!!!");
        return;
    }
    uint32_t* rssi_histogram_data = (uint32_t*) malloc(UBNT_RSSI_HISTOGRAM_SIZE * sizeof(uint32_t) * usi.width);
    if (rssi_histogram_data == NULL) {
        error(MODULE, "UOH, not enough memory!");
        return;
    }
    for (i = 0; i < usi.width; i++, rssi_histogram_data += UBNT_RSSI_HISTOGRAM_SIZE) {
        usi.rssi_histograms[i] = rssi_histogram_data;
        memset(usi.rssi_histograms[i], 0, UBNT_RSSI_HISTOGRAM_SIZE * sizeof(uint32_t));
    }
    // print_usi_table();
}

void ubnt_cleanup(mtk_ssd_info_t *pinfo)
{
    usi.num_processed = 0;

    if (usi.table) {
        free(usi.table);
        usi.table = NULL;
        usi.count = 0;
    }
    if (*usi.rssi_histograms)
        free(*usi.rssi_histograms);
    if (usi.rssi_histograms)
        free(usi.rssi_histograms);

    if (usi.rssi_histograms_counts)
        free(usi.rssi_histograms_counts);

    if (pinfo->chan_list)
        free(pinfo->chan_list);
}

void ubnt_process_spectral_data(mtk_ssd_info_t *pinfo, uint16_t sample_idx)
{
    struct ubnt_spectral_stats *uss;
    SPECTRAL_SAMP_DATA *ssd = &pinfo->pssd[sample_idx];
    int i;
    uint8_t  channel = pinfo->current_channel;
    uint8_t  chan_width = ssd->ch_width;
    uint8_t  bw = 20 * pow(2, 0 /*chan_width*/);
    uint16_t freq_center = 0;
    static bool print_once = false;
#ifdef IF_INFO_4EACH_SAMP
    struct ath_info iface_info;
#endif //IF_INFO_4EACH_SAMP

    for (i = 0; i < usi.count; i++) {
        if (usi.table[i].channel == channel && usi.table[i].chan_width == chan_width) {
            break;
        }
    }
    if (i == usi.count && !print_once) {
        warn(MODULE, "%s: unexpected spectral msg chan %d, ch_width %d\n",
               __func__, channel, chan_width);
        for (i = 0; i < usi.count; i++)
            debug(MODULE, "%d %d\n", usi.table[i].channel, usi.table[i].chan_width);
        print_once = true;
        return;
    }
    uss = &usi.table[i];
    freq_center = usi.table[i].freq_center;

    if (ssd->spectral_rssi < 0) {
        /* ignore samples with -ve rssi? */
#if UBNT_WIFI_DBG_VERBOSE
        printf("rssi %d! \n", ssd->spectral_rssi);
#endif
        return;
    }
#if 0 && UBNT_WIFI_DBG_VERBOSE
    else {
        printf("c %d w %d r %d\n", channel, chan_width, pinfo->pssd->spectral_rssi);
    }
#endif

    /* each bin in the histogram is for 2dBm */
    if (ssd->spectral_rssi >= (UBNT_RSSI_HISTOGRAM_SIZE * 2)) {
        /* account samples above the histogram size in the last bin */
        uss->rssi_histogram[UBNT_RSSI_HISTOGRAM_SIZE - 1]++;
    } else if (ssd->spectral_rssi <= 0) {
        uss->rssi_histogram[0]++;
    } else {
        uss->rssi_histogram[ssd->spectral_rssi >> 1]++;
    }

    /* if histogram counts are about to overflow, divide all
       bins by 2 (effectively giving 50% weightage to previous
       samples)
    */
    if (uss->total_samples == UINT_MAX) {
        for (i = 0; i < UBNT_RSSI_HISTOGRAM_SIZE; i++)
            uss->rssi_histogram[i] >>= 1;
        uss->total_samples >>= 1;
    } else {
        uss->total_samples++;
    }

#ifdef IF_INFO_4EACH_SAMP
    /* utilization */
    get_athstat(pinfo->radio_ifname, &iface_info);
    uss->utilization = iface_info.ath_11n_info.cu_total;
#endif //IF_INFO_4EACH_SAMP

    // if (ssd->bin_pwr_count && !print_once) {
    //     debug(MODULE, "%s: ssd->bin_pwr_count %d\n", __func__, ssd->bin_pwr_count);
    //     for (i = 0; i < ssd->bin_pwr_count; i++)
    //         printf("%d ", ssd->bin_pwr[i]);
    //     print_once = true;
    //     printf("\n");
    // }

    /* process the per-mhz histogram for the full spectrum */
    for (i = 0; i < ssd->bin_pwr_count; i++) {
        int16_t  bin;
        uint32_t freq;
        int32_t  log_bin_pwr;

        if (ssd->bin_pwr[i] == 0)
            ssd->bin_pwr[i] = 1;
        log_bin_pwr = ssd->bin_pwr[i];

        /* map bin to frequency */
        freq = freq_center - bw/2 + (bw * i) / ssd->bin_pwr_count;
        if (freq >= UBNT_RSSI_SPECTRUM_START_5G) {
            bin = freq - UBNT_RSSI_SPECTRUM_START_5G;
        } else {
            bin = freq - UBNT_RSSI_SPECTRUM_START_2G;
        }
        if ((bin < 0) || (bin >= usi.width)) {
            // outside our range
            continue;
        }
        usi.rssi_histograms_counts[bin]++;
        if (log_bin_pwr >= (UBNT_RSSI_HISTOGRAM_SIZE * 2)) {
            usi.rssi_histograms[bin][UBNT_RSSI_HISTOGRAM_SIZE-1]++;
        } else {
            usi.rssi_histograms[bin][log_bin_pwr >> 1]++;
        }
    }
}
