
#pragma once

namespace fixedstep {

constexpr double kStepSeconds = 1.0 / 60.0;
constexpr float kStep = 1.0f / 60.0f;

constexpr int kMaxSteps = 5;
constexpr unsigned kSampleEvery = 60;

struct Clock {
    double accumulated;
};

inline void add(Clock* clock, double seconds) {
    if (seconds > 0) clock->accumulated += seconds;
}

inline int take(Clock* clock) {
    const double cap = kMaxSteps * kStepSeconds;
    if (clock->accumulated > cap) clock->accumulated = cap;
    if (clock->accumulated < 0) clock->accumulated = 0;
    int steps = (int)(clock->accumulated / kStepSeconds);
    clock->accumulated -= steps * kStepSeconds;
    return steps;
}

}
