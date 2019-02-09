/*
 * animation.c
 *
 * Created by buzzert <buzzert@buzzert.net> 2019-01-20
 */

#include "animation.h"
#include <stdlib.h>
#include <sys/param.h>

/*
 * Easing functions
 */

double anim_identity(double p)
{
    return p;
}

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

double anim_progress_ease(animation_t *anim, const double duration, AnimationEasingFunc easing_f)
{
    const anim_time_interval_t now = anim_now();
    double progress = MAX(0.0, now - anim->start_time) / duration;
    progress = easing_f(progress);

    return (anim->direction == IN) ? progress : (1.0 - progress);
}

double anim_progress(animation_t *anim, const double duration)
{
    return anim_progress_ease(anim, duration, anim_identity);
}

bool anim_complete(animation_t *anim, const double progress)
{
   return (anim->direction == IN) ? progress >= 1.0 : progress <= 0.0;
}


