#include <mi_common.h>
#include <mi_isp.h>
#include <mi_sensor.h>
#include <mi_sys.h>
#include <plugin.h>
#include <isp_cus3a_if.h>
#include <time.h>

#include <pthread.h>
#include <sys/prctl.h>
#include <poll.h>
#include <poll.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
 

//Tests with custom 3A handler, didn't solve the high CPU load 

static unsigned long get_time_ms(void) // in milliseconds
{
    struct timespec ts;
    int rc = clock_gettime(CLOCK_MONOTONIC, &ts);    
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

static unsigned long LastAE;
static unsigned long AEs;
static unsigned long AEChanges;

//Custom AE handler as shown https://wx.comake.online/doc/ds82ff82j7jsd9-SSD220/customer/development/isp/3a.html
//not used
int ae_init(void* pdata, ISP_AE_INIT_PARAM *init_state)
{
    printf("****** ae_init ,shutter=%d,shutter_step=%d,sensor_gain_min=%d,sensor_gain_max=%d *******\n",
        (int)init_state->shutter,
        (int)init_state->shutter_step,
        (int)init_state->sensor_gain,
        (int)init_state->sensor_gain_max
        );
	LastAE=get_time_ms();	
    return 0;
}

void ae_run(void* pdata, const ISP_AE_INFO *info, ISP_AE_RESULT *result)
{
#define log_info 1

    // Only one can be chosen (the following three define)
#define shutter_test 0
#define gain_test 0
#define AE_sample 1


#if (shutter_test) || (gain_test)
    static int AE_period = 4;
#endif
    static unsigned int fcount = 0;
    unsigned int max = info->AvgBlkY * info->AvgBlkX;
    unsigned int avg = 0;
    unsigned int n;
#if gain_test
    static int tmp = 0;
    static int tmp1 = 0;
#endif


    result->Change              = 0;
    result->u4BVx16384          = 16384;
    result->HdrRatio            = 10; //infinity5 //TBD //10 * 1024;   //user define hdr exposure ratio
    result->IspGain             = 1024;
    result->SensorGain          = 4096;
    result->Shutter             = 20000;
    result->IspGainHdrShort     = 1024;
    result->SensorGainHdrShort  = 1024;
    result->ShutterHdrShort     = 1000;
    //result->Size         = sizeof(CusAEResult_t);

    for(n = 0; n < max; ++n)
    {
        avg += info->avgs[n].y;
    }
    avg /= max;

    result->AvgY         = avg;

#if shutter_test // shutter test under constant sensor gain
    int Shutter_Step = 100; //per frame
    int Shutter_Max = 33333;
    int Shutter_Min = 150;
    int Gain_Constant = 10240;

    result->SensorGain = Gain_Constant;
    result->Shutter = info->Shutter;

    if(++fcount % AE_period == 0)
    {
        if (tmp == 0)
        {
            result->Shutter = info->Shutter + Shutter_Step * AE_period;
            //printf("[shutter-up] result->Shutter = %d \n", result->SensorGain);
        }
        else
        {
            result->Shutter = info->Shutter - Shutter_Step * AE_period;
            //printf("[shutter-down] result->Shutter = %d \n", result->SensorGain);
        }
        if (result->Shutter >= Shutter_Max)
        {
            result->Shutter = Shutter_Max;
            tmp = 1;
        }
        if (result->Shutter <= Shutter_Min)
        {
            result->Shutter = Shutter_Min;
            tmp = 0;
        }
    }
#if log_info
    printf("fcount = %d, Image avg = 0x%X \n", fcount, avg);
    printf("tmp = %d, Shutter: %d -> %d \n", tmp, info->Shutter, result->Shutter);
#endif
#endif

#if gain_test // gain test under constant shutter
    int Gain_Step = 1024; //per frame
    int Gain_Max = 1024 * 100;
    int Gain_Min = 1024 * 2;
    int Shutter_Constant = 20000;

    result->SensorGain = info->SensorGain;
    result->Shutter = Shutter_Constant;

    if(++fcount % AE_period == 0)
    {
        if (tmp1 == 0)
        {
            result->SensorGain = info->SensorGain + Gain_Step * AE_period;
            //printf("[gain-up] result->SensorGain = %d \n", result->SensorGain);
        }
        else
        {
            result->SensorGain = info->SensorGain - Gain_Step * AE_period;
            //printf("[gain-down] result->SensorGain = %d \n", result->SensorGain);
        }
        if (result->SensorGain >= Gain_Max)
        {
            result->SensorGain = Gain_Max;
            tmp1 = 1;
        }
        if (result->SensorGain <= Gain_Min)
        {
            result->SensorGain = Gain_Min;
            tmp1 = 0;
        }
    }
#if log_info
    printf("fcount = %d, Image avg = 0x%X \n", fcount, avg);
    printf("tmp = %d, SensorGain: %d -> %d \n", tmp, info->SensorGain, result->SensorGain);
#endif
#endif

#if AE_sample
    int y_lower = 0x28;
    int y_upper = 0x38;
    int change_ratio = 10; // percentage
    int Gain_Min = 1024 * 2;
    int Gain_Max = 1024 * 1000;
    int Shutter_Min = 150;
    int Shutter_Max = 16666;//33333;

    result->SensorGain = info->SensorGain;
    result->Shutter = info->Shutter;

    if(avg < y_lower)
    {
        if (info->Shutter < Shutter_Max)
        {
            result->Shutter = info->Shutter + (info->Shutter * change_ratio / 100);
            if (result->Shutter > Shutter_Max) result->Shutter = Shutter_Max;
        }
        else
        {
            result->SensorGain = info->SensorGain + (info->SensorGain * change_ratio / 100);
            if (result->SensorGain > Gain_Max) result->SensorGain = Gain_Max;
        }
        result->Change = 1;
    }
    else if(avg > y_upper)
    {
        if (info->SensorGain > Gain_Min)
        {
            result->SensorGain = info->SensorGain - (info->SensorGain * change_ratio / 100);
            if (result->SensorGain < Gain_Min) result->SensorGain = Gain_Min;
        }
        else
        {
            result->Shutter = info->Shutter - (info->Shutter * change_ratio / 100);
            if (result->Shutter < Shutter_Min) result->Shutter = Shutter_Min;
        }
        result->Change = 1;
    }

#if 0 //infinity5 //TBD
    //hdr demo code
    result->SensorGainHdrShort = result->SensorGain;
    result->ShutterHdrShort = result->Shutter * 1024 / result->HdrRatio;
#endif

#if log_info

//if (LastCalled>)
fcount++;
AEs++;
if (get_time_ms()>LastAE+1000){
	LastAE=get_time_ms();	
    printf("%u : fcount = %d, Image avg = 0x%X \n", AEs,fcount, avg);
    printf("SensorGain: %d -> %d \n", (int)info->SensorGain, (int)result->SensorGain);
    printf("Shutter: %d -> %d \n", (int)info->Shutter, (int)result->Shutter);
	AEs=0;
}
#endif

#endif
}

void ae_release(void* pdata)
{
    printf("************* ae_release *************\n");
}


//stops AE, IspDriverThread load jumps 3 times, majestic drops 3 times
static int CUS3A_Set(){
/*Check CUS3A*/
	 
	Cus3AEnable_t *pCus3AEnable;
    pCus3AEnable = (Cus3AEnable_t *)malloc(sizeof(Cus3AEnable_t));
    pCus3AEnable->bAE = 1;
    pCus3AEnable->bAWB = 0;
    pCus3AEnable->bAF = 0;	
    int r2 = MI_ISP_CUS3A_Enable(0,pCus3AEnable);
    free(pCus3AEnable);
	 return r2;

}

static int RegisterAE(){
	ISP_AE_INTERFACE tAeIf;
     // int res=0;  
    int res = CUS3A_Init();
    printf("************CUS3A_Init : %d **********\n",res);

    /*AE*/
    tAeIf.ctrl = NULL;
    tAeIf.pdata = NULL;
    tAeIf.init = ae_init;
    tAeIf.release = ae_release;
    tAeIf.run = ae_run;
	return CUS3A_RegInterface(0,&tAeIf,NULL,NULL);

}

//Stop And Start 3A 
static void reset3a(const char *value) {
	//int res=CUS3A_RegInterface(0,NULL,NULL,NULL);
 	int r1=99,r2=99,r3=99;	

    //MI_ISP_RegisterIspApiAgent(0, NULL, NULL);
    //CUS3A_Set();
	r1=CUS3A_RegInterface(0,NULL,NULL,NULL);//No effect, AE works, starts with  [MI_ISP_CUS3A_Enable] AE = 1, AWB = 1, AF = 1	
	CUS3A_Release();//This only stops AE, majestic load drops 3 times
    printf("************  CUS3A_Release: %d **********\n",r1);
 	//CUS3A_Set();
 	//usleep(500*500);
	

	//Cus3AEnable_t En =  {1,0,0};//{0,0,0};
    //printf("Disable userspace 3a\n");
    //MI_ISP_DisableUserspace3A(0);
    //r3=MI_ISP_CUS3A_Enable(0,&En);//This stops standard cus3a
	//CUS3A_Set();

	//usleep(500*500);
	MI_ISP_EnableUserspace3A(0,NULL);//This seems to restart standard 3A[    AeInit] 
    printf("************  MI_ISP_EnableUserspace3A: %d **********\n");
	r3=RegisterAE();
    printf("************  RegisterAE: %d **********\n",r3);
	CUS3A_Set();
	RETURN("CUS3A_RegInterface reinit: %d %d %d", r1, r2, r3);
}

static void stop3a(const char *value) {
 
	int r1=CUS3A_RegInterface(0,NULL,NULL,NULL);//No effect, AE works, starts with  [MI_ISP_CUS3A_Enable] AE = 1, AWB = 1, AF = 1	
	CUS3A_Release();//This only stops AE, majestic load drops 3 times
    printf("************  CUS3A_Release: %d **********\n",r1);
 	
	RETURN("CUS3A Stopped: %d", r1);
}

static void start3a(const char *value) {
 	int r1=99,r2=99,r3=99;	
    
	MI_ISP_EnableUserspace3A(0,NULL);//This seems to restart standard 3A[    AeInit] 
    printf("************  MI_ISP_EnableUserspace3A: %d **********\n");
	r3=RegisterAE();
    printf("************  RegisterAE: %d **********\n",r3);
	CUS3A_Set();
	RETURN("RegisterAE init: %d %d %d", r1, r2, r3);
}

