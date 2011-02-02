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
#include "../common/dsra.h"

#define MAXBUFFER 60
#define DESIREDBUFFER 30

static int get_data( const void *inputBuffer, void *outputBuffer,
						  unsigned long framesPerBuffer,
						  const PaStreamCallbackTimeInfo* timeInfo,
						  PaStreamCallbackFlags statusFlags,
						  void *userData )
{
	struct udata * ud = ((struct udata*)userData);
    size_t size = framesPerBuffer*(ud->bpFrame)*(ud->prm->channels); 
	pthread_mutex_lock(&(ud->mutex));
	if (ud->first)
	{
		struct list_el * nw = ud->first;
		ud->first = nw->next;
		if (ud->first == NULL)
			ud->last = NULL;
		ud->buflen--;
		pthread_mutex_unlock(&(ud->mutex));
		memcpy(outputBuffer,nw->buffer,size);
		free(nw->buffer);
		free(nw);
	} else 
	{
		pthread_mutex_unlock(&(ud->mutex));
		memset(outputBuffer,0,size);
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

int read_data(int fd, void * buf, size_t size)
{
	while (1)
	{
		int err = read(fd,buf,size);
		if ((err==-1)&&(errno!=EAGAIN)&&(errno!=EINTR))
			return -1;
		if (err == (int)size)
			return size;
		if (err==0)
			return -1;
	}
}

void network_thread(struct udata * ud)
{
    size_t size = (ud->prm->framesPerBuffer)*(ud->bpFrame)*(ud->prm->channels); 
	while (ud->still_running)
	{
		struct list_el * nw = (struct list_el *)malloc(sizeof(struct list_el));
		nw->buffer = (uint8_t*)malloc(size);
		if (read_data(ud->fd,nw->buffer,size)<0)
		{
			ud->still_running = 0;
			continue;
		}
		pthread_mutex_lock(&(ud->mutex));
		nw->next = NULL;
		if (ud->last)
		{
			ud->last->next = nw;
			ud->last = nw;
		} else {
			ud->first = ud->last = nw;
		}
		ud->buflen++;
		if (ud->buflen > MAXBUFFER)
		{
			while (ud->buflen > DESIREDBUFFER)
			{
				ud->buflen--;
				struct list_el * tmp = ud->first;
				ud->first = ud->first->next;
				free(tmp->buffer);
				free(tmp);
			}
		}
		pthread_mutex_unlock(&(ud->mutex));
	}
	pthread_mutex_lock(&(ud->mutex));
	while (ud->first)
	{
		struct list_el * nw = ud->first;
		ud->first = ud->first->next;
		free(nw->buffer);
		free(nw);
	}
	ud->last = NULL;
	ud->buflen = 0;
	pthread_mutex_unlock(&(ud->mutex));
}

PaDeviceIndex dev;
			
int stream(int fd, struct params prm, int verbose)
{
    PaStream *stream;
	PaError err;

	struct udata ud;
	ud.fd = fd;
	ud.still_running = 1;
	ud.prm = &prm;
	ud.verbose = verbose;
	ud.first = NULL;
	ud.last = NULL;
	ud.buflen = 0;
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
			break;
	}
	
	pthread_t network;
	pthread_mutex_init(&(ud.mutex),NULL);
	pthread_create( &network, NULL, (void * (*)(void *))network_thread, (void*) &ud);
	
	PaStreamParameters ioparam;
	ioparam.device = dev;
	ioparam.channelCount = prm.channels;
	ioparam.sampleFormat = prm.sampleFormat;
	ioparam.suggestedLatency = Pa_GetDeviceInfo(dev)->defaultLowOutputLatency;
	ioparam.hostApiSpecificStreamInfo = NULL;
	
	err = Pa_OpenStream(&stream,
						NULL,
						&ioparam,
						prm.sampleRate,
						prm.framesPerBuffer,
						0,
						get_data,
						&ud );
    if ( err!=paNoError)
    {
        fprintf(stderr,"Can't open stream: %s\n",Pa_GetErrorText( err ));
		ud.still_running = 0;
		pthread_join( network, NULL);
		pthread_mutex_destroy(&(ud.mutex));
        return 1;
    }
    err = Pa_StartStream( stream );
    if( err != paNoError)
    {
        fprintf(stderr,"Can't start stream: %s\n",Pa_GetErrorText( err ));
        Pa_CloseStream( stream );
		ud.still_running = 0;
		pthread_join( network, NULL);
		pthread_mutex_destroy(&(ud.mutex));
        return 1;
    }
    while (ud.still_running)
        Pa_Sleep(60);
    err = Pa_CloseStream( stream );
	pthread_join( network, NULL);
	pthread_mutex_destroy(&(ud.mutex));
    return 0;
}

int nrthreads = 0;
int verbose = 0;

int read_header(int fd, struct params * prm)
{
	uint8_t header[HEADER_SIZE];
	read_data(fd,header,1);
	if (header[0]!=DSRA_SIG)
		return 1;
	read_data(fd,header+1,HEADER_SIZE-1);
	prm->channels = header[1];
	const char * format;
	switch (header[2]) {
		case DSRA_INT8:
			prm->sampleFormat = paInt8;
			format = "8";
			break;
		case DSRA_INT16:
			prm->sampleFormat = paInt16;
			format = "16";
			break;
		case DSRA_INT24:
			prm->sampleFormat = paInt24;
			format = "24";
			break;
		case DSRA_INT32:
			prm->sampleFormat = paInt32;
			format = "32";
			break;
		case DSRA_FLOAT32:
			prm->sampleFormat = paFloat32;
			format = "float";
			break;
		default:
			fprintf(stderr,"Invalid bit depth\n");
			return 1;
	}
	prm->sampleRate = ntohl(*(uint32_t*)(header+3));
	prm->framesPerBuffer = ntohl(*(uint32_t*)(header+7));
	
	
	fprintf(stderr,"Number of channels: %d\n",prm->channels);
	fprintf(stderr,"Bit depth: %s\n",format);
	fprintf(stderr,"Sample rate: %.0fHz\n",prm->sampleRate);
	fprintf(stderr,"Buffer size: %lu frames\n",prm->framesPerBuffer);
	
	return 0;
}

void audio_thread(int * i)
{
	int fd = *i;
	free(i);
    fprintf(stdout,"New client\n");
	
	struct params prm;
	if (read_header(fd,&prm)!=0)
	{
		fprintf(stderr,"Invalid header format\n");
	} else 
		stream(fd,prm,verbose);
    fprintf(stdout,"Client disconnected\n");
	nrthreads--;
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
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;     // fill in my IP for me
	
    getaddrinfo(NULL, port, &hints, &res);
	
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
		
		if (listen(sockfd, 1)<0)
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
	
	while (1)
	{
		fd_set fds;
		int max=0;
		for (i=0; i<nsocks; i++)
		{
			FD_ZERO(&fds);
			FD_SET(socks[i],&fds);
			if (socks[i]+1>max)
				max=socks[i]+1;
		}
		if ((select(max,&fds,NULL,NULL,NULL)<0)&&(errno!=EINTR))
		{
			fprintf(stderr,"select() failed: %s\n",gai_strerror(errno));
			for (i=0; i<nsocks; i++)
				close(socks[i]);
			return -1;
		}
		for (i=0; i<nsocks; i++)
		{
			if (FD_ISSET(socks[i],&fds))
			{
				struct sockaddr_storage their_addr;
				socklen_t addr_size = sizeof their_addr;
				if (nrthreads) 
				{
					fprintf(stderr, "Can only accept one connection at a time\n");
					close(accept(socks[i], (struct sockaddr *)&their_addr, &addr_size));
					continue;
				}
				nrthreads++;
				int * fdp = (int*)malloc(sizeof(int));
				*fdp = accept(socks[i], (struct sockaddr *)&their_addr, &addr_size);
				if (*fdp<0)
				{
					fprintf(stderr,"accept() failed : %s\n",gai_strerror(errno));
				} else {
					pthread_t thread;
					pthread_create( &thread, NULL, (void * (*)(void *))audio_thread, (void*) fdp);
				}
			}
		}
	}
	for (i=0; i<nsocks; i++)
		close(socks[i]);
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
