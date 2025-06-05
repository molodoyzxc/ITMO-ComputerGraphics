#pragma once
#include <Windows.h>

class Timer
{
public:
    Timer();
    void Reset();
    void Tick();
    float GetElapsedSeconds() const;   // время последнего кадра
    float GetTotalSeconds() const;     // от сброса

private:
    INT64 m_start;
    INT64 m_last;
    INT64 m_frequency;
    float m_elapsedSeconds;
};
