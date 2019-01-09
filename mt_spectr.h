#ifndef MT_SPECTR_H
#define MT_SPECTR_H

#include "ubnt.h"

#define IQ_FILE_LOC "/var/run/WifiSpectrum_IQ.txt"
#define LNA_LPF_FILE_LOC "/var/run/WifiSpectrum_LNA_LPF.txt"

#ifndef TRUE
#define TRUE    (1)
#endif

#ifndef FALSE
#define FALSE   !(TRUE)
#endif

#define MTK_SPECTRUM_DATA_LEN 32768
#define SPECTRAL_SAMP_DATA_LEN 512


/* the maximum channel list for the 13th Region in 5G */
#define DEF_5G_CHANNELS_20  {36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165}

/* the maximum channels the 5th Region in 2.4G (20 & 40 MHz)*/
#define DEF_2G_CHANNEL_20 {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}

/* OID:  Copyright (c) Ralink Technology Corporation All Rights Reserved. */
#define OID_802_11_CURRENTCHANNEL                        0x0712

#define OID_802_11_WIFISPECTRUM_SET_PARAMETER            0x0970
#define OID_802_11_WIFISPECTRUM_GET_CAPTURE_STOP_INFO    0x0971
#define OID_802_11_WIFISPECTRUM_DUMP_DATA                0x0972
#define OID_802_11_WIFISPECTRUM_GET_CAPTURE_BW           0x0973
#define OID_802_11_WIFISPECTRUM_GET_CENTRAL_FREQ         0x0974


#if WIRELESS_EXT <= 11
#ifndef SIOCDEVPRIVATE
#define SIOCDEVPRIVATE              0x8BE0
#endif /* !SIOCDEVPRIVATE */
#ifndef SIOCIWFIRSTPRIV
#define SIOCIWFIRSTPRIV             SIOCDEVPRIVATE
#endif /* !SIOCIWFIRSTPRIV */
#endif /* WIRELESS_EXT <= 11 */

#ifndef AP_MODE
#define RT_PRIV_IOCTL               (SIOCIWFIRSTPRIV + 0x0E)
#else
#define RT_PRIV_IOCTL               (SIOCIWFIRSTPRIV + 0x01)
#endif    /* AP_MODE */

#define RT_QUERY_ATE_TXDONE_COUNT                   0x0401
#define OID_GET_SET_TOGGLE                          0x8000
/* OID */

typedef struct _MTK_SPECTRUM_DATA {
    int Ival;
    int Qval;
    int LNA;
    int LPF;
} MTK_SPECTRUM_DATA, *P_MTK_SPECTRUM_DATA;

typedef struct _BW_UI_CFG {
	unsigned char	Priority;
	unsigned int	G_Rate;
	unsigned int	M_Rate;
	unsigned int	G_Time_Ratio;
	unsigned int	M_Time_Ratio;
} BW_UI_CFG, *PBW_UI_CFG;

typedef struct _ICAP_WIFI_SPECTRUM_SET_STRUC_T {
	unsigned int  fgTrigger;
	unsigned int  fgRingCapEn;
	unsigned int  u4TriggerEvent;
	unsigned int  u4CaptureNode;
	unsigned int  u4CaptureLen;
	unsigned int  u4CapStopCycle;
	unsigned int  u4MACTriggerEvent;
	unsigned int  u4SourceAddressLSB;
	unsigned int  u4SourceAddressMSB;
	unsigned int  u4Band;
	unsigned char ucBW;
	unsigned char aucReserved[3];
} ICAP_WIFI_SPECTRUM_SET_STRUC_T, *P_ICAP_WIFI_SPECTRUM_SET_STRUC_T;


int getWifiSpectrumBWandFreq(char *interface, mtk_ssd_info_t *pinfo);
int set_wifi_spectrum_param(char* interface, mtk_ssd_info_t *pinfo, char* node, int node_f);
void fill_scan_data_from_file(MTK_SPECTRUM_DATA *SD);
void cleanup_scan_data_files(void);
// void ubnt_process_spectral_data(uint16_t channel, struct ubnt_spectral_info *usi, SPECTRAL_SAMP_DATA *ssd);
int nvram_set(char *interface, char *option, char *value);
int interface_reload(char *interface);
// int set_int_iwpriv(char *interface, char *option, int value);
int set_channel(char *interface, uint8_t channel);
uint8_t get_current_channel(char *interface);

#endif //MT_SPECTR_H
