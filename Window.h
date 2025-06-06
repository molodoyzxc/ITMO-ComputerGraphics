#pragma once
#include <windows.h>
#include <string>
#include "InputDevice.h"

class Window
{
public:
    Window(HINSTANCE hInstance, int nCmdShow,
        const std::wstring& title, int width, int height);
    ~Window();

    bool ProcessMessages();
    HWND GetHwnd() const { return hWnd_; }
    InputDevice* GetInput() const { return  input_; };
    float GetWidth() const { return width_; };
    float GetHeight() const { return height_; };

private:
    bool RegisterWindowClass();

    HINSTANCE    hInstance_;
    HWND         hWnd_;
    std::wstring title_;
    float          width_, height_;
    const wchar_t* className_ = L"DX12WindowClass";

    static LRESULT CALLBACK WndProcThunk(
        HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT CALLBACK WndProc(
        HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    InputDevice* input_ = nullptr;
};
