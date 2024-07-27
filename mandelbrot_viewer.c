#include <windows.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <process.h>

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")

#define WIDTH 1920
#define HEIGHT 1080
#define MAX_ITERATIONS 512
#define MAX_DETAIL_LEVEL 5

typedef struct {
    int startY;
    int endY;
    double centerX;
    double centerY;
    double zoom;
    int detailLevel;
    unsigned char* buffer;
} ThreadData;

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void EnableOpenGL(HWND hwnd, HDC* hDC, HGLRC* hRC);
void DisableOpenGL(HWND hwnd, HDC hDC, HGLRC hRC);
void RenderMandelbrotSection(void* param);
void RenderMandelbrot(double centerX, double centerY, double zoom, int detailLevel);

LARGE_INTEGER lastPanZoomTime;
LARGE_INTEGER frequency;
bool needsUpdate = true;
double centerX = 0.0, centerY = 0.0, zoom = 1.0;
int currentDetailLevel = 0;
unsigned char* pixelBuffer;
HANDLE* threads;
ThreadData* threadData;
int numThreads;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEX wcex;
    HWND hwnd;
    HDC hDC;
    HGLRC hRC;
    MSG msg;
    BOOL bQuit = FALSE;
    
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_OWNDC;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = "MandelbrotViewer";
    wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassEx(&wcex))
        return 0;

    hwnd = CreateWindowEx(0, "MandelbrotViewer", "Mandelbrot Viewer",
                          WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                          WIDTH, HEIGHT, NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);
    EnableOpenGL(hwnd, &hDC, &hRC);

    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&lastPanZoomTime);

    // Get the number of processors
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    numThreads = sysInfo.dwNumberOfProcessors;

    // Allocate memory for threads and thread data
    threads = (HANDLE*)malloc(numThreads * sizeof(HANDLE));
    threadData = (ThreadData*)malloc(numThreads * sizeof(ThreadData));

    // Allocate pixel buffer
    pixelBuffer = (unsigned char*)malloc(WIDTH * HEIGHT * 3 * sizeof(unsigned char));

    while (!bQuit) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                bQuit = TRUE;
            } else {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        } else {
            LARGE_INTEGER currentTime;
            QueryPerformanceCounter(&currentTime);
            double timeSinceLastPanZoom = (currentTime.QuadPart - lastPanZoomTime.QuadPart) / (double)frequency.QuadPart;


            if (timeSinceLastPanZoom < 0.01) {
                currentDetailLevel = 1;
            }
            
            if (currentDetailLevel < MAX_DETAIL_LEVEL) {
                currentDetailLevel++;
            }

            glClear(GL_COLOR_BUFFER_BIT);
            RenderMandelbrot(centerX, centerY, zoom, currentDetailLevel);
            SwapBuffers(hDC);

            Sleep(1);
        }
    }

    // Clean up
    free(pixelBuffer);
    free(threads);
    free(threadData);

    DisableOpenGL(hwnd, hDC, hRC);
    DestroyWindow(hwnd);
    return msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static bool isPanning = false;
    static int lastMouseX, lastMouseY;

    switch (uMsg) {
        case WM_CLOSE:
            PostQuitMessage(0);
            break;

        case WM_LBUTTONDOWN:
            isPanning = true;
            lastMouseX = LOWORD(lParam);
            lastMouseY = HIWORD(lParam);
            QueryPerformanceCounter(&lastPanZoomTime);
            needsUpdate = true;
            break;

        case WM_LBUTTONUP:
            isPanning = false;
            break;

        case WM_MOUSEMOVE:
            if (isPanning) {
                int mouseX = LOWORD(lParam);
                int mouseY = HIWORD(lParam);
                double dx = (mouseX - lastMouseX) / (WIDTH * zoom);
                double dy = (mouseY - lastMouseY) / (HEIGHT * zoom);
                centerX -= dx;
                centerY += dy;
                lastMouseX = mouseX;
                lastMouseY = mouseY;
                QueryPerformanceCounter(&lastPanZoomTime);
                needsUpdate = true;
            }
            break;

        case WM_MOUSEWHEEL:
            {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                double zoomFactor = pow(1.1, delta / 120.0);
                zoom *= zoomFactor;
                QueryPerformanceCounter(&lastPanZoomTime);
                needsUpdate = true;
            }
            break;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    return 0;
}

void EnableOpenGL(HWND hwnd, HDC* hDC, HGLRC* hRC) {
    PIXELFORMATDESCRIPTOR pfd;
    int iFormat;

    *hDC = GetDC(hwnd);

    ZeroMemory(&pfd, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 16;
    pfd.iLayerType = PFD_MAIN_PLANE;

    iFormat = ChoosePixelFormat(*hDC, &pfd);
    SetPixelFormat(*hDC, iFormat, &pfd);

    *hRC = wglCreateContext(*hDC);
    wglMakeCurrent(*hDC, *hRC);
}

void DisableOpenGL(HWND hwnd, HDC hDC, HGLRC hRC) {
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(hRC);
    ReleaseDC(hwnd, hDC);
}

void RenderMandelbrotSection(void* param) {
    ThreadData* data = (ThreadData*)param;
    int stepSize = 1 << (MAX_DETAIL_LEVEL - data->detailLevel);

    for (int py = data->startY; py < data->endY; py += stepSize) {
        for (int px = 0; px < WIDTH; px += stepSize) {
            double x0 = (px - WIDTH / 2.0) / (0.5 * data->zoom * WIDTH) + data->centerX;
            double y0 = (py - HEIGHT / 2.0) / (0.5 * data->zoom * HEIGHT) + data->centerY;
            double x = 0.0;
            double y = 0.0;
            int iteration = 0;

            
            while (x*x + y*y < 4.0 && iteration < MAX_ITERATIONS) {
                double xtemp = x*x - y*y + x0;
                y = 2*x*y + y0;
                x = xtemp;
                iteration++;
            }
            
            unsigned char r, g, b;
            if (iteration < MAX_ITERATIONS) {
                double hue = (double)iteration / MAX_ITERATIONS;
                double saturation = 1.0;
                double value = 1.0;
                double c = value * saturation;
                double x = c * (1 - fabs(fmod(hue * 6.0, 2.0) - 1));
                double m = value - c;
                
                if (hue < 1.0/6.0) {
                    r = (c + m) * 255; g = (x + m) * 255; b = m * 255;
                } else if (hue < 2.0/6.0) {
                    r = (x + m) * 255; g = (c + m) * 255; b = m * 255;
                } else if (hue < 3.0/6.0) {
                    r = m * 255; g = (c + m) * 255; b = (x + m) * 255;
                } else if (hue < 4.0/6.0) {
                    r = m * 255; g = (x + m) * 255; b = (c + m) * 255;
                } else if (hue < 5.0/6.0) {
                    r = (x + m) * 255; g = m * 255; b = (c + m) * 255;
                } else {
                    r = (c + m) * 255; g = m * 255; b = (x + m) * 255;
                }
            } else {
                r = g = b = 0;
            }

            for (int i = 0; i < stepSize; i++) {
                for (int j = 0; j < stepSize; j++) {
                    if (px + i < WIDTH && py + j < data->endY) {
                        int index = ((py + j) * WIDTH + (px + i)) * 3;
                        data->buffer[index] = r;
                        data->buffer[index + 1] = g;
                        data->buffer[index + 2] = b;
                    }
                }
            }
        }
    }
}

void RenderMandelbrot(double centerX, double centerY, double zoom, int detailLevel) {
    int sectionHeight = HEIGHT / numThreads;

    for (int i = 0; i < numThreads; i++) {
        threadData[i].startY = i * sectionHeight;
        threadData[i].endY = (i == numThreads - 1) ? HEIGHT : (i + 1) * sectionHeight;
        threadData[i].centerX = centerX;
        threadData[i].centerY = centerY;
        threadData[i].zoom = zoom;
        threadData[i].detailLevel = detailLevel;
        threadData[i].buffer = pixelBuffer;

        threads[i] = (HANDLE)_beginthreadex(NULL, 0, (unsigned int (__stdcall *)(void *))RenderMandelbrotSection, &threadData[i], 0, NULL);
    }

    WaitForMultipleObjects(numThreads, threads, TRUE, INFINITE);

    for (int i = 0; i < numThreads; i++) {
        CloseHandle(threads[i]);
    }

    glDrawPixels(WIDTH, HEIGHT, GL_RGB, GL_UNSIGNED_BYTE, pixelBuffer);
}