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
    IN,
    OUT
} AnimationDirection;

typedef enum {
    _EmptyAnimationType,
    ACursorAnimation,
    ALogoAnimation,
    ARedFlashAnimation,
    ASpinnerAnimation,
} AnimationType;

// Cursor flash animation
typedef struct {
    bool cursor_animating;
} CursorAnimation;

// Logo transition in/out animation
typedef struct {
} LogoAnimation;

// Red flash for incorrect password
typedef struct {
    unsigned           flash_count;
} RedFlashAnimation;

// Spinner shown when checking password
typedef struct {
    double rotation;
} SpinnerAnimation;

typedef union {
    CursorAnimation   cursor_anim;
    LogoAnimation     logo_anim;
    RedFlashAnimation redflash_anim;
    SpinnerAnimation  spinner_anim;
} Animation;

typedef struct {
    AnimationType        type;
    Animation            anim;

    bool                 completed;
    anim_time_interval_t start_time;

    AnimationDirection   direction;

    AnimationCompletion  completion_func;
    void                *completion_func_context;
} animation_t;

// Convenience functions

// returns current time as anim_time_interval_t
anim_time_interval_t anim_now();

// Returns normalized progress based on start time of `anim` and `duration`
double anim_progress(animation_t *anim, const double duration);

// Returns true if `anim` is complete depending on direction
bool anim_complete(animation_t *anim, const double progress);

// Easing functions
double anim_qubic_ease_out(double p);
double anim_quad_ease_out(double p);


