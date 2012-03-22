#define CLIENT

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include "../common/dsra.h"

struct udata
{
	volatile int still_running;
	int bpFrame;
	int fd;
	int verbose;
	struct params * prm;
};

int write_data(int fd, uint8_t * buf, size_t size)
{
	while (1)
	{
		int err = write(fd,buf,size);
		if ((err==-1)&&(errno!=EAGAIN)&&(errno!=EINTR))
		{
			fprintf(stderr,"write(): %s\n",strerror(errno));
			return -1;
		}
		if (err == (int)size)
			return 0;
		if (err==0)
			return -1;
	}
	return 0;
}

void send_header(const struct params * prm, int fd);
static int get_data( const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData )
{
	struct udata * ud = ((struct udata*)userData);
    size_t size = framesPerBuffer*(ud->bpFrame)*(ud->prm->channels); 
	int empty = 1;

	size_t i;
	for (i=0; i<size; i++)
		if (((uint8_t*)inputBuffer)[i]!=0)
		{
			empty = 0;
			break;
		}

    static int eframes = 0;
    if (empty)
    {
        if (eframes<300)
        {
            eframes++;
            empty = 0;
        }
    } else  eframes = 0;
	static unsigned int count = 0;
	int bufferspersecond = (ud->prm->sampleRate)/framesPerBuffer;
	
	if (!empty)
	{
		uint8_t * buffer = (uint8_t*)malloc(size+2);
		buffer[0] = DSRA_SIG_DATA;
		buffer[1] = (uint8_t)count;
		memcpy(buffer+2,inputBuffer,size);
		if (write_data(ud->fd,buffer,size+2)!=0)
		{
			free(buffer);
			ud->still_running = 0;
			return paComplete;
		}
		free(buffer);
		count++;
		if ((count % bufferspersecond) == 0)
			send_header(ud->prm,ud->fd);
	}
    return 0;
}

int init_network(const char * host, const char * port)
{
    struct addrinfo hints, *res;
    int stat;
    int resu;
	int sockfd = -1;
	
    memset(&hints,0,sizeof(hints));
    hints.ai_family=AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if ((stat=getaddrinfo(host,port,&hints,&res)))
    {
        fprintf(stderr,"getaddrinfo failed: %s\n",gai_strerror(stat));
        return -1;
    }
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd==-1)
    {
        fprintf(stderr,"Socket opening failed: %s\n",strerror(errno));
        return -1;
    }
    resu=connect(sockfd,res->ai_addr,res->ai_addrlen);
    if (resu==-1)
    {
        fprintf(stderr,"Connecting failed: %s\n",strerror(errno));
        return -1;
    }
    return sockfd;
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

void cleanup_audio()
{
	Pa_Terminate();
}


void list_devices()
{
	PaDeviceIndex i,n = Pa_GetDeviceCount();
	printf("Available input devices:\n");
	for (i=0; i<n; i++)
	{
		const PaDeviceInfo * info = Pa_GetDeviceInfo(i);
		if (info->maxInputChannels)
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
		nr = Pa_GetDefaultInputDevice();
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
				if ((info->maxInputChannels)&&(strcmp(info->name,dev)==0))
					break;
			}
			if (nr>=n) return -1;
		}
		const PaDeviceInfo * info = Pa_GetDeviceInfo(nr);
		if (info->maxInputChannels==0)
			return -1;
	}
	if (nr>=n) return -1;
	if (nr<0) return -1;
	return nr;
}

PaDeviceIndex dev;

void send_header(const struct params * prm, int fd)
{
	uint8_t header[HEADER_SIZE];
	header[0] = DSRA_SIG;
	header[1] = (uint8_t)(prm->channels);
	switch (prm->sampleFormat) {
		case paInt8:
			header[2] = DSRA_INT8;
			break;
		case paInt16:
			header[2] = DSRA_INT16;
			break;
		case paInt24:
			header[2] = DSRA_INT24;
			break;
		case paInt32:
			header[2] = DSRA_INT32;
			break;
		case paFloat32:
			header[2] = DSRA_FLOAT32;
			break;
		default:
			header[2] = 0;
			break;
	}	
	*(uint32_t*)(header+3) = htonl((uint32_t)(prm->sampleRate));
	*(uint32_t*)(header+7) = htonl((uint32_t)(prm->framesPerBuffer));
	write_data(fd,header,HEADER_SIZE);
}

int play_stream(int fd, struct params prm)
{
	PaStream *stream;
	PaError err;
	
	send_header(&prm, fd);
	
	struct udata ud;
	ud.fd = fd;
	ud.still_running = 1;
	ud.prm = &prm;
	ud.verbose = 0;
	switch (prm.sampleFormat) {
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
			ud.bpFrame = 1;
			break;
	}	
	
    PaStreamParameters ioparam;
	ioparam.device = dev;
	ioparam.channelCount = prm.channels;
	ioparam.sampleFormat = prm.sampleFormat;
	ioparam.suggestedLatency = 0;
	ioparam.hostApiSpecificStreamInfo = NULL;
	
	err = Pa_OpenStream(&stream,
						&ioparam,
						NULL,
						prm.sampleRate,
						prm.framesPerBuffer,
						0,
						get_data,
						&ud );
    
	if ( err != paNoError )
    {
        fprintf(stderr,"Can't open stream: %s\n",Pa_GetErrorText( err ));
		return -1;
    }
    err = Pa_StartStream( stream );
    if( err != paNoError)
    {
        fprintf(stderr,"Can't start stream: %s\n",Pa_GetErrorText( err ));
		return -1;
    }
    while (ud.still_running)
        Pa_Sleep(60);
    err = Pa_StopStream( stream );
    err = Pa_CloseStream( stream );
	return 0;
}

void usage()
{
    printf("dsra: Dead Simple Remote Audio - Client\n");
    printf("Usage: dsra [OPTIONS] hostname \n");
    printf("OPTIONS can be: \n");
    printf("  -p=PORT --port=PORT     connect to port PORT on the remote host. defaults to 4547\n");
    printf("  -c=N --channels=N       transmit N channels. defaults to 2 (stereo)\n");
	printf("  -d=DEPTH --depth=DEPTH  bit depth. 8, 16, 24, 32 or float. defaults to 24\n");
	printf("  -r=RATE --rate=RATE     sample rate in Hz. defaults to 44100\n");
	printf("  -b=BUF --buffer=BUF     the number of frames to send in one packet. defaults to 64\n");
	printf("  -l --list               list all input devices and exit\n");
	printf("  --dev=DEVICE            use input device DEVICE. DEVICE can be a name or a device index (see -l)\n");
	printf("  -? -h --help            display this info\n");
    exit(2);
}

int numeric_value(const char * s)
{
	int nr=0;
	const char * c;
	for(c = s; *c!='\0'; c++)
	{
		if ((*c)<='9' && (*c)>='0')
			nr=nr*10+((*c)-'0');
		else {
			fprintf(stderr,"Invalid numeric value:%s\n",s);
			exit(2);
		}
	}
	return nr;
}

void process_args(int argc, const char * argv[],struct params * prm,const char ** _host, const char ** _port, const char ** device,int * list)
{
	const char * host = NULL;
	const char * port = "4575";
	const char * channels = "2";
	const char * format = "24";
	const char * rate = "44100";
	const char * frames = "64";
	*device = NULL;
	*list = 0;
	int i;
	for (i=1; i<argc; i++)
	{
		if (argv[i]==NULL)
			continue;
		if( strncmp(argv[i],"-p=",3)==0 )
		{
			port = argv[i]+3;
			continue;
		}	
		if( strncmp(argv[i],"--port=",7)==0 )
		{
			port = argv[i]+7;
			continue;
		}	
		if( strncmp(argv[i],"-c=",3)==0 )
		{
			channels = argv[i]+3;
			continue;
		}
		
		if( strncmp(argv[i],"--channels=",11)==0 )
		{
			channels = argv[i]+11;
			continue;
		}
		
		if( strncmp(argv[i],"-d=",3)==0 )
		{
			format = argv[i]+3;
			continue;
		}
		
		if( strncmp(argv[i],"--depth=",8)==0 )
		{
			format = argv[i]+8;
			continue;
		}
		
		if( strncmp(argv[i],"-r=",3)==0 )
		{
			rate = argv[i]+3;
			continue;
		}
		
		if( strncmp(argv[i],"--rate=",7)==0 )
		{
			rate = argv[i]+7;
			continue;
		}
		
		if( strncmp(argv[i],"-b=",3)==0 )
		{
			frames = argv[i]+3;
			continue;
		}
		
		if( strncmp(argv[i],"--buffer=",9)==0 )
		{
			frames = argv[i]+7;
			continue;
		}
		if (strncmp(argv[i],"--dev=",6)==0)
		{
			*device = argv[i]+6;
			continue;
		}
		if( strcmp(argv[i],"--help")==0 )
			usage();
		if( strcmp(argv[i],"-?")==0 )
			usage();
		if( strcmp(argv[i],"-h")==0 )
			usage();
		if (!host && argv[i][0]!='-')
 		{
			host = argv[i];
			continue;
		}
		
		if (strcmp(argv[i],"-l")==0)
		{
			*list = 1;
			continue;
		}
		
		if (strcmp(argv[i],"--list")==0)
		{
			*list = 1;
			continue;
		}
		fprintf(stderr,"Unknown option: \"%s\"\n",argv[i]);
		usage();
	}
	if (!host && !(*list))
	{
		fprintf(stderr,"You must specify a hostname on the command line\n");
		usage();
	}
	
	prm->channels = numeric_value(channels);
	if (strcmp(format,"8")==0)
		prm->sampleFormat = paInt8;
	else
	if (strcmp(format,"16")==0)
		prm->sampleFormat = paInt16;
	else
	if (strcmp(format,"24")==0)
		prm->sampleFormat = paInt24;
	else
	if (strcmp(format,"32")==0)
		prm->sampleFormat = paInt32;
	else
	if (strcmp(format,"float")==0)
		prm->sampleFormat = paFloat32;
	else
	{
		fprintf(stderr,"Invalid depth: %s\n",format);
		usage();
	}
	prm->sampleRate = numeric_value(rate);
	prm->framesPerBuffer = numeric_value(frames);
	
	fprintf(stderr,"Trying to connect to %s on port %s\n",host,port);
	fprintf(stderr,"Number of channels: %d\n",prm->channels);
	fprintf(stderr,"Bit depth: %s\n",format);
	fprintf(stderr,"Sample rate: %.0fHz\n",prm->sampleRate);
	fprintf(stderr,"Buffer size: %lu frames\n",prm->framesPerBuffer);
	*_host = host;
	*_port = port;
}

int main(int argc, const char * argv[])
{
	int ret = 0;
	
	const char * host;
	const char * port;
	const char * device;
	struct params prm;
	int list;
	process_args(argc,argv,&prm,&host,&port,&device,&list);
	if (list)
	{
		if (init_audio()!=0)
			return 1;
		list_devices();
		return 0;
	}
	int sockfd;
	
    if ((sockfd=init_network(host,port))<0)
    {
        fprintf(stderr,"Can't connect to \"%s\" on port %s\n",host,port);
        exit(1);
    }
	if (init_audio()!=0)
		return 1;
	if ((dev=device_for_string(device))==-1)
	{
		fprintf(stderr,"Unknown device \"%s\"\n",device?device:"");
		cleanup_audio();
		close(sockfd);
		return 1;
	} else {
		fprintf(stderr,"Using device \"%s\"\n",Pa_GetDeviceInfo(dev)->name);
	}
	if (play_stream(sockfd,prm)!=0)
		ret = 1;
	cleanup_audio();
    close(sockfd); 
	return ret;
}
