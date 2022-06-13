#include "mainwindow.h"
#include "ui_mainwindow.h"
#pragma region Constants

// Magnification lens refresh interval - Should be as low as possible to match monitor refresh rate.
const UINT          TIMER_INTERVAL_MS = 8;

// Magnification rates
const float         MAGNIFICATION_INCREMENT = 0.5f;
const float         MAGNIFICATION_LIMIT = 12.0f;

// lens sizing factors as a percent of screen resolution
const float         INIT_LENS_WIDTH_FACTOR = 0.5f;
const float         INIT_LENS_HEIGHT_FACTOR = 0.5f;
const float         INIT_LENS_RESIZE_HEIGHT_FACTOR = 0.0625f;
const float         INIT_LENS_RESIZE_WIDTH_FACTOR = 0.0625f;
const float         LENS_MAX_WIDTH_FACTOR = 1.2f;
const float         LENS_MAX_HEIGHT_FACTOR = 1.2f;

// lens shift/pan increments
const int           PAN_INCREMENT_HORIZONTAL = 75;
const int           PAN_INCREMENT_VERTICAL = 75;

#pragma endregion


#pragma region Variables

float               magnificationFactor = 2.0f;
float               newMagnificationFactor = magnificationFactor; // Temp mag factor to store change during update

SIZE                screenSize;
SIZE                lensSize; // Size in pixels of the lens (host window)
SIZE                newLensSize; // Temp size to store changes in lensSize during update
POINT               lensPosition; // Top left corner of the lens (host window)


SIZE                resizeIncrement;
SIZE                resizeLimit;

// Current mouse location
POINT               mousePoint;

#pragma endregion
#pragma region Objects

// Main program handle
const TCHAR         WindowClassName[] = TEXT("MagnifierWindow");

// Window handles
HWND                hwndHost;

MagWindow           mag1;
MagWindow           mag2;
MagWindow*          magActive;

// Show magnifier or not
BOOL                enabled;

// lens pan offset x|y
POINT               panOffset;

// Keyboard/Mouse hook
HHOOK               hkb;
KBDLLHOOKSTRUCT*    key;
BOOL                wkDown = FALSE;

// Timer interval structures
union FILETIME64
{
    INT64 quad;
    FILETIME ft;
};
FILETIME CreateRelativeFiletimeMS(DWORD milliseconds)
{
    FILETIME64 ft = { -static_cast<INT64>(milliseconds) * 10000 };
    return ft.ft;
}
FILETIME            timerDueTime = CreateRelativeFiletimeMS(TIMER_INTERVAL_MS);
PTP_TIMER           refreshTimer;

#pragma endregion
#pragma region Function Definitions

// Calculates an X or Y value where the lens (host window) should be relative to mouse position. i.e. top left corner of a window centered on mouse
#define LENS_POSITION_VALUE(MOUSEPOINT_VALUE, LENSSIZE_VALUE) (MOUSEPOINT_VALUE - (LENSSIZE_VALUE / 2) - 1)

// Calculates a lens size value that is slightly larger than (lens + increment) to give an extra buffer area on the edges
#define LENS_SIZE_BUFFER_VALUE(LENS_SIZE_VALUE, RESIZE_INCREMENT_VALUE) (LENS_SIZE_VALUE + (2 * RESIZE_INCREMENT_VALUE))

// Forward declarations.
ATOM                RegisterHostWindowClass(HINSTANCE hInstance);
BOOL                SetupMagnifierWindow(HINSTANCE hInst);
BOOL                SetupHostWindow(HINSTANCE hinst);

VOID                InitScreenDimensions();
VOID                RefreshMagnifier();
VOID                ToggleMagnifier();

#pragma endregion
#pragma region Host Window Proc

LRESULT CALLBACK HostWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_USER: // Exit on task tray icon click - very simple exit functionality
        switch (lParam)
        {
        case WM_LBUTTONUP:
            PostMessage(hwndHost, WM_CLOSE, 0, 0);
            break;
        case WM_RBUTTONUP:
            PostMessage(hwndHost, WM_CLOSE, 0, 0);
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }

    case WM_QUERYENDSESSION:
        PostMessage(hwndHost, WM_DESTROY, 0, 0);
        break;
    case WM_CLOSE:
        PostMessage(hwndHost, WM_DESTROY, 0, 0);
        break;
    case WM_DESTROY:
        enabled = FALSE;
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

#pragma endregion
VOID InitScreenDimensions()
{
    screenSize.cx = GetSystemMetrics(SM_CXSCREEN);
    screenSize.cy = GetSystemMetrics(SM_CYSCREEN);

    lensSize.cx = (int)(screenSize.cx * INIT_LENS_WIDTH_FACTOR);
    lensSize.cy = (int)(screenSize.cy * INIT_LENS_HEIGHT_FACTOR);
    newLensSize = lensSize; // match initial value

    resizeIncrement.cx = (int)(screenSize.cx * INIT_LENS_RESIZE_WIDTH_FACTOR);
    resizeIncrement.cy = (int)(screenSize.cy * INIT_LENS_RESIZE_HEIGHT_FACTOR);
    resizeLimit.cx = (int)(screenSize.cx * LENS_MAX_WIDTH_FACTOR);
    resizeLimit.cy = (int)(screenSize.cy * LENS_MAX_HEIGHT_FACTOR);
}

ATOM RegisterHostWindowClass(HINSTANCE hInstance)
{
    WNDCLASSEX wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = HostWndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(1 + COLOR_BTNFACE);
    wcex.lpszClassName = WindowClassName;

    return RegisterClassEx(&wcex);
}
BOOL SetupHostWindow(HINSTANCE hInst)
{
    // Create the host window.
    RegisterHostWindowClass(hInst);

    hwndHost = CreateWindowEx(
                WS_EX_LAYERED | // Required style to render the magnification correctly
                WS_EX_TOPMOST | // Always-on-top
                WS_EX_TRANSPARENT | // Click-through
                WS_EX_TOOLWINDOW, // Do not show program on taskbar
                WindowClassName,
                TEXT("Screen Magnifier"),
                WS_CLIPCHILDREN | // ???
                WS_POPUP | // Removes titlebar and borders - simply a bare window
                WS_BORDER, // Adds a 1-pixel border for tracking the edges - aesthetic
                lensPosition.x, lensPosition.y,
                lensSize.cx, lensSize.cy,

                nullptr, nullptr, hInst, nullptr);


    if (!hwndHost)
    {
        return FALSE;
    }

    // Make the window fully opaque.
    return SetLayeredWindowAttributes(hwndHost, 0, 255, LWA_ALPHA);
}

BOOL SetupMagnifierWindow(HINSTANCE hInst)
{
    SIZE magSize;
    magSize.cx = LENS_SIZE_BUFFER_VALUE(lensSize.cx, resizeIncrement.cx);
    magSize.cy = LENS_SIZE_BUFFER_VALUE(lensSize.cy, resizeIncrement.cy);
    POINT magPosition; // position in the host window coordinates - top left corner
    magPosition.x = 0;
    magPosition.y = 0;

    mag1 = MagWindow(magnificationFactor, magPosition, magSize);
    if (!mag1.Create(hInst, hwndHost, TRUE))
    {
        return FALSE;
    }
    magActive = &mag1;
    return TRUE;
}

VOID RefreshMagnifier()
{
    LPPOINT mousePosition = &mousePoint;
    lensPosition.x = LENS_POSITION_VALUE(mousePosition->x, lensSize.cx);
    lensPosition.y = LENS_POSITION_VALUE(mousePosition->y, lensSize.cy);
    GetCursorPos(&mousePoint);
    SetWindowPos(hwndHost, HWND_TOPMOST,
//                 350,440,400,400,
                 lensPosition.x, lensPosition.y, // x|y coordinate of top left corner
                             lensSize.cx, lensSize.cy, // width|height of window
                 SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOSIZE);
    magActive->UpdateMagnifier(&mousePoint, panOffset, lensSize);

}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    time500ms = startTimer(20);
    HINSTANCE     hInstance = nullptr;
    ui->setupUi(this);
    InitScreenDimensions();
    MagInitialize();
    SetupHostWindow(hInstance);
    SetupMagnifierWindow(hInstance);
    UpdateWindow(hwndHost);
    ShowWindow(hwndHost, SW_SHOWNOACTIVATE);//ShowWindow(hwndHost, SW_SHOWNOACTIVATE);
}
void MainWindow::timerEvent(QTimerEvent *event)
{
    if(event->timerId() == time500ms)
    {
        RefreshMagnifier();
    }
}
MainWindow::~MainWindow()
{
    delete ui;
}

