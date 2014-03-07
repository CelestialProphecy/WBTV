#ifndef __WBTV_RAND_HEADER__
#define __WBTV_RAND_HEADER__
void WBTV_doRand();
void WBTV_doRand(uint32_t seed);
uint32_t WBTV_rawrand();
unsigned long WBTV_rand(unsigned long min, unsigned long max);
long WBTV_rand(long min,long max);
unsigned char WBTV_rand(unsigned char min, unsigned char max);
unsigned int WBTV_rand(unsigned int min, unsigned int max);
int WBTV_rand(int min, int max);
unsigned int WBTV_rand(unsigned int max);
unsigned char WBTV_rand(unsigned char max);
unsigned char WBTV_urand_byte();
void WBTV_true_rand();
#endif
