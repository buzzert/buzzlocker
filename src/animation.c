/*
 * animation.c
 *
 * Created by buzzert <buzzert@buzzert.net> 2019-01-20
 */

#include "animation.h"
#include <stdlib.h>

anim_time_interval_t anim_now()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);

    long ms = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
    return (ms / 1000.0);
}

/*
 * Easing functions
 */

double anim_qubic_ease_out(double p)
{
	double f = (p - 1.0);
    return (f * f * f) + 1;
}

double anim_quad_ease_out(double p)
{
    return -(p * (p - 2.0));
}


