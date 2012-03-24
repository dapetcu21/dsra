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

inline void raise_thread_priority()
{
	int ret;
	struct sched_param param;
	param.sched_priority = sched_get_priority_max(SCHED_OTHER);
	ret = pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
	if (ret!=0)
		fprintf(stderr,"Can't raise thread priority: %s\n",strerror(errno));
}

#define HEADER_SIZE 11
#define DSRA_SIG 0x12
#define DSRA_SIG_DATA 0x11
#define DSRA_INT8 1
#define DSRA_INT16 2
#define DSRA_INT24 3
#define DSRA_INT32 4
#define DSRA_FLOAT32 5

#endif