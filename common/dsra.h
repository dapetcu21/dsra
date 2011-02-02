#ifndef DSRA_H
#define DSRA_H

#include <portaudio.h>
#include <pthread.h>

struct params
{
	int channels;
	PaSampleFormat 	sampleFormat;
	double 	sampleRate;
	unsigned long framesPerBuffer;
};

#ifdef SERVER
struct list_el
{
	uint8_t * buffer;
	struct list_el * next;
};
#endif

struct udata
{
	volatile int still_running;
	int bpFrame;
	int fd;
	int verbose;
	struct params * prm;
#ifdef SERVER
	struct list_el * first, * last;
	pthread_mutex_t mutex;
	int buflen;
#endif
};

#define HEADER_SIZE 11
#define DSRA_SIG 0x12
#define DSRA_INT8 1
#define DSRA_INT16 2
#define DSRA_INT24 3
#define DSRA_INT32 4
#define DSRA_FLOAT32 5

#endif