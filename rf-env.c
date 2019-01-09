/*
 * Ubiquiti RF Environment tool
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <stdbool.h>
#include <math.h>

#include <ctype.h>
#include <getopt.h>
#include <sys/sysinfo.h>
#include <jansson.h>

#include "ubnt.h"
#include "fft_proc.h"


#define IFACE_MAX_LEN 32
#define NUM_SUGGESTED_CHANNELS 4


/* investigation options */
// #define ONLY_5G_SUPPORT
// #define SET_WIFI_SPECTR_SUPPORT
// #define IF_INFO_4EACH_SAMP
#define SPECTRAL_SCAN_SUPPORT


static mtk_ssd_info_t  mtk_ssdinfo;
static mtk_ssd_info_t *pinfo = &mtk_ssdinfo;

bool ubnt_spectral_table_ready = FALSE;

/*
 * Function     : print_usage
 * Description  : print the RF Environment tool usage
 * Input params : name
 * Return       : void
 *
 */
static void print_usage(char *name)
{
    printf("%s - usage\n", name);
    line();
    printf("h : print this help message\n");
    printf("i : interface name [ra0|rai0] - result file suffix\n");
    printf("r : HW radio interface name, default: rai0\n");
    // printf("b : set bandwidth channels\n");
    printf("B : set band 2.4G:0 5G:1\n");
#ifdef SPECTRAL_SCAN_SUPPORT
    printf("n : capture node [b,c,d,e]\n");
    printf("w : capture Node type [0..1]\n");
    printf("S : collect spectral scanning data\n");
#endif // SPECTRAL_SCAN_SUPPORT
    printf("v : verbose\n");
    printf("d : output to stdout instead of syslog\n");
    line();
    exit(0);
}


/*
 * Write table to file.
 */
static void write_spectrum_json_table(const char *radio_ifname)
{
    FILE *fp;
    char rftable_fname[FILE_NAME_LEN], rftable_ftemp[FILE_NAME_LEN], best_channel_fname[FILE_NAME_LEN];
    struct channel_bw best_channels[NUM_SUGGESTED_CHANNELS];
    json_t *json_root = json_object();

    json_object_set_new(json_root, "spectrum_table", prepare_spectrum_table());

    // get best channel for auto
    snprintf(best_channel_fname, sizeof(best_channel_fname), "/var/run/rftable_best_channel_%s", radio_ifname);

    fp = fopen(best_channel_fname, "w");
    if (fp) {
        ubnt_get_best_channels(radio_ifname, best_channels, NUM_SUGGESTED_CHANNELS);
        fprintf(fp, "%d", best_channels[0].channel);
        fclose(fp);
    }

    // report top best channels in inform
    json_object_set_new(json_root, "suggested_channels", prepare_suggested_channels(best_channels, NUM_SUGGESTED_CHANNELS));

    snprintf(rftable_ftemp, sizeof(rftable_ftemp), "/var/run/rftable_%s.temp", radio_ifname);
    snprintf(rftable_fname, sizeof(rftable_fname), "/var/run/rftable_%s", radio_ifname);
    json_dump_file(json_root, rftable_ftemp, JSON_COMPACT);

    rename(rftable_ftemp, rftable_fname);

    json_decref(json_root);
}

static void write_timestamp_file(char *filename)
{
    struct sysinfo si;
    FILE *uptime_table;

    uptime_table =  fopen(filename, "w");

    if (uptime_table != NULL) {
        sysinfo(&si);
        fprintf(uptime_table, "%ld\n", si.uptime);
        fclose(uptime_table);
    }
}

static void timestamp_spectrum_table(char *ifname)
{
    char buf[FILE_NAME_LEN];
    snprintf(buf, sizeof(buf), "/var/run/rftable_%s.timestamp", ifname);
    write_timestamp_file(buf);
}


static void start_spectrum_table(char *ifname)
{
    char buf[FILE_NAME_LEN];
    snprintf(buf, sizeof(buf), "/var/run/rftable_%s.start", ifname);
    write_timestamp_file(buf);
}

static void cleanup_files(char *ifname)
{
    char buf[FILE_NAME_LEN];

    snprintf(buf, sizeof(buf), "/var/run/rftable_%s.complete", ifname);
    if(!access(buf, F_OK))
    {
        info(MODULE, " remove %s", buf);
        unlink(buf);
    }
    snprintf(buf, sizeof(buf), "/var/run/rftable_%s.abnormal", ifname);
    if(!access(buf, F_OK))
    {
        info(MODULE, " remove %s", buf);
        unlink(buf);
    }
    snprintf(buf, sizeof(buf), "/var/run/rftable_%s.pre_abnormal", ifname);
    if(!access(buf, F_OK))
    {
        info(MODULE, " remove %s", buf);
        unlink(buf);
    }
    snprintf(buf, sizeof(buf), "/var/run/rftable_%s.start", ifname);
    if(!access(buf, F_OK))
    {
        info(MODULE, " remove %s", buf);
        unlink(buf);
    }
    snprintf(buf, sizeof(buf), "/var/run/rftable_%s.timestamp", ifname);
    if(!access(buf, F_OK))
    {
        info(MODULE, " remove %s", buf);
        unlink(buf);
    }
}


/**
 * This call let's mcagent know that the scan is done and we can reset the system.
 */
static void mark_spectrum_scan_done(char *ifname)
{
    char buf[FILE_NAME_LEN];
    snprintf(buf, sizeof(buf), "/var/run/rftable_%s.complete", ifname);
    write_timestamp_file(buf);
}

/* define greater than one to increase preciseness */
#define ATTEMPTS_OF_SAMPLES 3
#define ATTEMPTS_4_UTILIZATION


/*
 * Function     : main
 * Description  : entry point
 * Input params : argc, argv
 * Return       : status
 *
 */
int main(int argc, char *argv[])
{
    int i, c, attempt;
    // int  bw = -1;
    enum nl80211_band band_5g = NL80211_BAND_5GHZ;
    char radio_if_name[IFACE_MAX_LEN] = "rai0";  // default interface for MT7615
    char if_name[IFACE_MAX_LEN]       = "rai0";  // the interface name is used to create json output files
    uint16_t sample_idx;
#ifndef IF_INFO_4EACH_SAMP
    struct ath_info iface_info;
    uint8_t tmp_cu;
    uint8_t ch_gr40_cnt = 0;
    uint8_t ch_gr80_cnt = 0;
    uint8_t ch_gr160_cnt = 0;
#endif //IF_INFO_4EACH_SAMP
#ifdef SPECTRAL_SCAN_SUPPORT
    int  node_f = 0;
    char node[2] = "b";
    bool scan_flag = false;
    MTK_SPECTRUM_DATA sd[MTK_SPECTRUM_DATA_LEN];
    SPECTRAL_SAMP_DATA ssd[SPECTRAL_SAMP_DATA_LEN];
    pinfo->pssd = ssd;
#endif //SPECTRAL_SCAN_SUPPORT
    struct ubnt_spectral_info *p_usi = get_usi_p();

    int  ret = 0;

    while ((c = getopt (argc, argv, "hHi:r:b:B:n:w:Svd")) != -1) {
        switch (c) {
            case 'h':
            case 'H':
                print_usage(argv[0]);
                return ret;
            case 'i':
                snprintf(if_name, IFACE_MAX_LEN, "%s", optarg);
                break;
            case 'r':
                snprintf(radio_if_name, IFACE_MAX_LEN, "%s", optarg);
                break;
            // case 'b':
            //     bw = atoi(optarg);
            //     break;
            case 'B':
                band_5g = !!(atoi(optarg)); // default 1 --> 5G
                break;
#ifdef SPECTRAL_SCAN_SUPPORT
            case 'n':
                memcpy(node, optarg, strlen(node));
                break;
            case 'w':
                node_f = atoi(optarg);
                break;
            case 'S':
                scan_flag = true;
                break;
#endif //SPECTRAL_SCAN_SUPPORT
            case 'v':
                libubnt_log_level = (libubnt_log_level << 1);
                break;
            case 'd':
                libubnt_log_use_syslog = 0;
                break;
            default:
                print_usage(argv[0]);
                abort();
        }

#ifdef ONLY_5G_SUPPORT
        if (!band_5g) {
            warn(MODULE, "5G - supported only\n");
            return -1;
        }
#endif
    }


#ifdef SPECTRAL_SCAN_SUPPORT
    if(scan_flag) {
        if (band_5g) {
            ret = nvram_set(radio_if_name, "WirelessMode", "14"); // 11A/AN/AC mixed 5G band only            
        } else {
            ret = nvram_set(radio_if_name, "WirelessMode", "9"); // 11bgn mixed
        }
#ifdef SET_WIFI_SPECTR_SUPPORT // "IcapMode" option changing in platdep_funcs.sh
        /* set Wifi-spectrum mode */
        nvram_set(radio_if_name, "IcapMode", "2");
        ret = interface_reload(radio_if_name);
        info(MODULE, "Set WifiScan mode\n");
        sleep(3); // waiting 3 sec to change the driver mode
#endif // SET_WIFI_SPECTR_SUPPORT
    }
#endif // SPECTRAL_SCAN_SUPPORT
    pinfo->radio_ifname = if_name;

    if (!strlen(if_name)) {
        error(MODULE, "the interface name is not defined!\n");
        exit(EXIT_FAILURE);
    }
    if (ubnt_populate_chan_list(radio_if_name, pinfo, band_5g)) {
        error(MODULE, "ubnt_populate_chan_list() - failed!\n");
        exit(EXIT_FAILURE);
    }
    ubnt_init(pinfo->max_channels, pinfo->chan_list, band_5g);

    cleanup_files(if_name);
    start_spectrum_table(if_name);

    for (pinfo->channel_index = 0; pinfo->channel_index < pinfo->channels_in_bw; pinfo->channel_index++) {
        ret = set_channel(radio_if_name, pinfo->chan_list[pinfo->channel_index].channel);
        if (ret < 0) {
            error(MODULE, "Error: set_channel idx:%d, ret=%d\n", pinfo->channel_index, ret);
        } else {
            sleep(2); // waiting 2 sec to set channel
            info(MODULE, "OK: set_channel:%d, ret=%d\n", pinfo->chan_list[pinfo->channel_index].channel, ret);
#ifndef IF_INFO_4EACH_SAMP
            ch_gr40_cnt++;
            ch_gr80_cnt++;
            ch_gr160_cnt++;
#endif // !IF_INFO_4EACH_SAMP
        }

#ifndef ATTEMPTS_4_UTILIZATION
        for (attempt = 0; attempt < ATTEMPTS_OF_SAMPLES; attempt++) {
#endif // !ATTEMPTS_4_UTILIZATION
#ifdef SPECTRAL_SCAN_SUPPORT
            if(scan_flag) {
                if(!(ret = set_wifi_spectrum_param(radio_if_name, pinfo, node, node_f))) {
                    uint8_t current_channel = get_current_channel(radio_if_name);
                    info(MODULE, "get_current_channel:%d\n", current_channel);
                    if(pinfo->chan_list[pinfo->channel_index].channel != current_channel) {
                        error(MODULE, "Error: set_channel idx:%d -> ch:%d\n", pinfo->channel_index, current_channel);
                        continue;
                    } else {
                        pinfo->current_channel = pinfo->chan_list[pinfo->channel_index].channel;
                        pinfo->pssd->ch_width = pinfo->current_bw = pinfo->chan_list[pinfo->channel_index].bw;
                        info(MODULE, "OK - current_channel:%d\n", current_channel);
                    }
                } else {
                    error(MODULE, "fail: set_wifi_spectrum_param, ret:%d\n", ret);
                }

                fill_scan_data_from_file(sd);
                // TODO: scan only in BW: 20MHz; other settings does not work...
                process_spectrum_data(sd, pinfo, 0 /*p_usi->table[pinfo->channel_index].chan_width*/, 
                                    ieee80211_channel_to_frequency(pinfo->current_channel, band_5g));
            }
            else
#endif // SPECTRAL_SCAN_SUPPORT
            {
                uint8_t current_channel = get_current_channel(radio_if_name);
                if(pinfo->chan_list[pinfo->channel_index].channel != current_channel) {
                    error(MODULE, "Error: set_channel idx:%d -> ch:%d\n", pinfo->channel_index, current_channel);
                    pinfo->chan_list[pinfo->channel_index].channel = 0;
                    continue;
                } else {
                    pinfo->current_channel = pinfo->chan_list[pinfo->channel_index].channel;
                    // pinfo->pssd->ch_width = pinfo->current_bw = pinfo->chan_list[pinfo->channel_index].bw;
                    info(MODULE, "OK: current_channel:%d\n", current_channel);
                }
            }
#ifndef IF_INFO_4EACH_SAMP

#ifdef ATTEMPTS_4_UTILIZATION
        for (attempt = 0; attempt < ATTEMPTS_OF_SAMPLES; attempt++) {
#endif // ATTEMPTS_4_UTILIZATION
            get_athstat(radio_if_name, &iface_info);
            info(MODULE, "Ch: %d; utilization: %d\n", pinfo->current_channel, iface_info.ath_11n_info.cu_total);
#ifdef UTILIZATION_AVERAGE
            p_usi->table[pinfo->channel_index].utilization += iface_info.ath_11n_info.cu_total;
#else
            tmp_cu = MAX(p_usi->table[pinfo->channel_index].utilization, iface_info.ath_11n_info.cu_total);
            ubnt_set_channel_utilization(pinfo->current_channel, BW_20, tmp_cu);
            // duplicate utilization for all bandwidth
            if(band_5g) {
                if (ch_gr40_cnt == 2) {
                    debug(MODULE, "Ch (BW_40) idx: %d; utilization: %d\n", (pinfo->channel_index-i), tmp_cu);
                    for (i = 0; i < 2; i++)
                        tmp_cu = MAX(p_usi->table[pinfo->channel_index-i].utilization, tmp_cu);
                    for (i = 0; i < 2; i++)
                        ubnt_set_channel_utilization(p_usi->table[pinfo->channel_index-i].channel, BW_40, tmp_cu);
                    ch_gr40_cnt = 0;
                }
                if (ch_gr80_cnt == 4) {
                    debug(MODULE, "Ch (BW_80) idx: %d; utilization: %d\n", (pinfo->channel_index-i), tmp_cu);
                    for (i = 0; i < 4; i++)
                        tmp_cu = MAX(p_usi->table[pinfo->channel_index-i].utilization, tmp_cu);
                    for (i = 0; i < 4; i++)
                        ubnt_set_channel_utilization(p_usi->table[pinfo->channel_index-i].channel, BW_80, tmp_cu);
                    ch_gr80_cnt = 0;
                }
                if (ch_gr160_cnt == 8) {
                    debug(MODULE, "Ch (BW_160) idx: %d; utilization: %d\n", (pinfo->channel_index-i), tmp_cu);
                    for (i = 0; i < 8; i++)
                        tmp_cu = MAX(p_usi->table[pinfo->channel_index-i].utilization, tmp_cu);
                    for (i = 0; i < 8; i++)
                        ubnt_set_channel_utilization(p_usi->table[pinfo->channel_index-i].channel, BW_160, tmp_cu);
                    ch_gr160_cnt = 0;
                }
            } else { // 2G
                ubnt_set_channel_utilization(pinfo->current_channel, BW_40, tmp_cu);
            }
#endif // !UTILIZATION_AVERAGE
#ifdef ATTEMPTS_4_UTILIZATION
        }
#endif // ATTEMPTS_4_UTILIZATION
#endif // !IF_INFO_4EACH_SAMP
            for (sample_idx = 0; sample_idx < pinfo->window_num; sample_idx++) {
                pinfo->pssd[sample_idx].ch_width = BW_20;
                ubnt_process_spectral_data(pinfo, sample_idx);
                pinfo->pssd[sample_idx].ch_width = BW_40;
                ubnt_process_spectral_data(pinfo, sample_idx);
                if(band_5g) {
                    pinfo->pssd[sample_idx].ch_width = BW_80;
                    ubnt_process_spectral_data(pinfo, sample_idx);
                    pinfo->pssd[sample_idx].ch_width = BW_160;
                    ubnt_process_spectral_data(pinfo, sample_idx);
                }
            }
#ifndef ATTEMPTS_4_UTILIZATION
        }
#endif // !ATTEMPTS_4_UTILIZATION
#ifndef IF_INFO_4EACH_SAMP
 #ifdef UTILIZATION_AVERAGE
        p_usi->table[pinfo->channel_index].utilization /= attempt;
        // duplicate utilization for all bandwidth
        ubnt_set_channel_utilization(pinfo->current_channel, BW_40, p_usi->table[pinfo->channel_index].utilization);
        if(band_5g) {
            ubnt_set_channel_utilization(pinfo->current_channel, BW_80, p_usi->table[pinfo->channel_index].utilization);
            ubnt_set_channel_utilization(pinfo->current_channel, BW_160, p_usi->table[pinfo->channel_index].utilization);
        }
 #endif // UTILIZATION_AVERAGE
#endif // !IF_INFO_4EACH_SAMP
        ubnt_process_channel_data(pinfo->current_channel, BW_20);
        ubnt_process_channel_data(pinfo->current_channel, BW_40);
        if(band_5g) {
            ubnt_process_channel_data(pinfo->current_channel, BW_80);
            ubnt_process_channel_data(pinfo->current_channel, BW_160);
        }
    }

    write_spectrum_json_table(if_name);

#ifdef SPECTRAL_SCAN_SUPPORT
    /* restore Normal mode */
    if(scan_flag) {
        ret = nvram_set(radio_if_name, "IcapMode", "0");
        // ret = interface_reload(radio_if_name);
        // there is no need to apply (in case softrestart applies)
        info(MODULE, "Restore Normal mode\n");
    }
#endif // SPECTRAL_SCAN_SUPPORT

    mark_spectrum_scan_done(if_name);
    timestamp_spectrum_table(if_name);

    ubnt_cleanup(pinfo);

    info(MODULE, "END SCAN - %s\n", (band_5g) ? "5G" : "2G");

    return ret;
}
