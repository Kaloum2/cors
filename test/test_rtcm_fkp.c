/* Round-trip test: encode RTCM 1034/1035 then decode via input_rtcm3 */
#include "cors.h"

static int decode_frame(const uint8_t *buf, int nb, int expect_type)
{
    rtcm_t rtcm={0};
    int i, ret=0;

    init_rtcm(&rtcm);
    for (i=0;i<nb;i++) {
        ret=input_rtcm3(&rtcm,buf[i]);
        if (ret!=0) break;
    }
    free_rtcm(&rtcm);
    if (ret!=10) {
        fprintf(stderr,"FKP decode type %d: expected ret=10 got %d\n",expect_type,ret);
        return 0;
    }
    return 1;
}

static int test_fkp_type(int type)
{
    rtcm_fkp_sat_t sat[2];
    uint8_t buff[512];
    int nb, i;

    memset(sat,0,sizeof(sat));
    sat[0].prn=12; sat[0].iod=3;
    sat[0].gn0=0.001; sat[0].ge0=-0.002; sat[0].gn1=0.0001; sat[0].ge1=0.0002;
    sat[1].prn=18; sat[1].iod=1;
    sat[1].gn0=0.003; sat[1].ge0=0.001; sat[1].gn1=-0.0001; sat[1].ge1=0.0003;

    nb=rtcm_encode_fkp(type,1001,sat,2,(char*)buff);
    if (nb<=0) {
        fprintf(stderr,"rtcm_encode_fkp(%d) failed\n",type);
        return 0;
    }
    if (buff[0]!=0xD3) {
        fprintf(stderr,"FKP frame missing RTCM3 preamble\n");
        return 0;
    }
    return decode_frame(buff,nb,type);
}

int main(void)
{
    int ok=1;

    if (!test_fkp_type(1034)) ok=0;
    if (!test_fkp_type(1035)) ok=0;

    if (ok) {
        printf("test_rtcm_fkp: OK (1034/1035 encode+decode)\n");
        return 0;
    }
    printf("test_rtcm_fkp: FAIL\n");
    return 1;
}
