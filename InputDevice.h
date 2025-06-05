#pragma once
#include <windows.h>
#include <unordered_set>
#include "Exports.h"
#include "Delegates.h"
#include "Keys.h"
#include <DirectXMath.h>
#include <SimpleMath.h>

class GAMEFRAMEWORK_API InputDevice
{
public:
    struct MouseMoveEventArgs
    {
        DirectX::SimpleMath::Vector2 Position;
        DirectX::SimpleMath::Vector2 Offset;
        int WheelDelta;
    };

    DirectX::SimpleMath::Vector2 MousePosition;
    DirectX::SimpleMath::Vector2 MouseOffset;
    int MouseWheelDelta;

    MulticastDelegate<const MouseMoveEventArgs&> MouseMove;

    InputDevice(HWND hWnd);
    ~InputDevice();

    void AddPressedKey(Keys key);
    void RemovePressedKey(Keys key);
    bool IsKeyDown(Keys key);

public:
    struct KeyboardInputEventArgs
    {
        USHORT MakeCode;
        USHORT Flags;
        USHORT VKey;
        UINT   Message;
    };

    struct RawMouseEventArgs
    {
        int Mode, ButtonFlags, ExtraInformation;
        int Buttons, WheelDelta, X, Y;
    };

    void OnKeyDown(KeyboardInputEventArgs args);
    void OnMouseMove(RawMouseEventArgs args);

private:
    HWND hWnd_;
    std::unordered_set<Keys> keys_;
};
