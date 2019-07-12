/*
 * main.c
 *
 *  Created on: Jul 10, 2019
 *      Author: Intern_2
 */

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#include <xtensa/tie/xt_hifi2.h>

#define INPUT_FILE_NAME "TestSound4.wav"
#define OUTPUT_FILE_NAME "Output.wav"
#define FILE_HEADER_SIZE 44
#define BYTES_PER_SAMPLE 2
#define DATA_BUFF_SIZE 1000
#define SAMPLE_RATE 48000
#define CHANNELS 2

#define FC 200
#define Q_VALUE 0.707		//must be between 0 and 0.707

#define PI 3.14159265358979323846


typedef struct {
	int16_t x[2];
	int16_t y[2];
	uint32_t remainder;

	double dx[2];
	double dy[2];
} BiquadBuff;

typedef struct {
	int32_t b[3];
	int32_t a[2];
	double db[3];
	double da[2];
} BiquadCoeffs;

typedef struct {
	ae_f64 h;
	ae_f64 l;
}F64x2;


static inline ae_f32x2 int16ToF32x2(int16_t x, int16_t y);
static inline ae_f32x2 int32ToF32x2(int32_t x, int32_t y);
static inline ae_f64 uint32ToF64(uint32_t x);
static inline F64x2 putF64ToF64x2(ae_f64 h, ae_f64 l);
static inline ae_f64 LeftShiftA(ae_f64 x, uint8_t shift);
static inline ae_f64 RightShiftA(ae_f64 x, uint8_t shift);
static inline F64x2 Mul(ae_f32x2 x, ae_f32x2 y);
static inline F64x2 Mac(F64x2 acc, ae_f32x2 x, ae_f32x2 y);
static inline F64x2 MSub(F64x2 acc, ae_f32x2 x, ae_f32x2 y);

int32_t doubleToFixed31(double x);

FILE * openFile(char *fileName, _Bool mode);		//if 0 - read, if 1 - write
void readHeader(uint8_t *headerBuff, FILE *inputFilePtr);
void writeHeader(uint8_t *headerBuff, FILE *outputFilePtr);

void initializeBiquadBuff(BiquadBuff *buff);
void calculateBiquadCoeffs(BiquadCoeffs *coeffs, double Fc, double Q);

int16_t biquadDoubleFilter(int16_t sample, BiquadBuff *buff, BiquadCoeffs *coeffs);
int16_t biquadFilter(int16_t sample, BiquadBuff *buff, BiquadCoeffs *coeffs);
void filterSignal(size_t size, BiquadBuff *buff, BiquadCoeffs *coeffs);
void filterSignalIntr(size_t size, BiquadBuff *buff, BiquadCoeffs *coeffs);
void run(FILE *inputFilePtr, FILE *outputFilePtr, BiquadBuff *buff, BiquadCoeffs *coeffs);

int16_t dataBuff[DATA_BUFF_SIZE * CHANNELS];


int main()
{
	FILE *inputFilePtr = openFile(INPUT_FILE_NAME, 0);
	FILE *outputFilePtr = openFile(OUTPUT_FILE_NAME, 1);
	uint8_t headerBuff[FILE_HEADER_SIZE];
	BiquadBuff buff[2];
	BiquadCoeffs coeffs;

	initializeBiquadBuff(&buff[0]);
	initializeBiquadBuff(&buff[1]);
	calculateBiquadCoeffs(&coeffs, FC, Q_VALUE);

	readHeader(headerBuff, inputFilePtr);
	writeHeader(headerBuff, outputFilePtr);
	run(inputFilePtr, outputFilePtr, buff, &coeffs);
	fclose(inputFilePtr);
	fclose(outputFilePtr);

	return 0;
}

static inline ae_f32x2 int16ToF32x2(int16_t x, int16_t y)
{
	return AE_MOVF32X2_FROMINT32X2(AE_MOVDA32X2((int32_t)x << 16, (int32_t)y << 16));
}

static inline ae_f32x2 int32ToF32x2(int32_t x, int32_t y)
{
	return AE_MOVF32X2_FROMINT32X2(AE_MOVDA32X2(x, y));
}

static inline ae_f64 uint32ToF64(uint32_t x)
{
	return AE_MOVF64_FROMINT32X2(AE_MOVDA32X2(0, x));
}

static inline F64x2 putF64ToF64x2(ae_f64 h, ae_f64 l)
{
	F64x2 res;
	res.h = h;
	res.l = l;

	return res;
}

static inline ae_f64 LeftShiftA(ae_f64 x, uint8_t shift)
{
	return AE_SLAA64S(x, shift);
}

static inline ae_f64 RightShiftA(ae_f64 x, uint8_t shift)
{
	return AE_SRAA64(x, shift);
}

static inline F64x2 Mul(ae_f32x2 x, ae_f32x2 y)
{
	F64x2 res;
	res.h = AE_MULF32S_HH(x, y);
	res.l = AE_MULF32S_LL(x, y);

	return res;
}

static inline F64x2 Mac(F64x2 acc, ae_f32x2 x, ae_f32x2 y)
{
	F64x2 prod = Mul(x, y);
	acc.h = AE_ADD64S(acc.h, prod.h);
	acc.l = AE_ADD64S(acc.l, prod.l);

	return acc;
}

static inline F64x2 MSub(F64x2 acc, ae_f32x2 x, ae_f32x2 y)
{
	F64x2 prod = Mul(x, y);
	acc.h = AE_SUB64S(acc.h, prod.h);
	acc.l = AE_SUB64S(acc.l, prod.l);

	return acc;
}

int32_t doubleToFixed31(double x)
{
	if (x >= 1)
	{
		return INT32_MAX;
	}
	else if (x < -1)
	{
		return INT32_MIN;
	}

	return (int32_t)(x * (double)(1LL << 31));
}

FILE * openFile(char *fileName, _Bool mode)		//if 0 - read, if 1 - write
{
	FILE *filePtr;

	if (mode == 0)
	{
		if ((filePtr = fopen(fileName, "rb")) == NULL)
		{
			printf("Error opening input file\n");
			system("pause");
			exit(0);
		}
	}
	else
	{
		if ((filePtr = fopen(fileName, "wb")) == NULL)
		{
			printf("Error opening output file\n");
			system("pause");
			exit(0);
		}
	}

	return filePtr;
}

void readHeader(uint8_t *headerBuff, FILE *inputFilePtr)
{
	if (fread(headerBuff, FILE_HEADER_SIZE, 1, inputFilePtr) != 1)
	{
		printf("Error reading input file (header)\n");
		system("pause");
		exit(0);
	}
}

void writeHeader(uint8_t *headerBuff, FILE *outputFilePtr)
{
	if (fwrite(headerBuff, FILE_HEADER_SIZE, 1, outputFilePtr) != 1)
	{
		printf("Error writing output file (header)\n");
		system("pause");
		exit(0);
	}
}

void initializeBiquadBuff(BiquadBuff *buff)
{
	buff->x[0] = 0;
	buff->x[1] = 0;
	buff->y[0] = 0;
	buff->y[1] = 0;
	buff->remainder = 0;

	buff->dx[0] = 0;
	buff->dx[1] = 0;
	buff->dy[0] = 0;
	buff->dy[1] = 0;
}

void calculateBiquadCoeffs(BiquadCoeffs *coeffs, double Fc, double Q)
{
	double K = tan(PI * Fc / SAMPLE_RATE);
	double norm = 1 / (1 + K / Q + K * K);

	coeffs->db[0] = K * K * norm;
	coeffs->db[1] = 2 * coeffs->db[0];
	coeffs->db[2] = coeffs->db[0];
	coeffs->da[0] = 2 * (K * K - 1) * norm;
	coeffs->da[1] = (1 - K / Q + K * K) * norm;

	coeffs->b[0] = doubleToFixed31(coeffs->db[0] / 2);
	coeffs->b[1] = doubleToFixed31(coeffs->db[1] / 2);
	coeffs->b[2] = coeffs->b[0];
	coeffs->a[0] = doubleToFixed31(coeffs->da[0] / 2);
	coeffs->a[1] = doubleToFixed31(coeffs->da[1] / 2);
}

int16_t biquadDoubleFilter(int16_t sample, BiquadBuff *buff, BiquadCoeffs *coeffs)
{
	double acc = coeffs->db[0] * sample + coeffs->db[1] * buff->dx[0] + coeffs->db[2] * buff->dx[1] -
		coeffs->da[0] * buff->dy[0] - coeffs->da[1] * buff->dy[1];

	buff->dx[1] = buff->dx[0];
	buff->dx[0] = sample;
	buff->dy[1] = buff->dy[0];
	buff->dy[0] = acc;

	return (int16_t)acc;
}

int16_t biquadFilter(int16_t sample, BiquadBuff *buff, BiquadCoeffs *coeffs)
{
	int64_t acc = buff->remainder;

	acc += (int64_t)coeffs->b[0] * sample + (int64_t)coeffs->b[1] * buff->x[0] + (int64_t)coeffs->b[2] *
		buff->x[1] - (int64_t)coeffs->a[0] * buff->y[0] - (int64_t)coeffs->a[1] * buff->y[1];

	buff->x[1] = buff->x[0];
	buff->x[0] = sample;
	buff->y[1] = buff->y[0];
	buff->y[0] = (int32_t)(acc >> 30);

	buff->remainder = (uint32_t)acc & 0x3FFFFFFF;

	return (int16_t)buff->y[0];
}

void filterSignal(size_t size, BiquadBuff *buff, BiquadCoeffs *coeffs)
{
	uint16_t i;

	for (i = 0; i < size / CHANNELS; i++)
	{
		dataBuff[i * CHANNELS] = biquadFilter(dataBuff[i * CHANNELS], &buff[0], coeffs);
		dataBuff[i * CHANNELS + 1] = biquadFilter(dataBuff[i * CHANNELS + 1], &buff[1], coeffs);
	}
}

void filterSignalIntr(size_t size, BiquadBuff *buff, BiquadCoeffs *coeffs)
{
	uint16_t i;
	ae_f32x2 coef1 = int32ToF32x2(coeffs->b[0], coeffs->b[0]);
	ae_f32x2 coef2 = int32ToF32x2(coeffs->b[1], coeffs->b[1]);
	ae_f32x2 coef3 = int32ToF32x2(coeffs->b[2], coeffs->b[2]);
	ae_f32x2 coef4 = int32ToF32x2(coeffs->a[0], coeffs->a[0]);
	ae_f32x2 coef5 = int32ToF32x2(coeffs->a[1], coeffs->a[1]);
	ae_f32x2 buffTmp;
	F64x2 acc;

	for (i = 0; i < size / CHANNELS; i++)
	{
		acc = putF64ToF64x2(
				LeftShiftA(uint32ToF64(buff[0].remainder), 15),
				LeftShiftA(uint32ToF64(buff[1].remainder), 15)
				);

		buffTmp = int16ToF32x2(dataBuff[i * CHANNELS], dataBuff[i * CHANNELS + 1]);
		acc = Mac(acc, coef1, buffTmp);

		buffTmp = int16ToF32x2(buff[0].y[0], buff[1].y[0]);
		acc = MSub(acc, coef4, buffTmp);

		buffTmp = int16ToF32x2(buff[0].x[0], buff[1].x[0]);
		acc = Mac(acc, coef2, buffTmp);

		buffTmp = int16ToF32x2(buff[0].y[1], buff[1].y[1]);
		acc = MSub(acc, coef5, buffTmp);

		buffTmp = int16ToF32x2(buff[0].x[1], buff[1].x[1]);
		acc = Mac(acc, coef3, buffTmp);

		acc.h = LeftShiftA(acc.h, 1);
		acc.l = LeftShiftA(acc.l, 1);

		buff[0].x[1] = buff[0].x[0];
		buff[0].x[0] = dataBuff[i * CHANNELS];
		buff[0].y[1] = buff[0].y[0];

		buff[1].x[1] = buff[1].x[0];
		buff[1].x[0] = dataBuff[i * CHANNELS + 1];
		buff[1].y[1] = buff[1].y[0];

		dataBuff[i * CHANNELS] = AE_MOVAD16_3(AE_MOVF16X4_FROMF64(acc.h));
		dataBuff[i * CHANNELS + 1] = AE_MOVAD16_3(AE_MOVF16X4_FROMF64(acc.l));

		buff[0].y[0] = dataBuff[i * CHANNELS];
		buff[1].y[0] = dataBuff[i * CHANNELS + 1];

		buff[0].remainder = (uint32_t)AE_MOVAD32_L(AE_MOVINT32X2_FROMF64(RightShiftA(acc.h, 16)));
		buff[1].remainder = (uint32_t)AE_MOVAD32_H(AE_MOVINT32X2_FROMF64(RightShiftA(acc.l, 16)));
	}
}

void run(FILE *inputFilePtr, FILE *outputFilePtr, BiquadBuff *buff, BiquadCoeffs *coeffs)
{
	size_t samplesRead;
	uint32_t i;

	while (1)
	{
		samplesRead = fread(dataBuff, BYTES_PER_SAMPLE, DATA_BUFF_SIZE * CHANNELS, inputFilePtr);

		if (!samplesRead)
		{
			break;
		}

		filterSignalIntr(samplesRead, buff, coeffs);
		fwrite(dataBuff, BYTES_PER_SAMPLE, samplesRead, outputFilePtr);
	}
}

