/*
 * animation.c
 *
 * Created by buzzert <buzzert@buzzert.net> 2019-01-20
 */

#include "animation.h"
#include <stdlib.h>

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

/*
 * Convenience calculation functions
 */

anim_time_interval_t anim_now()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);

    long ms = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
    return (ms / 1000.0);
}

double anim_progress(animation_t *anim, const double duration)
{
    const anim_time_interval_t now = anim_now();
    double progress = (now - anim->start_time) / duration;
    return (anim->direction == IN) ? progress : (1.0 - progress);
}

bool anim_complete(animation_t *anim, const double progress)
{
   return (anim->direction == IN) ? progress >= 1.0 : progress <= 0.0;
}


