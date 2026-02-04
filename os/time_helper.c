#include "timer.h"

/**
 * Returns the current time in milliseconds as an int.
 * Based on syscall code. Allows kernal functions to get the current time.
 * 
 * Developed for LAB1.
 */
int get_time_as_int(void) {
    uint64 cycle = get_cycle();
    uint64 sec = cycle / CPU_FREQ;
    uint64 usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	return (int)(sec * 1000000 + usec);
}