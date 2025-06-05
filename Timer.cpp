#include "Timer.h"

Timer::Timer()
{
    QueryPerformanceFrequency((LARGE_INTEGER*)&m_frequency);
    Reset();
}

void Timer::Reset()
{
    QueryPerformanceCounter((LARGE_INTEGER*)&m_start);
    m_last = m_start;              
    m_elapsedSeconds = 0.0f;    
}

// обновляет прошедшее время с момента последнего вызова Tick.
void Timer::Tick()
{
    INT64 current;
    QueryPerformanceCounter((LARGE_INTEGER*)&current); // текущий момент времени
    m_elapsedSeconds = (current - m_last) / static_cast<float>(m_frequency); // разницу во времени
    m_last = current; // обновление
}

float Timer::GetElapsedSeconds() const
{
    return m_elapsedSeconds;
}

// общее количество секунд с момента Reset().
float Timer::GetTotalSeconds() const
{
    INT64 current;
    QueryPerformanceCounter((LARGE_INTEGER*)&current); // текущий момент времени
    return (current - m_start) / static_cast<float>(m_frequency); // разница между текущим и стартом
}
