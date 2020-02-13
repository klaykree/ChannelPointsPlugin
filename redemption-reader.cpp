// compile with: /D_UNICODE /DUNICODE /DWIN32 /D_WINDOWS /c

#include "redemption-reader.hpp"
#include "redemption-data.hpp"
#include <windows.h>
#include <stdlib.h>
#include <string>
#include <tchar.h>
#include <wrl.h>
#include <wil/com.h>
#include "WebView2.h"
#include <queue>
#include <Shlobj.h>

using namespace Microsoft::WRL;

EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#define HINST_THISCOMPONENT ((HINSTANCE)&__ImageBase)

// Global variables

// The main window class name
static TCHAR szWindowClass[] = _T("cpd_redemption_reader");

// The string that appears in the application's title bar
static TCHAR szTitle[] = _T("WebView sample");

HINSTANCE hInst;
HWND hwnd;

HANDLE MutationsTimer;
HANDLE MutationsTimerQueue;
DWORD MutationsTimerInterval = 1000;

MSG msg;

bool Initialised = false;

HANDLE MessageLoopThread = NULL;

// Forward declarations of functions included in this code module:
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
DWORD WINAPI MessageLoop(LPVOID lpParam);

// Pointer to WebView window
static wil::com_ptr<IWebView2WebView> WebviewWindow = nullptr;

std::queue<std::wstring> Mutations;
std::queue<int> QueuedRedemptions;

std::wstring ChannelURL;

void MakeWebView()
{
	WNDCLASSEX wcex;

	hInst = HINST_THISCOMPONENT;

	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInst;
	wcex.hIcon = LoadIcon(wcex.hInstance, IDI_APPLICATION);
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, IDI_APPLICATION);

	if(!RegisterClassEx(&wcex))
	{
		//Failed to register class
		return;
	}

	// The parameters to CreateWindow explained:
	// szWindowClass: the name of the application
	// szTitle: the text that appears in the title bar
	// WS_OVERLAPPEDWINDOW: the type of window to create
	// CW_USEDEFAULT, CW_USEDEFAULT: initial position (x, y)
	// 500, 100: initial size (width, length)
	// NULL: the parent of this window
	// NULL: this application does not have a menu bar
	// hInstance: the first parameter from WinMain
	// NULL: not used in this application
	hwnd = CreateWindow(
		szWindowClass,
		szTitle,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		1200, 600,
		NULL,
		NULL,
		hInst,
		NULL
	);

	if(!hwnd)
	{
		//Failed to create window
		return;
	}
	
	// The parameters to ShowWindow explained:
	// hWnd: the value returned from CreateWindow
	// nCmdShow: the fourth parameter from WinMain
	ShowWindow(hwnd, SW_SHOWDEFAULT);
	UpdateWindow(hwnd);

	ShowWindow(hwnd, SW_HIDE);

	// Step 3 - Create a single WebView within the parent window
	// Locate the browser and set up the environment for WebView
	PWSTR AppDataLocal;
	SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &AppDataLocal);

	WCHAR AppDataLocalDir[128];
	wcscpy_s(AppDataLocalDir, AppDataLocal);
	wcscat_s(AppDataLocalDir, 128, L"\\ChannelPointsDisplay");

	CoTaskMemFree(AppDataLocal);

	CreateWebView2EnvironmentWithDetails(nullptr, AppDataLocalDir, nullptr, Callback<IWebView2CreateWebView2EnvironmentCompletedHandler>(
			[](HRESULT result, IWebView2Environment* env) -> HRESULT {
				// Create a WebView, whose parent is the main window hWnd
				env->CreateWebView(hwnd, Callback<IWebView2CreateWebViewCompletedHandler>(
					[](HRESULT result, IWebView2WebView* webview) -> HRESULT {
						if(webview != nullptr) {
							WebviewWindow = webview;
						}

						// Add a few settings for the webview
						// this is a redundant demo step as they are the default settings values
						IWebView2Settings* Settings;
						WebviewWindow->get_Settings(&Settings);
						Settings->put_IsScriptEnabled(TRUE);
						Settings->put_AreDefaultScriptDialogsEnabled(TRUE);
						Settings->put_IsWebMessageEnabled(TRUE);
						
						// Resize WebView to fit the bounds of the parent window
						RECT bounds;
						GetClientRect(hwnd, &bounds);
						WebviewWindow->put_Bounds(bounds);
						
						// Schedule an async task to add initialization script that
						// 1) Add an listener to print message from the host
						// 2) Post document URL to the host
						WebviewWindow->AddScriptToExecuteOnDocumentCreated(
							L"window.addEventListener('load', function() {\
							var $$expectedId = 'chat-list__lines';\
							__webview_observers__ = window.__webview_observers__ || {};\
								(function() {\
									var target = document.getElementsByClassName($$expectedId)[0];\
									__webview_observers__[$$expectedId] = {\
											observer: new MutationObserver(function(mutations) {\
												if(mutations[0]['addedNodes'].length > 0) {\
													__webview_observers__[$$expectedId].mutations.push(mutations[0]['addedNodes'][0].textContent.toLowerCase());\
												}\
											})\
									};\
									__webview_observers__[$$expectedId].mutations = [];\
									var config = { attributes: true, childList : true, characterData : true, subtree : true };\
									__webview_observers__[$$expectedId].observer.observe(target, config);\
							})();\
						});", nullptr);

						Initialised = true;

						return S_OK;
					}).Get());
				return S_OK;
			}).Get());
}

void TryRetrieveMutation()
{
	HRESULT result = WebviewWindow->ExecuteScript(
		//general message: "justinfan54: hi" (with quotes)
		//redemption: "klaykree redeemed nice69" (with quotes)

		L"var mutationString = null;\
				if(window.__webview_observers__['chat-list__lines'].mutations.length > 0) {\
					mutationString = window.__webview_observers__['chat-list__lines'].mutations[0];\
					window.__webview_observers__['chat-list__lines'].mutations.splice(0, 1);\
				}\
				mutationString;",
		Callback<IWebView2ExecuteScriptCompletedHandler>(
			[](HRESULT errorCode, LPCWSTR resultObjectAsJson) -> HRESULT {
				std::wstring Result = std::wstring(resultObjectAsJson);

				if(Result != L"null")
				{
					std::wstring Redeemed = L" redeemed ";
					size_t RedeemedIndex = Result.find(Redeemed);
					if(RedeemedIndex != std::wstring::npos)
					{
						size_t ColonIndex = Result.find(':');
						if(ColonIndex != std::wstring::npos && ColonIndex < RedeemedIndex)
						{
							return S_OK; //Skip this mutation because a colon before " redeemed " would mean its a chat message
						}

						//Add the mutation
						Mutations.push(Result);
					}
				}
				return S_OK;
			}).Get());
}

int GetLatestRedemption(struct darray* Redemptions, int RedemptionCount)
{
	if(Mutations.empty() && QueuedRedemptions.empty())
		return -1;

	//Process all the mutations that have gathered
	while(!Mutations.empty())
	{
		std::wstring Mutation = Mutations.front();

		for(int i = 0 ; i < RedemptionCount ; ++i)
		{
			struct RedemptionData* Redemption = (RedemptionData*)darray_item(sizeof(RedemptionData), Redemptions, i);

			int size_needed = MultiByteToWideChar(CP_UTF8, 0, Redemption->Title.array, (int)Redemption->Title.len, NULL, 0);
			std::wstring Title(size_needed, 0);
			MultiByteToWideChar(CP_UTF8, 0, Redemption->Title.array, (int)Redemption->Title.len, &Title[0], size_needed);

			if(Title != L"" && Mutation.find(Title) != std::wstring::npos)
			{
				std::wstring Redeemed = L" redeemed ";
				size_t RedeemedIndex = Mutation.find(Redeemed);
				if(RedeemedIndex != std::wstring::npos)
				{
					//Check if the first thing after redemption title is a number,
					//it must be if its a true redemption since point cost follows the title
					if(isdigit(Mutation[RedeemedIndex + Redeemed.size() + Title.size()]))
					{
						QueuedRedemptions.push(i);
						break;
					}
				}
			}
		}

		Mutations.pop();
	}

	if(!QueuedRedemptions.empty())
	{
		int RedemptionIndex = QueuedRedemptions.front();
		QueuedRedemptions.pop();
		return RedemptionIndex;
	}

	return -1;
}

void ChangeChannelURL(const char* ChannelName)
{
	if(ChannelName != NULL && strlen(ChannelName) > 0)
	{
		ChannelURL = L"https://www.twitch.tv/embed/";

		int size_needed = MultiByteToWideChar(CP_UTF8, 0, ChannelName, (int)strlen(ChannelName), NULL, 0);
		std::wstring WChanneName(size_needed, 0);
		MultiByteToWideChar(CP_UTF8, 0, ChannelName, (int)strlen(ChannelName), &WChanneName[0], size_needed);

		ChannelURL.append(WChanneName);
		ChannelURL.append(L"/chat");

		if(WebviewWindow != nullptr)
		{
			WebviewWindow->Navigate(ChannelURL.c_str());
		}
	}
}

void StartRedemptionReader()
{
	MessageLoopThread = CreateThread(
		NULL,                   // default security attributes
		0,                      // use default stack size  
		MessageLoop,			// thread function name
		NULL,					// argument to thread function 
		0,                      // use default creation flags 
		NULL);					// returns the thread identifier
}

void StopRedemptionReader()
{
	SendMessage(hwnd, WM_CLOSE, 0, 0);
}

VOID CALLBACK UpdateMutationsTick(PVOID lpParam, BOOLEAN TimerOrWaitFired)
{
	if(!Initialised)
		return;

	TryRetrieveMutation();
}

DWORD WINAPI MessageLoop(LPVOID lpParam)
{
	MakeWebView();

	MutationsTimerQueue = CreateTimerQueue();
	CreateTimerQueueTimer(&MutationsTimer, MutationsTimerQueue, (WAITORTIMERCALLBACK)UpdateMutationsTick, NULL, MutationsTimerInterval, MutationsTimerInterval, 0);

	MSG msg;
	while(GetMessage(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;
}

//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_DESTROY  - post a quit message and return
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	TCHAR greeting[] = _T("Hello, Windows desktop!");

	switch(message)
	{
	case WM_CLOSE:
		DeleteTimerQueue(MutationsTimerQueue);

		if(WebviewWindow != nullptr)
		{
			WebviewWindow->Close();
		}

		//Close thread
		CloseHandle(MessageLoopThread);

		DestroyWindow(hwnd);

		UnregisterClass(szWindowClass, hInst);

		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
		break;
	}

	return 0;
}