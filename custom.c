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
 


static unsigned long get_time_ms(void) // in milliseconds
{
    struct timespec ts;
    int rc = clock_gettime(CLOCK_MONOTONIC, &ts);    
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

static unsigned long LastAE;
static unsigned long AEs;
static unsigned long AEChanges;

 
static void set_blackwhite(const char *value) {
	bool index = strlen(value) ? atoi(value) : false;

	MI_ISP_IQ_COLORTOGRAY_TYPE_t color;
	color.bEnable = index;

	if (MI_ISP_IQ_SetColorToGray(0, &color)) {
		RETURN("MI_ISP_IQ_SetColorToGray failed");
	}

	RETURN("Set blackwhite: %d", index);
}

static void set_brightness(const char *value) {
	MI_ISP_IQ_BRIGHTNESS_TYPE_t brightness;
	if (MI_ISP_IQ_GetBrightness(0, &brightness)) {
		RETURN("MI_ISP_IQ_GetBrightness failed");
	}

	if (!strlen(value)) {
		RETURN("Get brightness: %d", brightness.stManual.stParaAPI.u32Lev);
	}

	int index = atoi(value);
	brightness.bEnable = SS_TRUE;
	brightness.enOpType = SS_OP_TYP_MANUAL;
	brightness.stManual.stParaAPI.u32Lev = index;

	if (MI_ISP_IQ_SetBrightness(0, &brightness)) {
		RETURN("MI_ISP_IQ_SetBrightness failed");
	}

	RETURN("Set brightness: %d", index);
}

static void set_contrast(const char *value) {
	MI_ISP_IQ_CONTRAST_TYPE_t contrast;
	if (MI_ISP_IQ_GetContrast(0, &contrast)) {
		RETURN("MI_ISP_IQ_GetContrast failed");
	}

	if (!strlen(value)) {
		RETURN("Get contrast: %d", contrast.stManual.stParaAPI.u32Lev);
	}

	int index = atoi(value);
	contrast.bEnable = SS_TRUE;
	contrast.enOpType = SS_OP_TYP_MANUAL;
	contrast.stManual.stParaAPI.u32Lev = index;

	if (MI_ISP_IQ_SetContrast(0, &contrast)) {
		RETURN("MI_ISP_IQ_SetContrast failed");
	}

	RETURN("Set contrast: %d", index);
}

static void set_rotation(const char *value) {
	int index = strlen(value) ? atoi(value) : -1;
	bool mirror, flip;

	switch (index) {
		case 0:
			mirror = false;
			flip = false;
			break;

		case 1:
			mirror = true;
			flip = false;
			break;

		case 2:
			mirror = false;
			flip = true;
			break;

		case 3:
			mirror = true;
			flip = true;
			break;

		default:
			RETURN("Unknown rotation: %d", index);
	}

	if (MI_SNR_SetOrien(0, mirror, flip)) {
		RETURN("MI_SNR_SetOrien failed");
	}

	RETURN("Set rotation: %d", index);
} 

//=========================================================
//============  Custom AE routine ===========================

static int MaxAEChange=10;
static int ISPFrameDelay=100;
static int ISPMaxGain=0;

static u32 g_ThreadRun = 0;
static pthread_t g_Cus3aThread;

static void* Cus3aThreadProcRoutine(void* data);
static int Cus3aThreadProcAE(void);

static pthread_mutex_t g_ThreadLock = PTHREAD_MUTEX_INITIALIZER;

static int Cus3aThreadInitialization(void)
{
    int r;
    pthread_attr_t attr;

    g_ThreadRun = 1;

    pthread_attr_init (&attr);
    pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
    r = pthread_create(&g_Cus3aThread, NULL, Cus3aThreadProcRoutine, (void*)NULL);
    pthread_attr_destroy (&attr);
    if(r == 0)
        return 0;
    else
    {
        printf("Failed to create Cus3A thread. err=%d", r);
        return r;
    }
}

static void Cus3aThreadRelease(void)
{
    if(g_ThreadRun == 1)
    {
        g_ThreadRun = 0;
        pthread_join(g_Cus3aThread, NULL);
    }
    printf("===== Cus3aThreadRelease() =====\n");
}

static void* Cus3aThreadProcRoutine(void* data)
{
    int fd0 = 0, fd1 = 0;

    Cus3AOpenIspFrameSync(&fd0, &fd1);
    printf("fd0 = 0x%x, fd1 = 0x%x\n", fd0, fd1);
    while(g_ThreadRun)
    {
        unsigned int status = Cus3AWaitIspFrameSync(fd0, fd1, 500); //500
        pthread_mutex_lock(&g_ThreadLock);
        if(status != 0)
        {
            Cus3aThreadProcAE();
        }else{
            Cus3aThreadRelease();
            printf("==========> Custom AE STOPPED <=============");
        }
        pthread_mutex_unlock(&g_ThreadLock);
        CamOsMsSleep(ISPFrameDelay);//Limit Frames read from ISP
    }
    Cus3ACloseIspFrameSync(fd0, fd1);
    return 0;
}

static int Cus3aDoAE(ISP_AE_INFO *info, ISP_AE_RESULT *result)
{
    unsigned int max = info->AvgBlkY * info->AvgBlkX;
    unsigned int avg = 0;
    unsigned int n;

    result->Change              = 0;
    result->u4BVx16384          = 16384;
    result->HdrRatio            = 10;
    result->IspGain             = 1024;
    result->SensorGain          = 4096;
    result->Shutter             = 10000; //20000; Change
    result->IspGainHdrShort     = 1024;
    result->SensorGainHdrShort  = 1024;
    result->ShutterHdrShort     = 2000;
    //result->Size         = sizeof(CusAEResult_t);

    for(n = 0; n < max; ++n)
    {
        avg += info->avgs[n].y;
    }
    avg /= max;

    result->AvgY         = avg;
    //printf("avg = 0x%X \n", avg);

 

#if 1 //AE_EXAMPLE  // DoAE sample code
    {
        int y_lower = 0x28;
        int y_upper = 0x38;
        int change_ratio = MaxAEChange; // percentage , orginal value 10
        int Gain_Min = 1024 * 2;
        int Gain_Max = 2048 * 200; //was 1024
        int Shutter_Min = 150;
        int Shutter_Max = 8192;//limited to 122fps //33333 , limited to 30fps

        if (ISPMaxGain>0)
            Gain_Max=ISPMaxGain;

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

#if (ENABLE_DOAE_MSG)
        printf("AvgY = %ld, Shutter = %ld(%ld), Gain = %ld(%ld)\n", result->AvgY, result->Shutter, info->Shutter, result->SensorGain, info->SensorGain);
#endif
    }
#endif
    return 0;
}

static int Cus3aThreadProcAE(void)
{
    MI_ISP_AE_HW_STATISTICS_t *pAvg = NULL;
    MI_S32 sret = MI_ISP_OK;
    CusAEInfo_t tCusAeInfo;
    CusAEResult_t tCusAeResult;
    int nCh = 0;
    ISP_AE_INFO tAeInfo;
    ISP_AE_RESULT tAeResult;

    /*AE avg statistics*/
    pAvg = malloc(sizeof(MI_ISP_AE_HW_STATISTICS_t));
    sret = MI_ISP_AE_GetAeHwAvgStats(nCh, pAvg);
    if(sret != MI_ISP_OK)
    {
        printf("%s,%d error!\n", __FUNCTION__, __LINE__);
        goto Cus3aThreadProcAE_EXIT;
    }

    /*Check AE param*/
    memset(&tCusAeInfo, 0, sizeof(CusAEInfo_t));
    sret = MI_ISP_CUS3A_GetAeStatus(nCh, &tCusAeInfo);
    if(sret != MI_ISP_OK)
    {
        printf("%s,%d error!\n", __FUNCTION__, __LINE__);
        goto Cus3aThreadProcAE_EXIT;
    }

    tAeInfo.Size        = sizeof(ISP_AE_INFO);
    tAeInfo.hist1       = NULL;
    tAeInfo.hist2       = NULL;
    tAeInfo.AvgBlkX     = tCusAeInfo.AvgBlkX;
    tAeInfo.AvgBlkY     = tCusAeInfo.AvgBlkY;
    tAeInfo.avgs        = (ISP_AE_SAMPLE*)pAvg->nAvg;
    tAeInfo.Shutter     = tCusAeInfo.Shutter;
    tAeInfo.SensorGain  = tCusAeInfo.SensorGain;
    tAeInfo.IspGain     = tCusAeInfo.IspGain;
    tAeInfo.ShutterHDRShort = tCusAeInfo.ShutterHDRShort;
    tAeInfo.SensorGainHDRShort = tCusAeInfo.SensorGainHDRShort;
    tAeInfo.IspGainHDRShort = tCusAeInfo.IspGainHDRShort;

    memset(&tAeResult, 0, sizeof(ISP_AE_RESULT));
    tAeResult.Size = sizeof(ISP_AE_RESULT);

    Cus3aDoAE(&tAeInfo, &tAeResult);

    if(tAeResult.Change)
    {
        AEChanges++;
        memset(&tCusAeResult, 0, sizeof(CusAEResult_t));
        tCusAeResult.Size         = sizeof(CusAEResult_t);
        tCusAeResult.Change       = tAeResult.Change;
        tCusAeResult.u4BVx16384   = tAeResult.u4BVx16384;
        tCusAeResult.HdrRatio     = tAeResult.HdrRatio;
        tCusAeResult.ShutterHdrShort = tAeResult.ShutterHdrShort;
        tCusAeResult.Shutter      = tAeResult.Shutter;
        tCusAeResult.IspGain      = tAeResult.IspGain;
        tCusAeResult.SensorGain   = tAeResult.SensorGain;
        tCusAeResult.SensorGainHdrShort = tAeResult.SensorGainHdrShort;
        tCusAeResult.IspGainHdrShort = tAeResult.IspGainHdrShort;
        tCusAeResult.AvgY         = tAeResult.AvgY;

        sret = MI_ISP_CUS3A_SetAeParam(nCh, &tCusAeResult);
        if(sret != MI_ISP_OK)
        {
            printf("%s,%d error!\n", __FUNCTION__, __LINE__);
            goto Cus3aThreadProcAE_EXIT;
        }
    }

    AEs++;
    if (get_time_ms()>LastAE+1000){
    	LastAE=get_time_ms();	
        printf("Frames:%d, AE Changes:%d \n", AEs,AEChanges);
        printf("SensorGain:%d , Shutter:%d \n", (int)tAeResult.SensorGain, (int)tAeResult.Shutter);        
    	AEs=0;
        AEChanges=0;
    }    

Cus3aThreadProcAE_EXIT:

    free(pAvg);
    return 0;
}

static void stop3a(const char *value) {
 
	int r1=CUS3A_RegInterface(0,NULL,NULL,NULL);//No effect, AE works, starts with  [MI_ISP_CUS3A_Enable] AE = 1, AWB = 1, AF = 1	
	CUS3A_Release();//This only stops AE, majestic load drops 3 times
    printf("************  CUS3A_Release: %d **********\n",r1);
 	
	RETURN("CUS3A Stopped: %d", r1);
}
static bool Custom3AStarted=false;

static int FrameDelayMS=100, PercentChange=10;
 
 /**
  * expects two numbers separated by comma, first is FPS to read, second is max percent change.
  * for example 20,15
  * Can be called when already started only to change params
 */
static void customAE(const char *value) {
 	
    int fps=0, percent=0, MaxGain=0;
 	 int result;

	int index = atoi(value);
    if (strlen(value) > 0 ){        
        if (strchr(value, ',') != NULL) {            
            result = sscanf(value, "%d,%d,%d", &fps, &percent, &MaxGain);
            if (result == 3) {
            // Use sscanf to read three integers separated by commas
                printf("FPS: %d, Percent AE Change: %d, Max Gain: %d\n", fps, percent, MaxGain);
            } else if (result == 2) {
                // Use sscanf to read two integers separated by a comma
                printf("FPS: %d, Percent AE Change: %d\n", fps, percent);
            } else {
                printf("Failed to parse two or three numbers from the params.\n");
            }
        } else {
            fps = atoi(value);        
        }
        if (percent!=0)
            MaxAEChange = percent;
        if (fps!=0)
            ISPFrameDelay = 1000/fps;   
        if (MaxGain!=0)
            ISPMaxGain = MaxGain;     

          
    }

    
	printf("FrameDelay: %dms Percent AE Change: %d, MaxGain: %d\n", ISPFrameDelay, MaxAEChange, MaxGain);
     
    if (!Custom3AStarted){
        stop3a(value);//Stop built-in 3A module   
	    Cus3aThreadInitialization();
        Custom3AStarted=true;
    }
	RETURN("CustomAE v:0.3a started");
}

static void stopAE(const char *value) {
    Cus3aThreadRelease();
    Custom3AStarted=false;
    RETURN("custom AE stopped");
}


static void get_version() {
	MI_SYS_Version_t version;
	if (MI_SYS_GetVersion(&version)) {
		RETURN("MI_SYS_GetVersion failed");
	}

	RETURN("%s", version.u8Version);
}

static table custom[] = {
	{ "blackwhite", &set_blackwhite },
	{ "brightness", &set_brightness },
	{ "contrast", &set_contrast },
	{ "rotation", &set_rotation },
	{ "version", &get_version },
//	{ "stop3a", &stop3a },
//    { "start3a", &start3a },
//    { "reset3a", &reset3a },
    { "customAE", &customAE },
    { "stopAE", &stopAE },
	{ "help", &get_usage },
};

config common = {
	.list = custom,
	.size = sizeof(custom) / sizeof(table),
};
