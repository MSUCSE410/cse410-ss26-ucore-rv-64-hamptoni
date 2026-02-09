#include "timer.h"

/**
 * Returns the current time in milliseconds as an int.
 * Based on syscall code. Allows kernal functions to get the current time.
 * 
 * Developed for LAB1.
 */
int gettime_as_int(void) {
    uint64 cycle = get_cycle();
	return (int)((cycle * 1000 / CPU_FREQ));
}