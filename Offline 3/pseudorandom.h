#ifndef _PSEUDORANDOM_H
#define _PSEUDORANDOM_H

#include "types.h"
#define SIZE 624
#define PERIOD 397
#define M32(x) (0x80000000 & x) // 32nd MSB
#define L31(x) (0x7FFFFFFF & x) // 31 LSBs

static const int DIFF = SIZE - PERIOD;
static const uint MAGIC = 0x9908b0df;

struct randomStuff
{
    uint MT[SIZE];
    uint MT_TEMPERED[SIZE];
    int index;
};

static struct randomStuff randomstuff = {.index = SIZE};

#define UNROLL(expr) \
  y = M32(randomstuff.MT[i]) | L31(randomstuff.MT[i+1]); \
  randomstuff.MT[i] = randomstuff.MT[expr] ^ (y >> 1) ^ ((((int)y << 31) >> 31) & MAGIC); \
  ++i;
  
void
GenerateNumbers()
{
  int i = 0;
  uint y;

  while ( i < DIFF ) {
    UNROLL(i+PERIOD);
  }

  while ( i < SIZE -1 ) {
    UNROLL(i-DIFF);
  }

  {
    y = M32(randomstuff.MT[SIZE-1]) | L31(randomstuff.MT[0]);
    randomstuff.MT[SIZE-1] = randomstuff.MT[PERIOD-1] ^ (y >> 1) ^ ((((int)y << 31) >>
          31) & MAGIC);
  }

  // Temper all numbers in a batch
  for (int i = 0; i < SIZE; ++i) {
    y = randomstuff.MT[i];
    y ^= y >> 11;
    y ^= y << 7  & 0x9d2c5680;
    y ^= y << 15 & 0xefc60000;
    y ^= y >> 18;
    randomstuff.MT_TEMPERED[i] = y;
  }

  randomstuff.index = 0;
}

void
srand(int newSeed)
{
    randomstuff.MT[0] = newSeed;
    randomstuff.index = SIZE;

    for ( uint i=1; i<SIZE; ++i )
        randomstuff.MT[i] = 0x6c078965*(randomstuff.MT[i-1] ^ randomstuff.MT[i-1]>>30) + i;
}

int
rand(void)
{
    if ( randomstuff.index == SIZE ) {
        GenerateNumbers();
        randomstuff.index = 0;
    }
    int retVal = randomstuff.MT_TEMPERED[randomstuff.index++];
    if(retVal < 0){
        retVal *= (-1);
    }
    return retVal;
}

int
randUpTo(int limit)
{
    return rand() % limit;
}

#endif