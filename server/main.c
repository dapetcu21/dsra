#define SERVER

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include "../common/dsra.h"

#define MAX_BUFFER_SIZE 2048

struct params prm;
int verbose = 0;
int keep_going = 0;
int keep_going_global = 1;
PaDeviceIndex dev;
pthread_mutex_t keep_going_mutex,queue_mutex;
uint8_t queue_pointer,queue_last_added;
int reset_queue_pointer;
uint8_t * queue_buffers[256] = {};
size_t queue_sizes[256] = {};
struct timeval last_data_packet_time;

struct udata
{
	int bpFrame;
	int verbose;
	struct params * prm;
};

static int get_data( const void *inputBuffer, void *outputBuffer,
						  unsigned long framesPerBuffer,
						  const PaStreamCallbackTimeInfo* timeInfo,
						  PaStreamCallbackFlags statusFlags,
						  void *userData )
{
	struct udata * ud = ((struct udata*)userData);
    size_t size = framesPerBuffer*(ud->bpFrame)*(ud->prm->channels); 

	uint8_t * buffer = NULL;
	size_t sz = 0;
	pthread_mutex_lock(&queue_mutex);
	if (reset_queue_pointer)
	{
		queue_pointer = queue_last_added;
		reset_queue_pointer = 0;
	}
	else
		queue_pointer++;
	
	uint8_t last = queue_pointer;
	while(!queue_buffers[queue_pointer])
	{
		queue_pointer++;
		if (last==queue_pointer)
			break;
	}
	
	buffer = queue_buffers[queue_pointer];
	sz = queue_sizes[queue_pointer];
	if (!buffer)
		reset_queue_pointer = 1;
	else 
		queue_buffers[queue_pointer] = NULL;
	pthread_mutex_unlock(&queue_mutex);
	
	if (size<sz)
		sz = size;
	if (buffer)
		memcpy(outputBuffer,buffer+2,sz);
	else 
	{
		memset(outputBuffer,0,size);
		return 0;
	}

	if (ud->verbose)
	{
		float nr = 0;
		if (ud->prm->sampleFormat==paInt8)
			nr = ((float)(*((int8_t*)outputBuffer)))/0x7F;
		if (ud->prm->sampleFormat==paInt16)
			nr = ((float)(*((int16_t*)outputBuffer)))/0x7FFF;
		if (ud->prm->sampleFormat==paInt24)
		{
			int32_t nrm = ((*((int8_t*)(outputBuffer)))<<16) |
						((*((int8_t*)(outputBuffer+1)))<<8) |
						((*((int8_t*)(outputBuffer+2))));
			nr = ((float)nrm)/0x7FFFFF;
		}
		if (ud->prm->sampleFormat==paInt32)
			nr = ((float)(*((int32_t*)(outputBuffer))))/0x7FFFFFFF;
		if (ud->prm->sampleFormat==paFloat32)
			nr = *((float*)outputBuffer);
		int j;
		for (j=0; j<(nr+1.0f)*75; j++)
			fprintf(stdout, " ");
		fprintf(stdout, "|\n");
	}
    return 0;
}

int queue_data(uint8_t * buffer, size_t size)
{
	if (buffer[0] != DSRA_SIG_DATA) return 1;
	gettimeofday(&last_data_packet_time,NULL);
	uint8_t timestamp = buffer[1];
	pthread_mutex_lock(&queue_mutex);
	if (reset_queue_pointer || timestamp>=queue_pointer)
	{
		if (queue_buffers[timestamp])
			free(queue_buffers[timestamp]);
		queue_buffers[timestamp] = buffer;
		queue_sizes[timestamp] = size;
		queue_last_added = timestamp;
		pthread_mutex_unlock(&queue_mutex);
		return 0;
	}
	pthread_mutex_unlock(&queue_mutex);
	return 1;
}


int init_audio()
{
    PaError err = Pa_Initialize();
    if( err != paNoError )
    {
        fprintf(stderr,"Can't initialise PortAudio: %s\n",Pa_GetErrorText( err ));
        return 1;
    }
    return 0;
}

int cleanup_audio()
{
    Pa_Terminate();
    return 0;
}

void list_devices()
{
	PaDeviceIndex i,n = Pa_GetDeviceCount();
	printf("Available output devices:\n");
	for (i=0; i<n; i++)
	{
		const PaDeviceInfo * info = Pa_GetDeviceInfo(i);
		if (info->maxOutputChannels)
		{
			printf("%d: %s\n",(int)i,info->name);
		}
	}
}

PaDeviceIndex device_for_string(const char * dev)
{
	PaDeviceIndex nr,n = Pa_GetDeviceCount();
	if (dev==NULL)
	{
		nr = Pa_GetDefaultOutputDevice();
	} else {
		nr=0;
		const char * c;
		for(c = dev; *c!='\0'; c++)
		{
			if ((*c)<='9' && (*c)>='0')
				nr=nr*10+((*c)-'0');
			else {
				nr=-1;
				break;
			}
		}
		if ((nr<0)||(nr>=n))
		{
			for (nr=0; nr<n; nr++)
			{
				const PaDeviceInfo * info = Pa_GetDeviceInfo(nr);
				if ((info->maxOutputChannels)&&(strcmp(info->name,dev)==0))
					break;
			}
			if (nr>=n) return -1;
		}
		const PaDeviceInfo * info = Pa_GetDeviceInfo(nr);
		if (info->maxOutputChannels==0)
			return -1;
	}
	if (nr>=n) return -1;
	if (nr<0) return -1;
	return nr;
}

			
void audio_thread(struct params * prm)
{	
	PaStream *stream;
	PaError err;
	
	PaStreamParameters ioparam;
	ioparam.device = dev;
	ioparam.channelCount = prm->channels;
	ioparam.sampleFormat = prm->sampleFormat;
	ioparam.suggestedLatency = Pa_GetDeviceInfo(dev)->defaultLowOutputLatency;
	ioparam.hostApiSpecificStreamInfo = NULL;

	pthread_mutex_lock(&queue_mutex);
	reset_queue_pointer = 1;
	pthread_mutex_unlock(&queue_mutex);	
	
	struct udata ud;
	ud.prm = prm;
	ud.verbose = verbose;
	switch (prm->sampleFormat) {
		case paInt8:
			ud.bpFrame = 1;
			break;
		case paInt16:
			ud.bpFrame = 2;
			break;
		case paInt24:
			ud.bpFrame = 3;
			break;
		case paInt32:
			ud.bpFrame = 4;
			break;
		case paFloat32:
			ud.bpFrame = 4;
			break;
		default:
			break;
	}
	
	err = Pa_OpenStream(&stream,
						NULL,
						&ioparam,
						prm->sampleRate,
						prm->framesPerBuffer,
						0,
						get_data,
						&ud );
    if (err!=paNoError)
    {
        fprintf(stderr,"Can't open stream: %s\n",Pa_GetErrorText(err));
        return;
    }
    err = Pa_StartStream(stream);
    if(err != paNoError)
    {
        fprintf(stderr,"Can't start stream: %s\n",Pa_GetErrorText(err));
        Pa_CloseStream(stream);
        return;
    }

	const char * format = "unknown";
	switch (prm->sampleFormat) {
		case paInt8:
			format = "8";
			break;
		case paInt16:
			format = "16";
			break;
		case paInt24:
			format = "24";
			break;
		case paInt32:
			format = "32";
			break;
		case paFloat32:
			format = "float32";
			break;
	}
	fprintf(stderr,"Opened audio stream\n");
	fprintf(stderr,"Number of channels: %d\n",prm->channels);
	fprintf(stderr,"Bit depth: %s\n",format);
	fprintf(stderr,"Sample rate: %.0fHz\n",prm->sampleRate);
	fprintf(stderr,"Buffer size: %lu frames\n",prm->framesPerBuffer);
	
    while (1)
	{
		pthread_mutex_lock(&keep_going_mutex);
		int sr = keep_going;
		pthread_mutex_unlock(&keep_going_mutex);
		if (!sr)
			break;
        Pa_Sleep(60);
	}
    Pa_CloseStream(stream);
	fprintf(stderr,"Closed audio stream due to inactivity\n");
}

int params_equal(const struct params * prm1, const struct params * prm2)
{
	if (!prm1 || !prm2) return 0;
	return (memcmp(prm1,prm2,sizeof(struct params)) == 0);
}

int audio_started = 0;
pthread_t audio;
struct params * audio_params = NULL;
void stop_audio()
{
	if (audio_started)
	{
		audio_started = 0;
		pthread_mutex_lock(&keep_going_mutex);
		keep_going = 0;
		pthread_mutex_unlock(&keep_going_mutex);
		pthread_join(audio,NULL);
		free(audio_params);
		audio_params = NULL;
	}
}

void start_audio(const struct params * prm)
{
	gettimeofday(&last_data_packet_time,NULL);
	if (audio_started && audio_params && prm && (memcmp(audio_params,prm,sizeof(struct params))==0))
		return;
	if (audio_started)
		stop_audio();
	audio_started = 1;
	
 	audio_params = malloc(sizeof(struct params));
	memcpy(audio_params,prm,sizeof(struct params));
	
	pthread_mutex_lock(&keep_going_mutex);
	keep_going = 1;
	pthread_mutex_unlock(&keep_going_mutex);
	pthread_create(&audio,NULL,(void * (*)(void*))audio_thread,(void*)audio_params);
}

int read_header(const uint8_t * header, size_t sz, struct params * prm)
{
	if (sz<HEADER_SIZE) 
		return 1;
	if (header[0]!=DSRA_SIG)
		return 1;
	memset(prm,0,sizeof(struct params));
	prm->channels = header[1];
	switch (header[2]) {
		case DSRA_INT8:
			prm->sampleFormat = paInt8;
			break;
		case DSRA_INT16:
			prm->sampleFormat = paInt16;
			break;
		case DSRA_INT24:
			prm->sampleFormat = paInt24;
			break;
		case DSRA_INT32:
			prm->sampleFormat = paInt32;
			break;
		case DSRA_FLOAT32:
			prm->sampleFormat = paFloat32;
			break;
		default:
			return 1;
	}
	prm->sampleRate = ntohl(*(uint32_t*)(header+3));
	prm->framesPerBuffer = ntohl(*(uint32_t*)(header+7));
	
	return 0;
}

int parse_data(uint8_t * buffer, size_t size)
{
	static struct params prm;
	static int prm_valid = 0;
	if (!size) return 1;
	if (buffer[0] == DSRA_SIG)
	{
		if ((prm_valid = (read_header(buffer,size,&prm) == 0)))
			start_audio(&prm);
		return 1;
	}
	if (buffer[0] == DSRA_SIG_DATA)
	{
		if (!audio_started)
			return 1;
		return queue_data(buffer,size);
	}
	return 1;
}

int timedout(const struct timeval * ts, long long timeout)
{
	struct timeval ct;
	gettimeofday(&ct,NULL);
	long long interval = ((long long)(ct.tv_sec-ts->tv_sec))*1000000 + (((long long)ct.tv_usec)-((long long)(ts->tv_usec)));
	return (interval > timeout);
}

int start_listening(const char * port)
{
    struct addrinfo hints, *res,*p;
    int sockfd;
	int socks[100];
	int nsocks = 0;
	int i;
	
    // first, load up address structs with getaddrinfo():
	
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;  // use IPv4 or IPv6, whichever
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;     // fill in my IP for me
	
	int r;
    if ((r = getaddrinfo(NULL, port, &hints, &res))!=0)
	{
		fprintf(stderr,"getaddrinfo: %s",gai_strerror(r));
		return -1;
	}
	
	for (p=res; p; p=p->ai_next)
	{
	
		// make a socket, bind it, and listen on it:
	
		if ((sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol))<0)
			continue;
		
		if (fcntl(sockfd, F_SETFL, O_NONBLOCK)<0)
		{
			close(sockfd);
			continue;
		}
		
		if (bind(sockfd, res->ai_addr, res->ai_addrlen)<0)
		{
			close(sockfd);
			continue;
		}
		
		socks[nsocks++]=sockfd;
		if (nsocks==100)
			break;
	}
	
	if (nsocks==0)
	{
		fprintf(stderr,"no valid interfaces to listen on found\n");
		return -1;
	}
	
	pthread_mutex_init(&keep_going_mutex,NULL);
	pthread_mutex_init(&queue_mutex,NULL);

	int asocks = nsocks;
	while (asocks && keep_going_global)
	{
		fd_set fds;
		int max=0;
		FD_ZERO(&fds);
		for (i=0; i<nsocks; i++)
		{
			if (socks[i]<0) continue;
			FD_SET(socks[i],&fds);
			if (socks[i]+1>max)
				max=socks[i]+1;
		}
		struct timeval timeout;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		if ((select(max,&fds,NULL,NULL,&timeout)<0)&&(errno!=EINTR))
		{
			fprintf(stderr,"select(): %s\n",strerror(errno));
			for (i=0; i<nsocks; i++)
				close(socks[i]);
			return -1;
		}
		for (i=0; i<nsocks; i++)
		{
			if (socks[i]<0) continue;
			if (FD_ISSET(socks[i],&fds))
			{
				struct sockaddr_storage their_addr;
				socklen_t addr_size = sizeof their_addr;
				
				uint8_t * buffer = malloc(MAX_BUFFER_SIZE);
				ssize_t ret = recvfrom(socks[i],buffer,MAX_BUFFER_SIZE,0,(struct sockaddr *)&their_addr,&addr_size);
				if (ret == -1)
				{
					fprintf(stderr,"recvfrom(): %s\n",strerror(errno));
					close(socks[i]);
					socks[i] = -1;
					asocks--;
				} else {
					if (parse_data(buffer,(size_t)ret))
						free(buffer);
				}
			}
		}
		if (timedout(&last_data_packet_time,1000000))
			stop_audio();
	}
	for (i=0; i<nsocks; i++)
		if (socks[i]>=0)
			close(socks[i]);
	stop_audio();
    return 0;
}

void usage()
{
	printf("dsra: Dead Simple Remote Audio - Server\n");
    printf("Usage: dsra [OPTIONS]\n");
    printf("OPTIONS can be: \n");
    printf("  -p=PORT --port=PORT  listen to port PORT. defaults to 4547\n");
	printf("  -l --list            list all output devices and exit\n");
	printf("  --dev=DEVICE         use output device DEVICE. DEVICE can be a name or a device index (see -l)\n");
	printf("  -v                   waveform visualization (for debugging)\n");
	printf("  -? -h --help         display this info\n");
	exit(2);
}

int main(int argc, const char * argv[])
{
	int list = 0;
	int i;
	const char * port = "4575";
	const char * device = NULL;
	for (i=1; i<=argc; i++)
	{
		if (!argv[i]) continue;
		if ((strcmp(argv[i],"-h")==0)||(strcmp(argv[i],"--help")==0)||(strcmp(argv[i],"-?")==0))
			usage();
		if (strncmp(argv[i],"-p=",3)==0)
		{
			port = argv[i]+3;
			continue;
		}
		if (strncmp(argv[i],"--port=",7)==0)
		{
			port = argv[i]+7;
			continue;
		}
		if (strcmp(argv[i],"-v")==0)
		{
			verbose = 1;
			continue;
		}
		if (strcmp(argv[i],"-l")==0)
		{
			list = 1;
			continue;
		}
		
		if (strcmp(argv[i],"--list")==0)
		{
			list = 1;
			continue;
		}
		if (strncmp(argv[i],"--dev=",6)==0)
		{
			device = argv[i]+6;
			continue;
		}
		printf("Unknown option: %s\n",argv[i]);
		usage();
	}
    if (init_audio()!=0)
        return 1;
	if ((dev=device_for_string(device))==-1)
	{
		fprintf(stderr,"Unknown device \"%s\"\n",device?device:"");
		cleanup_audio();
		return 1;
	} else {
		fprintf(stderr,"Using device \"%s\"\n",Pa_GetDeviceInfo(dev)->name);
	}
	if (list)
	{
		list_devices();
		cleanup_audio();
		return 0;
	}
	if (start_listening(port)!=0)
	{
		cleanup_audio();
		return 1;
	}
    cleanup_audio();
	return 0;
}      
