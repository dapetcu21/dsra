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

#define HEADER_SIZE 11
#define DSRA_SIG 0x12
#define DSRA_SIG_DATA 0x11
#define DSRA_INT8 1
#define DSRA_INT16 2
#define DSRA_INT24 3
#define DSRA_INT32 4
#define DSRA_FLOAT32 5

#endif