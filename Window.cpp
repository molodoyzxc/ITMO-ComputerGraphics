#include "Window.h"
#include "CubeApp.h"

Window::Window(HINSTANCE hInstance, int nCmdShow,
    const std::wstring& title, int width, int height)
    : hInstance_(hInstance),
    title_(title), width_(width), height_(height)
{
    RegisterWindowClass();

    hWnd_ = CreateWindowEx(
        0,
        className_,             
        title_.c_str(),         
        WS_OVERLAPPEDWINDOW,    
        CW_USEDEFAULT, CW_USEDEFAULT, // позиция окна по умолчанию
        width_, height_,        
        nullptr, nullptr,       
        hInstance_,             
        this                    
    );

    ShowWindow(hWnd_, nCmdShow);

    input_ = new InputDevice(hWnd_); // объект ввода

    // устройство для получения сырых событий клавиатуры
    RAWINPUTDEVICE Rid;
    Rid.usUsagePage = 0x01;     // устройство ввода
    Rid.usUsage = 0x06;         // клавиатура
    Rid.dwFlags = RIDEV_INPUTSINK;
    Rid.hwndTarget = hWnd_;     // получатель событий

    RegisterRawInputDevices(&Rid, 1, sizeof(RAWINPUTDEVICE)); // регистрация
}

Window::~Window()
{
    delete input_;
    DestroyWindow(hWnd_);
}

bool Window::RegisterWindowClass()
{
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW; // перерисовка при изменении размеров
    wc.lpfnWndProc = WndProcThunk;      // функция обработки сообщений
    wc.hInstance = hInstance_;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); 
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = className_;
    wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

    return RegisterClassEx(&wc) != 0;
}

LRESULT CALLBACK Window::WndProcThunk(
    HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    Window* window = nullptr;

    if (message == WM_NCCREATE)
    {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        window = reinterpret_cast<Window*>(cs->lpCreateParams); // получаем указатель на Window
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)window); // сохраняем в UserData
        window->hWnd_ = hWnd; // запоминаем HWND
    }
    else
    {
        window = reinterpret_cast<Window*>(
            GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    return window
        ? window->WndProc(hWnd, message, wParam, lParam)
        : DefWindowProc(hWnd, message, wParam, lParam);
}

// оконная процедура
inline LRESULT Window::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    // обработка ввода
    case WM_INPUT:
    {
        UINT dwSize = 0;
        // размер буфера
        GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &dwSize, sizeof(RAWINPUTHEADER));
        if (dwSize == 0)
            break;

        std::vector<BYTE> buffer(dwSize);
        if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buffer.data(), &dwSize, sizeof(RAWINPUTHEADER)) != dwSize)
            break;

        // преобразование в RAWINPUT
        RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(buffer.data());

        if (raw->header.dwType == RIM_TYPEKEYBOARD)
        {
            const RAWKEYBOARD& rk = raw->data.keyboard;

            InputDevice::KeyboardInputEventArgs args = {
                rk.MakeCode, // физический код клавиши
                rk.Flags,    // флаги нажатия/отпускания
                rk.VKey,     // виртуальный код
                rk.Message,  // сообщение
            };

            input_->OnKeyDown(args);
        }

        return 0;
    }

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
}

// обработка сообщений
bool Window::ProcessMessages()
{
    MSG msg = {};
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);  // обработка WM_CHAR
        DispatchMessage(&msg);   // отправка в оконную процедуру

        if (msg.message == WM_QUIT) // выход
            return false;
    }
    return true;
}
    