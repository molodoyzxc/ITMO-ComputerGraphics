#include "InputDevice.h"
#include <iostream>
using namespace DirectX::SimpleMath;

InputDevice::InputDevice(HWND hWnd)
    : hWnd_(hWnd)
{
    RAWINPUTDEVICE Rid[2] = {};
    // Мышь
    Rid[0].usUsagePage = 0x01;
    Rid[0].usUsage = 0x02;
    Rid[0].dwFlags = RIDEV_INPUTSINK; // получаем события всегда
    Rid[0].hwndTarget = hWnd_;
    // Клавиатура
    Rid[1].usUsagePage = 0x01;
    Rid[1].usUsage = 0x06;
    Rid[1].dwFlags = RIDEV_INPUTSINK;
    Rid[1].hwndTarget = hWnd_;

    if (!RegisterRawInputDevices(Rid, 2, sizeof(Rid[0])))
    {
        std::cout << "ERROR RegisterRawInputDevices: "
            << GetLastError() << std::endl;
    }
}

InputDevice::~InputDevice() = default;

void InputDevice::OnKeyDown(KeyboardInputEventArgs args)
{
    bool isBreak = args.Flags & 0x01;
    Keys key = static_cast<Keys>(args.VKey);
    if (args.MakeCode == 42) key = Keys::LeftShift;
    if (args.MakeCode == 54) key = Keys::RightShift;

    if (isBreak)
        RemovePressedKey(key);
    else
        AddPressedKey(key);
}

void InputDevice::OnMouseMove(RawMouseEventArgs args)
{
    POINT p; GetCursorPos(&p);
    ScreenToClient(hWnd_, &p);
    MousePosition = Vector2(p.x, p.y);
    MouseOffset = Vector2(args.X, args.Y);
    MouseWheelDelta = args.WheelDelta;
    MouseMove.Broadcast({ MousePosition, MouseOffset, MouseWheelDelta });
}

void InputDevice::AddPressedKey(Keys key)
{
    keys_.insert(key);
}

void InputDevice::RemovePressedKey(Keys key)
{
    keys_.erase(key);
}

bool InputDevice::IsKeyDown(Keys key)
{
    return keys_.count(key) != 0;
}

HWND InputDevice::GetHwnd() const
{
    return hWnd_;
}

POINT InputDevice::GetCursorPosClient() const
{
    POINT p;
    ::GetCursorPos(&p);
    ::ScreenToClient(hWnd_, &p);
    return p;
}
