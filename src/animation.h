/*
 * animation.h
 *
 * Created by buzzert <buzzert@buzzert.net> 2019-01-20
 */

#pragma once
#include <stdbool.h>
#include <time.h>

typedef double anim_time_interval_t;

struct animation_t;
typedef void(*AnimationCompletion)(struct animation_t *anim, void *context);

typedef enum {
    _EmptyAnimationType,
    ACursorAnimation,
    ALogoAnimation,
} AnimationType;

typedef struct {
    AnimationType type;

    bool   cursor_animating;
    double cursor_fade_direction;
} CursorAnimation;

typedef struct {
    AnimationType type;

    bool                 direction; // false: in, true: out
} LogoAnimation;

typedef union {
    AnimationType type;

    CursorAnimation cursor_anim;
    LogoAnimation   logo_anim;
} Animation;

typedef struct {
    bool                 completed;
    anim_time_interval_t start_time;

    AnimationCompletion  completion_func;
    void                *completion_func_context;

    Animation anim;
} animation_t;

// Convenience: returns current time as anim_time_interval_t
anim_time_interval_t anim_now();

// Easing functions
double anim_qubic_ease_out(double p);
double anim_quad_ease_out(double p);


