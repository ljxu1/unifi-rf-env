#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <linux/wireless.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <math.h>
#include <limits.h>

#include "mt_spectr.h"

static const char *typedev[2] = {"2860", "rtdev"};

int nvram_set(char *interface, char *option, char *value)
{
    char cmd_buff[64] = {0};
    int ret = 0;

    snprintf(cmd_buff, sizeof(cmd_buff), "nvram_set.sh %s %s %s",
                                    typedev[!!strcmp(interface, "ra0")], option, value);
    debug(MODULE, "%s: cmd:%s\n",__func__, cmd_buff);
    if ((ret = system(cmd_buff)) && ret < 0) {
            error(MODULE, "failure : cmd:%s: ret=%d\n", cmd_buff, ret);
    }
    return ret;
}

int interface_reload(char *interface)
{
    char cmd_buff[64] = {0};
    int ret = 0;

    snprintf(cmd_buff, sizeof(cmd_buff), "ifconfig %s down up", interface);
    debug(MODULE, "%s: cmd:%s\n",__func__, cmd_buff);
    if ((ret = system(cmd_buff)) && ret < 0) {
            error(MODULE, "failure : cmd:%s: ret=%d\n", cmd_buff, ret);
    }
    return ret;
}

/* IOCTL exchange */
static int SetRalinkOid(char *pIntfName,
        unsigned short ralink_oid,
        unsigned short BufLen,
        void *pInBuf)
{
    int skfd;
    struct iwreq lwreq;
    int rv = 0;

    BW_UI_CFG cfg[4];
    memcpy(cfg, pInBuf, BufLen);

    if((skfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        error(MODULE, "Open socket failed.\n");
        return -1;
    }
    strcpy(lwreq.ifr_ifrn.ifrn_name, pIntfName);

    lwreq.u.data.flags = ralink_oid | OID_GET_SET_TOGGLE;

    lwreq.u.data.pointer = (caddr_t) pInBuf;
    lwreq.u.data.length = BufLen;

    int tmp = ioctl(skfd, RT_PRIV_IOCTL, &lwreq);
    if(tmp < 0)
    {
        // error(MODULE, "SetRalinkOid:: Interface (%s) doesn't accept private ioctl...(0x%04x)\n", pIntfName, ralink_oid);
        rv = -1;
    }
    close(skfd);

    return rv;
}

static int QueryRalinkOid(char *pIntfName,
        unsigned short ralink_oid,
        char *arg,
        unsigned short BufLen,
        void *pOutBuf)
{
    int skfd;
    struct iwreq lwreq;
    int rv = 0;

    if((skfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        error(MODULE, "Open socket failed.\n");
        return -1;
    }

    if ((arg != NULL) && (strlen(arg) < BufLen))
        strncpy(pOutBuf, arg, BufLen);

    sprintf(lwreq.ifr_ifrn.ifrn_name, pIntfName, strlen(pIntfName));
    lwreq.u.data.flags = ralink_oid;
    lwreq.u.data.pointer = (caddr_t) pOutBuf;
    lwreq.u.data.length = BufLen;

    if(ioctl(skfd, RT_PRIV_IOCTL, &lwreq) < 0)
    {
        // error(MODULE, "QueryRalinkOid:: Interface (%s) doesn't accept private ioctl...(0x%04x)\n", pIntfName, ralink_oid);
        rv = -1;
    }

    close(skfd);

    return rv;
}


int set_channel(char *interface, uint8_t channel)
{
    int ret = 0;

    ret = SetRalinkOid(interface,
            OID_802_11_CURRENTCHANNEL,
            sizeof(uint8_t),
            (void *)&channel);

    return ret;
}

uint8_t get_current_channel(char *interface)
{
    uint8_t channel = 0;

    QueryRalinkOid(interface,
            OID_802_11_CURRENTCHANNEL,
            NULL,
            sizeof(uint8_t),
            (void *)&channel);

    return channel;
}

int getWifiSpectrumBWandFreq(char *interface, mtk_ssd_info_t *pinfo)
{
    uint8_t CaptureBw = 0;
    uint16_t CentralFreq = 0;
    int status = -1;

    QueryRalinkOid(interface,
                OID_802_11_WIFISPECTRUM_GET_CAPTURE_BW,
                NULL,
                sizeof(uint8_t),
                (void *)&CaptureBw);

    status = QueryRalinkOid(interface,
                OID_802_11_WIFISPECTRUM_GET_CENTRAL_FREQ,
                NULL,
                sizeof(uint16_t),
                (void *)&CentralFreq);

    if (!status && CaptureBw) {
        CaptureBw -= 1;
        CentralFreq -= 10*(1 + CaptureBw);
        info(MODULE, "CaptureBw:%u; CentralFreq:%u\n", CaptureBw, CentralFreq);
        // pinfo->current_channel = (uint8_t)ieee80211_mhz2ieee(CentralFreq);
        // pinfo->current_bw = CaptureBw;
        return status;
    }

    return -1;
}

#define WAITING_SCAN_ATTEMPTS 4

int set_wifi_spectrum_param(char* interface, mtk_ssd_info_t *pinfo, char* node, int node_f)
{

    int status = -1;
    int sc_attempt_cnt = 0;
    uint16_t node_pref;

    {
        //run IOCTL command here
        ICAP_WIFI_SPECTRUM_SET_STRUC_T WifiSpecInfo;
        WifiSpecInfo.fgTrigger=1;
        WifiSpecInfo.fgRingCapEn=0;
        WifiSpecInfo.u4Band=0;
        WifiSpecInfo.u4CapStopCycle=0;
        WifiSpecInfo.u4MACTriggerEvent=0;
        WifiSpecInfo.u4SourceAddressLSB=0;
        WifiSpecInfo.u4SourceAddressMSB=0;
        WifiSpecInfo.u4TriggerEvent=0;
        WifiSpecInfo.ucBW = 0;//uss->chan_width+1;

        if(node_f)
            node_pref = 0x2000;
        else
            node_pref = 0x3000;
        /* Antenna selection */
        if (!strcmp(node, "b"))
        {
            WifiSpecInfo.u4CaptureNode=node_pref+0xb;
        }
        else if (!strcmp(node, "c"))
        {
            WifiSpecInfo.u4CaptureNode=node_pref+0xc;
        }
        else if (!strcmp(node, "d"))
        {
            WifiSpecInfo.u4CaptureNode=node_pref+0xd;
        }
        else if (!strcmp(node, "e"))
        {
            WifiSpecInfo.u4CaptureNode=node_pref+0xe;
        }

        WifiSpecInfo.u4CaptureLen=0;
        status = SetRalinkOid(interface,
                    OID_802_11_WIFISPECTRUM_SET_PARAMETER,
                    sizeof(WifiSpecInfo),
                    (void *)&WifiSpecInfo);
        if(status < 0)
        {
            error(MODULE, "IOCTL failed OID_802_11_WIFISPECTRUM_SET_PARAMETER\n");
            return status;
        }
        status = SetRalinkOid(interface,
                    OID_802_11_WIFISPECTRUM_GET_CAPTURE_STOP_INFO,
                    0,
                    NULL);
        usleep(10000);
        while(status < 0 && sc_attempt_cnt < WAITING_SCAN_ATTEMPTS)
        {
            warn(MODULE, "IOCTL attempt OID_802_11_WIFISPECTRUM_GET_CAPTURE_STOP_INFO - is not ready\n");
            status = SetRalinkOid(interface,
                    OID_802_11_WIFISPECTRUM_GET_CAPTURE_STOP_INFO,
                    0,
                    NULL);
            sc_attempt_cnt++;
        }
        if (sc_attempt_cnt == WAITING_SCAN_ATTEMPTS)
        {
            error(MODULE, "IOCTL failed OID_802_11_WIFISPECTRUM_GET_CAPTURE_STOP_INFO after %d attempts\n", sc_attempt_cnt);
            return status;
        }

        status = SetRalinkOid(interface,
                    OID_802_11_WIFISPECTRUM_DUMP_DATA,
                    0,
                    NULL);

        debug(MODULE, "OID_802_11_WIFISPECTRUM_DUMP_DATA Done, Status = %d\n", status);
        status = getWifiSpectrumBWandFreq(interface, pinfo);
    }
    return status;
}


void cleanup_scan_data_files(void)
{
    char buf[FILE_NAME_LEN];

    snprintf(buf, sizeof(buf), "%s", IQ_FILE_LOC);
    if(!access(buf, F_OK))
    {
        info(MODULE, " remove %s", buf);
        unlink(buf);
    }
    snprintf(buf, sizeof(buf), "%s", LNA_LPF_FILE_LOC);
    if(!access(buf, F_OK))
    {
        info(MODULE, " remove %s", buf);
        unlink(buf);
    }
}


void fill_scan_data_from_file(MTK_SPECTRUM_DATA *SD)
{
    MTK_SPECTRUM_DATA *psd = SD;
    FILE *fp_iq;
    FILE *fp_lna_lpf;
    int i;

    fp_iq = fopen(IQ_FILE_LOC,"r");
    if(fp_iq == NULL)
    {
        error(MODULE, "Failed to run the command for %s\n", IQ_FILE_LOC);
        return;
    }

    fp_lna_lpf = fopen(LNA_LPF_FILE_LOC,"r");
    if(fp_lna_lpf == NULL)
    {
        error(MODULE, "Failed to run the command for %s\n", LNA_LPF_FILE_LOC);
        fclose(fp_iq);
        return;
    }

    memset(psd, 0, sizeof(MTK_SPECTRUM_DATA));
    for (i = 0; i < MTK_SPECTRUM_DATA_LEN; i++)
    {
        if(fscanf(fp_iq, "%d\t%d", &psd->Ival, &psd->Qval) == 1) {
            debug(MODULE, "%d\t%d\n",psd->Ival, psd->Qval);
        }
        if(fscanf(fp_lna_lpf, "%d\t%d",&psd->LNA, &psd->LPF) == 1);
        psd++;
    }

    if(fp_iq)
        fclose(fp_iq);
    if(fp_lna_lpf)
        fclose(fp_lna_lpf);
}
