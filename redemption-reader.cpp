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
#include <mutex>

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

std::mutex MutationsMutex;

MSG msg;

bool Initialised = false;

void (*InitialisedCallback)(void) = NULL;

HANDLE MessageLoopThread = NULL;

WCHAR AppDataLocalDir[128];

// Forward declarations of functions included in this code module:
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
DWORD WINAPI MessageLoop(LPVOID lpParam);

// Pointer to WebView window
//static wil::com_ptr<ICoreWebView2Host> WebViewWindow = nullptr;
//static wil::com_ptr<ICoreWebView2> WebView = nullptr;
static wil::com_ptr<IWebView2WebView> WebView = nullptr; //webview rollback

std::queue<std::wstring> Mutations;
std::queue<int> QueuedRedemptions;

std::wstring ChannelURL;

void InitialiseWebView();

void SetWebViewFailedEvent()
{
	WebView->add_ProcessFailed(
		//Callback<ICoreWebView2ProcessFailedEventHandler>(
		Callback<IWebView2ProcessFailedEventHandler>( //webview rollback
			//[](ICoreWebView2* sender, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT
			[](IWebView2WebView* sender, IWebView2ProcessFailedEventArgs* args) -> HRESULT //webview rollback
			{
				Initialised = false;
				blog(LOG_WARNING, "ChannelPointsDisplay - WebView process failed - attempting to restart");

				InitialiseWebView();

				return S_OK;
			}).Get(), nullptr);
}

void InitialiseWebView()
{
	//HRESULT result = CreateCoreWebView2EnvironmentWithDetails(nullptr, AppDataLocalDir, nullptr,
	HRESULT result = CreateWebView2EnvironmentWithDetails(nullptr, AppDataLocalDir, nullptr, //webview rollback
		//Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
		Callback<IWebView2CreateWebView2EnvironmentCompletedHandler>( //webview rollback
		//[](HRESULT result, ICoreWebView2Environment* env) -> HRESULT
		[](HRESULT result, IWebView2Environment* env) -> HRESULT //webview rollback
		{
			if(result != S_OK)
			{
				blog(LOG_WARNING, "ChannelPointsDisplay - Failed to create WebView2 environment (error #%i)", result);
				return result;
			}

			// Create a WebView, whose parent is the main window hWnd
			//env->CreateCoreWebView2Host(hwnd,
			env->CreateWebView(hwnd, //webview rollback
				//Callback<ICoreWebView2CreateCoreWebView2HostCompletedHandler>(
				Callback<IWebView2CreateWebViewCompletedHandler>( //webview rollback
				//[](HRESULT result, ICoreWebView2Host* webview) -> HRESULT
				[](HRESULT result, IWebView2WebView* webview) -> HRESULT //webview rollback
				{
					if(result != S_OK)
					{
						blog(LOG_WARNING, "ChannelPointsDisplay - Failed to create WebView2 host (error #%i)", result);
						return result;
					}

					if(webview != nullptr) {
						//WebViewWindow = webview;
						//WebViewWindow->get_CoreWebView2(&WebView);
						WebView = webview; //webview rollback
					}

					// Resize WebView to fit the bounds of the parent window
					RECT bounds;
					GetClientRect(hwnd, &bounds);
					//WebViewWindow->put_Bounds(bounds);
					WebView->put_Bounds(bounds); //webview rollback

					WebView->AddScriptToExecuteOnDocumentCreated(
						L"window.addEventListener('load', function() {\
							var $$expectedId = 'chat-list__lines';\
							__webview_observers__ = window.__webview_observers__ || {};\
								(function() {\
									var target = document.getElementsByClassName($$expectedId)[0];\
									__webview_observers__[$$expectedId] = {\
											observer: new MutationObserver(function(mutations) {\
												if(mutations[0]['addedNodes'].length > 0) {\
													let addedNode = mutations[0]['addedNodes'][0];\
													if(addedNode.children.length > 0) {\
														if(addedNode.children[0].className.includes('channel-points-reward-line')) {\
															__webview_observers__[$$expectedId].mutations.push(addedNode.children[0].textContent.toLowerCase());\
														}\
													}\
												}\
											})\
									};\
									__webview_observers__[$$expectedId].mutations = [];\
									var config = { attributes: true, childList : true, characterData : true, subtree : true };\
									__webview_observers__[$$expectedId].observer.observe(target, config);\
							})();\
						});",
						//Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
						Callback<IWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>( //webview rollback
						[](HRESULT result, LPCWSTR id) -> HRESULT
						{
							if(result != S_OK)
							{
								blog(LOG_WARNING, "ChannelPointsDisplay - Failed to add script WebView2 (error #%i)", result);
							}

							return result;
						}).Get());

					SetWebViewFailedEvent();

					Initialised = true;

					ShowWindow(hwnd, SW_HIDE);

					blog(LOG_INFO, "ChannelPointsDisplay - Initialised webview");

					if(InitialisedCallback != NULL)
						InitialisedCallback();

					return result;
				}).Get());
			return result;
		}).Get());

	if(result != S_OK)
		blog(LOG_WARNING, "ChannelPointsDisplay - Create CoreWebView failed (error #%i) - likely requires new Edge browser", result);
}

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

	// Step 3 - Create a single WebView within the parent window
	// Locate the browser and set up the environment for WebView
	PWSTR AppDataLocal;
	SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &AppDataLocal);

	wcscpy_s(AppDataLocalDir, AppDataLocal);
	wcscat_s(AppDataLocalDir, 128, L"\\ChannelPointsDisplay");
	
	CoTaskMemFree(AppDataLocal);

	InitialiseWebView();
}

void TryRetrieveMutation()
{
	//Force the window to update otherwise the following script execute will miss some mutations
	ShowWindow(hwnd, SW_HIDE);
	UpdateWindow(hwnd);

	HRESULT result = WebView->ExecuteScript(
		//general message: "justinfan54: hi" (with quotes)
		//redemption: "klaykree redeemed nice69" (with quotes)

		L"var mutationString = null;\
				if(window.__webview_observers__['chat-list__lines'].mutations.length > 0) {\
					mutationString = window.__webview_observers__['chat-list__lines'].mutations[0];\
					window.__webview_observers__['chat-list__lines'].mutations.splice(0, 1);\
				}\
				mutationString;",
		//Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
		Callback<IWebView2ExecuteScriptCompletedHandler>( //webview rollback
			[](HRESULT errorCode, LPCWSTR resultObjectAsJson) -> HRESULT {
				if(errorCode != S_OK)
				{
					blog(LOG_WARNING, "ChannelPointsDisplay - Failed to execute script (%i)", errorCode);
				}

				std::wstring Result = std::wstring(resultObjectAsJson);

				//For testing redemptions without requiring actual redemptions
				/*if(rand() % 20 == 0)
				{
					const std::scoped_lock<std::mutex> lock(MutationsMutex);
					Mutations.push(L"klaykree redeemed nice69");
				}*/

				if(Result != L"null")
				{
					//std::wstring Redeemed = L"redeemed ";
					//size_t RedeemedIndex = Result.find(Redeemed);
					//if(RedeemedIndex != std::wstring::npos)
					{
						//size_t ColonIndex = Result.find(':');
						//if(ColonIndex != std::wstring::npos && ColonIndex < RedeemedIndex)
						//{
						//	return S_OK; //Skip this mutation because a colon before " redeemed " would mean its a chat message
						//}

						const std::scoped_lock<std::mutex> lock(MutationsMutex);

						//Add the mutation
						Mutations.push(Result);
					}
				}
				return S_OK;
			}).Get());
}

int GetLatestRedemption(struct darray* Redemptions, int RedemptionCount)
{
	const std::scoped_lock<std::mutex> lock(MutationsMutex);

	if(Mutations.empty() && QueuedRedemptions.empty())
		return -1;

	//Process all the mutations that have gathered
	while(!Mutations.empty())
	{
		std::wstring Mutation = Mutations.front();

		for(int i = 0 ; i < RedemptionCount ; ++i)
		{
			if(i >= Redemptions->num)
			{
				break;
			}

			RedemptionData* Redemption = (RedemptionData*)darray_item(sizeof(RedemptionData), Redemptions, i);

			int size_needed = MultiByteToWideChar(CP_UTF8, 0, Redemption->Title.array, (int)Redemption->Title.len, NULL, 0);
			std::wstring Title(size_needed, 0);
			MultiByteToWideChar(CP_UTF8, 0, Redemption->Title.array, (int)Redemption->Title.len, &Title[0], size_needed);

			if(Title != L"" && Mutation.find(Title) != std::wstring::npos)
			{
				std::wstring Redeemed = L"redeemed ";
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

bool ChangeChannelURL(const char* ChannelName)
{
	ChannelURL = L"https://www.twitch.tv/embed/";

	int size_needed = MultiByteToWideChar(CP_UTF8, 0, ChannelName, (int)strlen(ChannelName), NULL, 0);
	std::wstring WChanneName(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, ChannelName, (int)strlen(ChannelName), &WChanneName[0], size_needed);

	ChannelURL.append(WChanneName);
	ChannelURL.append(L"/chat");

	if(WebView != nullptr)
	{
		HRESULT Result = WebView->Navigate(ChannelURL.c_str());
		return true;
	}

	return false;
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
	const std::scoped_lock<std::mutex> lock(MutationsMutex); //Avoids crashing if the mutex is locked elsewhere when closing OBS

	DeleteTimerQueueEx(MutationsTimerQueue, NULL);
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
		//if(WebViewWindow != nullptr)
		if(WebView != nullptr) //webview rollback
		{
			blog(LOG_INFO, "ChannelPointsDisplay - Closing webview");
			//WebViewWindow->Close();
			WebView->Close(); //webview rollback
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