
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <time.h>
#include <signal.h>

#include <sys/time.h>
#include <linux/input.h>

#include "cutils/log.h"
#include "cutils/properties.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define VOLUME_MAX			(15)
#define VOLUME_DEFAULT		(12)	// 80%
#define VOLUME_RATIO		(4)	// 2dB
#define VOLUME_LEVEL		(((VOLUME_MAX-1)*VOLUME_RATIO) - 1)

#define BRIGHTNESS_MAX		(4)
#define BRIGHTNESS_DEFAULT	(4)

#define LONGKEY_CNT			10	// 1sec
#define REPEAT_CNT			3	// 3sec

#define PATH_PANEL			"/sys/bus/i2c/devices/3-004c"
#define PATH_AUDIO			"/sys/bus/i2c/devices/1-0038"

typedef enum
{	
	KEY_VOLUME_DOWN = 114,		// Volume down
	KEY_VOLUME_UP,				// Volume up
	KEY_BRIGHTNESS_DOWN	= 224,	// Brightness down
	KEY_BRIGHTNESS_UP,			// Brightness up
}_KEY_DATA;
 
typedef enum
{
	tId_Key,
	tId_Max
}_TIMER_ID;


#define GET_TIMER_ID(id)		(SIGRTMAX-tId_Max+id)
#define SET_TIMER_ID(id)		(id - SIGRTMAX + tId_Max)

#define TIMEOUT_TS		10000//15000
#define TIMEOUT_KEY		100

typedef struct
{
	int			sleep;			// sleep status
	int			mode;			// mode
	int			cnt;
	int			delay;
	int			long_tick;		// long key check
	int			repeat_tick;	// repeat key check
	int			key_code	;	// key code
	int			volume;			// volume
	int			brightness;		// brightness
}SYS_STATUS;
SYS_STATUS g_status;
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

static timer_t	g_tId[tId_Max];

void timer_handler(int id, siginfo_t * info, void *context);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
int dev_wr_status(char *filename, char *basedir, int status)
{
	FILE  *fp;
	char tmp[100];

	memset(tmp, 0x00, sizeof(tmp));
	sprintf(tmp, "%s/%s", basedir, filename);

	fp = fopen(tmp, "w");
	if (fp == NULL) {
		printf("failed to open %s\n", tmp);
		return -1;
	}
	fprintf(fp, "%d", status);
	fclose(fp);

	return 0;	
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
int dev_rd_status(char *filename, char *basedir)
{
	FILE  *fp;
	int ret;
	char tmp[100];

	memset(tmp, 0, sizeof(tmp));
	sprintf(tmp, "%s/%s", basedir, filename);

	fp = fopen(tmp, "r");
	if (fp == NULL) {
		printf("failed to open %s\n", tmp);
		return -1;
	}
	fscanf(fp, "%d\n", &ret);
	fclose(fp);

	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////
int dev_remove_file(char *filename, char *basedir)
{
	char tmp[100];

	memset(tmp, 0, sizeof(tmp));
	sprintf(tmp, "%s/%s", basedir, filename);
	return remove(tmp);
}
///////////////////////////////////////////////////////////////////////////////////////
int ar_atoi(char *s)
{
        int k = 0;

        k = 0;
        while (*s != '\0' && *s >= '0' && *s <= '9') {
                k = 10 * k + (*s - '0');
                s++;
        }
        return k;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void set_volume(int level)
{
	char buf[PROPERTY_VALUE_MAX+1];	
	dev_wr_status("volume", PATH_AUDIO, level);	
	sprintf(buf, "%d", level);
	property_set("persist.prazen.volume", buf);
	g_status.volume = level;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int get_volume(void)
{
	char buf[PROPERTY_VALUE_MAX+1];
	property_get("persist.prazen.volume", buf, NULL);
	return ar_atoi(buf);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void set_brightness(int value)
{
	char buf[PROPERTY_VALUE_MAX+1];	
	ALOGD("[armon] set brightness = %d\n", value);
	dev_wr_status("brightness", PATH_PANEL, value);	
	sprintf(buf, "%d", value);
	property_set("persist.prazen.brightness", buf);
	g_status.brightness = value;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int get_brightness(void)
{
	char buf[PROPERTY_VALUE_MAX+1];
	property_get("persist.prazen.brightness", buf, NULL);
	return ar_atoi(buf);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void timer_init(void)
{
	int i=0;

	for (i=0; i<tId_Max; i++)
		g_tId[i] = 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int timer_start(int id, int delay)
{
	timer_t timerid;
	struct sigaction sigact;
	struct sigevent sigev;
	struct itimerspec itval;
	struct itimerspec oitval;

	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = SA_SIGINFO;
	sigact.sa_sigaction = timer_handler;

	// Set up sigaction to catch signal
	if (sigaction(GET_TIMER_ID(id), &sigact, NULL) == -1) {
		perror("sigaction failed");
		return -1;
	}
			
	// Create the POSIX timer to generate signo
	sigev.sigev_notify = SIGEV_SIGNAL;
	sigev.sigev_signo = GET_TIMER_ID(id);
	sigev.sigev_value.sival_ptr = &timerid;

	if (timer_create(CLOCK_REALTIME, &sigev, &timerid) == 0) {
		itval.it_value.tv_sec = delay / 1000;
		itval.it_value.tv_nsec = (long)(delay % 1000) * (1000000L);
		itval.it_interval.tv_sec = itval.it_value.tv_sec;
		itval.it_interval.tv_nsec = itval.it_value.tv_nsec;

		if (timer_settime(timerid, 0, &itval, &oitval) != 0) {
			perror("time_settime error!");
		}
	} else {
		perror("timer_create error!");
		return -1;
	}

	g_tId[id] =  timerid;

	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void timer_stop(int id)
{
	if (g_tId[id]) {
		timer_delete(g_tId[id]);
		g_tId[id] = 0;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int is_timer_running(int id)
{
	return (g_tId[id] ? 1 : 0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void timer_handler(int id, siginfo_t *info, void *context)
{
	siginfo_t *siginfo = info;
	void *tmp = context;
		
	switch (SET_TIMER_ID(id)) 
	{
		case tId_Key:
			if (g_status.long_tick < LONGKEY_CNT)
			{
				g_status.long_tick++;
			}
			else 
			{
				switch (g_status.key_code)
				{
					case KEY_VOLUME_UP:
						if (g_status.volume < VOLUME_MAX) {
							set_volume(get_volume() + 1);
						}
						break;
						
					case KEY_VOLUME_DOWN:
						if (g_status.volume > 0) {
							set_volume(get_volume() - 1);
						}
						break;
						
					case KEY_BRIGHTNESS_UP:
						if (g_status.brightness < VOLUME_MAX) {
							set_brightness(get_brightness() + 1);
						}
						break;
						
					case KEY_BRIGHTNESS_DOWN:
						if (g_status.brightness > 0) {
							set_brightness(get_brightness() - 1);
						}
						break;							
				}	
			}
			break;
        }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void* keyevent_thread(void* arg)
{
	int res;
	struct input_event event;
	void *tmp = arg;
	int fd = open("/dev/input/event9", O_RDWR);

	if (fd < 0)
	{
		ALOGE("[armon] could not open input event9, %s\n", strerror(errno));
		return NULL;
	}

	while (1) 
	{		
		res = read(fd, &event, sizeof(event));
		if (res < (int)sizeof(event)) {
			ALOGE("[armon] key read fail = %d\n", event.code);
			continue;
		}

		if (event.type==1 && (KEY_VOLUME_DOWN <= event.code && event.code <= KEY_BRIGHTNESS_UP)) {
			g_status.key_code = event.code;
			
			// key up			
			if (event.value == 0) {
				if (is_timer_running(tId_Key)) {
					g_status.long_tick = 0;
					timer_stop(tId_Key);
				}

				ALOGD("[armon] key code = %d\n", event.code);
				switch (event.code)
				{
					case KEY_VOLUME_UP:
						if (g_status.volume < VOLUME_MAX) {
							set_volume(get_volume() + 1);
						}
						break;
						
					case KEY_VOLUME_DOWN:
						if (g_status.volume > 0) {
							set_volume(get_volume() - 1);
						}
						break;

					case KEY_BRIGHTNESS_UP:
						if (g_status.brightness < VOLUME_MAX) {
							set_brightness(get_brightness() + 1);
						}
						break;
						
					case KEY_BRIGHTNESS_DOWN:
						if (g_status.brightness > 0) {
							set_brightness(get_brightness() - 1);
						}
						break;						
				}
			} else { // key down
				if (!is_timer_running(tId_Key)) {
					timer_start(tId_Key, TIMEOUT_KEY);
				}
			}
		}
	}
	return NULL;
}

int main(int argc, char *argv[])
{
	pthread_t key_handle;
	char buf[PROPERTY_VALUE_MAX+1];	
	char *arg_v = argv[0];
	int arg_c = argc;

	memset(&g_status, 0, sizeof(g_status)); 

	ALOGD("[armon] deamon service start");

	if (property_get("persist.prazen.brightness", buf, NULL)) {
		dev_wr_status("brightness", PATH_PANEL, ar_atoi(buf));
	}
/*
	if (property_get("persist.prazen.volume", buf, NULL)) {
		dev_wr_status("volume", PATH_AUDIO, ar_atoi(buf));
	}
*/

	pthread_create(&key_handle, NULL, keyevent_thread, NULL);

	timer_init();

	while(1) sleep(10);

	return 0;
}
