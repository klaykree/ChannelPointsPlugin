#include <obs-module.h>
#include <math.h>
#include <util/bmem.h>
#include <util/threading.h>
#include <util/platform.h>
#include <obs.h>
#include <Windows.h>
#include <obs-frontend-api.h>
#include <util/dstr.h>
#include <util/darray.h>
#include "directory-reader.hpp"
#include "redemption-reader.hpp"
#include "redemption-data.hpp"

#define MAX_REDEMPTION_COUNT 40

static bool TryStartShowingRedemption(void* data, RedemptionData* Redemption);

enum State { State_Read, State_FadeIn, State_Wait, State_FadeOut };

struct channelpoints_data {
	bool Initialised;
	bool InitialisedThread;
	bool CreatedSources;
	RedemptionData* CurrentRedemptionShown;
	struct dstr ChannelName;
	int RedemptionCount;
	struct darray Redemptions;
	pthread_t Thread;
	HANDLE TickThread;
	bool Ticking;
	HANDLE TimerHandle;
	HANDLE TimerQueue;
	enum State CurrentState;
	os_event_t* Event;
	obs_source_t* MainSource;
	obs_sceneitem_t* MainItem;
	obs_source_t* GroupSource;
	obs_sceneitem_t* GroupItem;
	obs_source_t* ImageSource;
	obs_sceneitem_t* ImageItem;
	obs_source_t* AlphaFilter;
};

struct channelpoints_data* CPD;

static long long GetOpacity(void* data)
{
	struct channelpoints_data* cpd = data;

	obs_data_t* ImageSettings = obs_source_get_settings(cpd->AlphaFilter);
	long long Alpha = obs_data_get_int(ImageSettings, "opacity");
	obs_data_release(ImageSettings);
	return Alpha;
}

static void SetOpacity(void* data, long long Alpha)
{
	struct channelpoints_data* cpd = data;

	obs_data_t* ImageSettings = obs_source_get_settings(cpd->AlphaFilter);
	obs_data_set_int(ImageSettings, "opacity", Alpha);
	obs_source_update(cpd->AlphaFilter, ImageSettings);
	obs_data_release(ImageSettings);
}

//Returns true when completely opaque
static bool IncrementImageAlpha(void* data)
{
	struct channelpoints_data* cpd = data;

	long long Alpha = GetOpacity(data);
	if(++Alpha > 100)
		Alpha = 100;
	SetOpacity(data, Alpha);

	return Alpha == 100;
}

//Returns true when completely transparent
static bool DecrementImageAlpha(void* data)
{
	struct channelpoints_data* cpd = data;

	long long Alpha = GetOpacity(data);
	if(--Alpha < 0)
		Alpha = 0;
	SetOpacity(data, Alpha);

	return Alpha == 0;
}

static void FadeIn(void* data)
{
	struct channelpoints_data* cpd = data;

	while(!IncrementImageAlpha(data))
	{
		os_sleep_ms((uint32_t)cpd->CurrentRedemptionShown->FadeDuration / 100);
	}
}

static void FadeOut(void* data)
{
	struct channelpoints_data* cpd = data;

	while(!DecrementImageAlpha(data))
	{
		os_sleep_ms((uint32_t)cpd->CurrentRedemptionShown->FadeDuration / 100);
	}
}

static bool ImageSourceValid()
{
	obs_source_t* sceneSource = obs_frontend_get_current_scene();
	obs_scene_t* scene = obs_scene_from_source(sceneSource);
	obs_sceneitem_t* ImageItem = obs_scene_find_source(scene, "_cpd_image");
	obs_source_release(sceneSource);

	return ImageItem != NULL;
}

bool ToggleSource(void* SourceNameToToggle, obs_source_t* SceneSource)
{
	obs_scene_t* Scene = obs_scene_from_source(SceneSource);
	const char* Name = SourceNameToToggle;
	obs_sceneitem_t* SceneItem = obs_scene_find_source(Scene, Name);
	if(SceneItem)
	{
		bool Toggle = !obs_sceneitem_visible(SceneItem);
		obs_sceneitem_set_visible(SceneItem, Toggle);
		return false; //Finish
	}

	return true; //Continue enumeration
}

bool ActivateSource(void* SourceNameToActivate, obs_source_t* SceneSource)
{
	obs_scene_t* Scene = obs_scene_from_source(SceneSource);
	const char* Name = SourceNameToActivate;
	obs_sceneitem_t* SceneItem = obs_scene_find_source(Scene, Name);
	if(SceneItem)
	{
		obs_sceneitem_set_visible(SceneItem, true);
		return false; //Finish
	}

	return true; //Continue enumeration
}

bool DeactivateSource(void* SourceNameToDeactivate, obs_source_t* SceneSource)
{
	obs_scene_t* Scene = obs_scene_from_source(SceneSource);
	const char* Name = SourceNameToDeactivate;
	obs_sceneitem_t* SceneItem = obs_scene_find_source(Scene, Name);
	if(SceneItem)
	{
		obs_sceneitem_set_visible(SceneItem, false);
		return false; //Finish
	}

	return true; //Continue enumeration
}

static void ChangeSourceVisibilities(RedemptionData* Redemption)
{
	if(Redemption)
	{
		if(!dstr_is_empty(&Redemption->ToggleSource))
		{
			obs_enum_scenes(ToggleSource, Redemption->ToggleSource.array);
		}

		if(!dstr_is_empty(&Redemption->ActivateSource))
		{
			obs_enum_scenes(ActivateSource, Redemption->ActivateSource.array);
		}

		if(!dstr_is_empty(&Redemption->DeactivateSource))
		{
			obs_enum_scenes(DeactivateSource, Redemption->DeactivateSource.array);
		}
	}
}

static bool TryGetRedemption(void* data)
{
	struct channelpoints_data* cpd = data;

	int RedemptionIndex = GetLatestRedemption(&cpd->Redemptions, cpd->RedemptionCount);
	if(RedemptionIndex != -1)
	{
		RedemptionData* Redemption = (RedemptionData*)darray_item(sizeof(RedemptionData), &cpd->Redemptions, RedemptionIndex);
		ChangeSourceVisibilities(Redemption);
		return TryStartShowingRedemption(data, Redemption);
	}
	else
	{
		cpd->CurrentRedemptionShown = NULL;
		return false;
	}
}

DWORD WINAPI Tick(LPVOID lpParam)
{
	struct channelpoints_data* cpd = lpParam;

	LARGE_INTEGER Frequency;
	LARGE_INTEGER StartingTime, EndingTime, ElapsedMicroseconds;
	LARGE_INTEGER Timer = { 0 };

	cpd->Ticking = true;

	while(cpd->Ticking)
	{
		QueryPerformanceFrequency(&Frequency);
		QueryPerformanceCounter(&StartingTime);

		//Sleep just a bit to not completely thrash the thread
		os_sleep_ms(7);

		QueryPerformanceCounter(&EndingTime);
		ElapsedMicroseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart;

		ElapsedMicroseconds.QuadPart *= 1000;
		ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;

		//Update a timer to check for redemptions at intervals and for the fading in/out
		//Cant use Sleep because it would halt obs from closing until the sleep is complete
		Timer.QuadPart += ElapsedMicroseconds.QuadPart;

		if(cpd->CurrentState == State_Read && Timer.QuadPart >= 1000)
		{
			Timer.QuadPart -= 1000;
			if(TryGetRedemption(cpd))
			{
				cpd->CurrentState = State_FadeIn;
			}
		}

		if(cpd->CurrentState == State_FadeIn && Timer.QuadPart >= cpd->CurrentRedemptionShown->FadeDuration / 100)
		{
			if(IncrementImageAlpha(cpd))
			{
				cpd->CurrentState = State_Wait;
			}
			Timer.QuadPart -= cpd->CurrentRedemptionShown->FadeDuration / 100;
		}
		else if(cpd->CurrentState == State_Wait && Timer.QuadPart >= cpd->CurrentRedemptionShown->ShowDuration)
		{
			cpd->CurrentState = State_FadeOut;
			Timer.QuadPart -= cpd->CurrentRedemptionShown->ShowDuration;
		}
		else if(cpd->CurrentState == State_FadeOut && Timer.QuadPart >= cpd->CurrentRedemptionShown->FadeDuration / 100)
		{
			if(DecrementImageAlpha(cpd))
			{
				cpd->CurrentState = State_Read;
			}
			Timer.QuadPart -= cpd->CurrentRedemptionShown->FadeDuration / 100;
		}
	}

	return 0;
}

VOID CALLBACK UpdateMutationsTick(PVOID lpParam, BOOLEAN TimerOrWaitFired)
{
	struct channelpoints_data* cpd = lpParam;

	if(cpd == NULL)
		return;

	if(cpd->CurrentState == State_Read)
	{
		if(TryGetRedemption(cpd))
		{
			cpd->CurrentState = State_FadeIn;
			ChangeTimerQueueTimer(cpd->TimerQueue, cpd->TimerHandle, (ULONG)cpd->CurrentRedemptionShown->FadeDuration / 100, (ULONG)cpd->CurrentRedemptionShown->FadeDuration / 100);
		}
	}
	
	if(cpd->CurrentState == State_FadeIn)
	{
		if(IncrementImageAlpha(cpd))
		{
			cpd->CurrentState = State_Wait;
			ChangeTimerQueueTimer(cpd->TimerQueue, cpd->TimerHandle, (ULONG)cpd->CurrentRedemptionShown->ShowDuration, (ULONG)cpd->CurrentRedemptionShown->ShowDuration);
		}
	}
	else if(cpd->CurrentState == State_Wait)
	{
		cpd->CurrentState = State_FadeOut;
		ChangeTimerQueueTimer(cpd->TimerQueue, cpd->TimerHandle, (ULONG)cpd->CurrentRedemptionShown->FadeDuration / 100, (ULONG)cpd->CurrentRedemptionShown->FadeDuration / 100);
	}
	else if(cpd->CurrentState == State_FadeOut)
	{
		if(DecrementImageAlpha(cpd))
		{
			cpd->CurrentState = State_Read;
			ChangeTimerQueueTimer(cpd->TimerQueue, cpd->TimerHandle, 1000, 1000);
		}
	}
}

static void* channelpoints_thread(void* data)
{
	struct channelpoints_data* cpd = data;

	while(os_event_try(cpd->Event) == EAGAIN)
	{
		os_sleep_ms(1000);

		TryGetRedemption(data);

		while(cpd->CurrentRedemptionShown) //Complete all the queued up redemptions
		{
			FadeIn(data);
			os_sleep_ms((uint32_t)cpd->CurrentRedemptionShown->ShowDuration);
			FadeOut(data);
			
			TryGetRedemption(data);
		}
	}

	return NULL;
}

static const char* channelpoints_getname(void* unused)
{
	UNUSED_PARAMETER(unused);
	return "Channel Points Display";
}

static void channelpoints_get_defaults(obs_data_t* data)
{
	obs_data_set_default_int(data, "redemptions_count", 1);

	for(int i = 0 ; i < MAX_REDEMPTION_COUNT ; ++i)
	{
		char property_name[64] = { 0 };
		snprintf(property_name, 64, "media_exts%d", i);
		obs_data_set_default_string(data, property_name, ".jpg;.jpeg;.png;.gif");

		memset(property_name, 0, strlen(property_name));
		snprintf(property_name, 64, "media_fade_duration%d", i);
		obs_data_set_default_int(data, property_name, 700);

		memset(property_name, 0, strlen(property_name));
		snprintf(property_name, 64, "media_show_duration%d", i);
		obs_data_set_default_int(data, property_name, 5000);
	}
}

static void SetSourceImage(void* data, const char* filePath)
{
	struct channelpoints_data* cpd = data;

	struct dstr jsonString = { 0 };
	dstr_init_copy(&jsonString, "{ \"file\": \"");
	dstr_catf(&jsonString, "%s \"}", filePath);

	obs_data_t* settings = obs_data_create_from_json(jsonString.array);
	obs_source_update(cpd->ImageSource, settings);
	obs_data_release(settings);

	dstr_free(&jsonString);
}

static bool TryStartShowingRedemption(void* data, RedemptionData* Redemption)
{
	struct channelpoints_data* cpd = data;

	bool ShowingRedemption = false;

	struct dstr RandomFile = { 0 };
	bool RandomFileGet = GetRandomFile(Redemption->Directory.array, Redemption->MediaExtensions.array, &RandomFile);
	if(RandomFileGet && ImageSourceValid())
	{
		SetSourceImage(data, RandomFile.array);
		cpd->CurrentRedemptionShown = Redemption;
		ShowingRedemption = true;
	}
	dstr_free(&RandomFile);

	return ShowingRedemption;
}

static void UpdateChannelURL(void* data, const char* NewChanneName)
{
	struct channelpoints_data* cpd = data;

	if(NewChanneName != NULL && strlen(NewChanneName) > 0)
	{
		if(dstr_is_empty(&cpd->ChannelName) || dstr_cmp(&cpd->ChannelName, NewChanneName) != 0)
		{
			if(ChangeChannelURL(NewChanneName))
			{
				dstr_copy(&cpd->ChannelName, NewChanneName);
			}
		}
	}
}

static void WebViewInitialisedCallback()
{
	obs_data_t* Settings = obs_source_get_settings(CPD->MainSource);
	UpdateChannelURL(CPD, obs_data_get_string(Settings, "channel_name"));
	obs_data_release(Settings);
}

static void channelpoints_activate(void* data)
{
	//Kind of hacky way to add this source to the group,
	//but cant occur in channelpoints_create since sceneitem for it doesnt exist at that point

	struct channelpoints_data* cpd = data;

	if(!cpd->Initialised)
	{
		cpd->Initialised = true;

		obs_data_t* Settings = obs_source_get_settings(cpd->MainSource);

		bool Created = obs_data_get_bool(Settings, "cpd_created");

		obs_source_t* sceneSource = obs_frontend_get_current_scene();
		obs_scene_t* scene = obs_scene_from_source(sceneSource);

		cpd->CreatedSources = false;

		if(!Created)
		{
			cpd->MainItem = obs_scene_find_source(scene, obs_source_get_name(cpd->MainSource));

			//cpd->GroupSource = obs_source_create("group", "_cpd_group", NULL, NULL);
			//cpd->GroupItem = obs_scene_add(scene, cpd->GroupSource);

			cpd->ImageSource = obs_source_create("image_source", "_cpd_image", NULL, NULL);
			cpd->ImageItem = obs_scene_add(scene, cpd->ImageSource);

			cpd->AlphaFilter = obs_source_create("color_filter", "alpha_filter", NULL, NULL);
			obs_source_filter_add(cpd->ImageSource, cpd->AlphaFilter);

			//obs_sceneitem_group_add_item(cpd->groupItem, cpd->mainItem);
			//obs_sceneitem_group_add_item(cpd->GroupItem, cpd->ImageItem);

			obs_sceneitem_set_bounds_type(cpd->ImageItem, OBS_BOUNDS_SCALE_INNER);
			struct vec2 StartingBounds;
			StartingBounds.x = 100;
			StartingBounds.y = 100;
			obs_sceneitem_set_bounds(cpd->ImageItem, &StartingBounds);

			cpd->CreatedSources = true;
		}
		else
		{
			//cpd->GroupItem = obs_scene_find_source(scene, "_cpd_group");
			//cpd->GroupSource = obs_sceneitem_get_source(cpd->GroupItem);
			
			//cpd->ImageItem = obs_scene_find_source(obs_group_from_source(cpd->GroupSource), "_cpd_image");
			//cpd->ImageSource = obs_sceneitem_get_source(cpd->ImageItem);

			cpd->ImageItem = obs_scene_find_source(scene, "_cpd_image");
			cpd->ImageSource = obs_sceneitem_get_source(cpd->ImageItem);
			
			cpd->AlphaFilter = obs_source_get_filter_by_name(cpd->ImageSource, "alpha_filter");
		}

		obs_source_release(sceneSource);

		obs_data_set_bool(Settings, "cpd_created", true);

		obs_data_release(Settings);

		SetOpacity(data, 0);

		StartRedemptionReader();
	}
}

static void channelpoints_save(void* data, obs_data_t* settings)
{
	struct channelpoints_data* cpd = data;

	UpdateChannelURL(data, obs_data_get_string(settings, "channel_name"));

	cpd->RedemptionCount = (int)obs_data_get_int(settings, "redemptions_count");
	
	for(int i = (int)cpd->Redemptions.num ; i < cpd->RedemptionCount ; ++i)
	{
		darray_push_back_new(sizeof(RedemptionData), &cpd->Redemptions);
	}
	
	for(int i = 0 ; i < cpd->RedemptionCount ; ++i)
	{
		RedemptionData* Redemption = (RedemptionData*)darray_item(sizeof(RedemptionData), &cpd->Redemptions, i);

		char property_name[64] = { 0 };
		snprintf(property_name, 64, "redemption_title%d", i);
		char* Title = (char*)obs_data_get_string(settings, property_name);
		for(int i = 0 ; Title[i] ; ++i)
		{
			Title[i] = tolower(Title[i]);
		}
		dstr_copy(&Redemption->Title, Title);

		memset(property_name, 0, strlen(property_name));
		snprintf(property_name, 64, "media_dir%d", i);
		dstr_copy(&Redemption->Directory, obs_data_get_string(settings, property_name));

		memset(property_name, 0, strlen(property_name));
		snprintf(property_name, 64, "media_exts%d", i);
		char* Extentions = (char*)obs_data_get_string(settings, property_name);
		for(int i = 0 ; Extentions[i] ; ++i)
		{
			Extentions[i] = tolower(Extentions[i]);
		}
		dstr_copy(&Redemption->MediaExtensions, Extentions);

		memset(property_name, 0, strlen(property_name));
		snprintf(property_name, 64, "source_to_toggle%d", i);
		dstr_copy(&Redemption->ToggleSource, obs_data_get_string(settings, property_name));

		memset(property_name, 0, strlen(property_name));
		snprintf(property_name, 64, "source_to_activate%d", i);
		dstr_copy(&Redemption->ActivateSource, obs_data_get_string(settings, property_name));

		memset(property_name, 0, strlen(property_name));
		snprintf(property_name, 64, "source_to_deactivate%d", i);
		dstr_copy(&Redemption->DeactivateSource, obs_data_get_string(settings, property_name));

		memset(property_name, 0, strlen(property_name));
		snprintf(property_name, 64, "media_fade_duration%d", i);
		Redemption->FadeDuration = obs_data_get_int(settings, property_name);

		memset(property_name, 0, strlen(property_name));
		snprintf(property_name, 64, "media_show_duration%d", i);
		Redemption->ShowDuration = obs_data_get_int(settings, property_name);
	}
}

static obs_properties_t* channelpoints_properties(void* data)
{
	struct channelpoints_data* cpd = data;

	obs_properties_t* props = obs_properties_create();

	obs_properties_add_text(props,
		"channel_name",
		"Channel Name",
		OBS_TEXT_DEFAULT);

	obs_properties_add_int_slider(props,
		"redemptions_count",
		"Redemptions Count\n('OK' and reopen to refresh)",
		1,
		MAX_REDEMPTION_COUNT,
		1);

	for(int i = 0 ; i < cpd->RedemptionCount ; ++i)
	{
		char property_name[64] = { 0 };
		snprintf(property_name, 64, "redemption_title%d", i);
		char description[64] = { 0 };
		snprintf(description, 64, "Redemption Title (%d)", i);
		obs_properties_add_text(props,
			property_name,
			description,
			OBS_TEXT_DEFAULT);

		memset(property_name, 0, strlen(property_name));
		snprintf(property_name, 64, "media_dir%d", i);
		memset(description, 0, strlen(description));
		snprintf(description, 64, "Media Folder (%d)", i);
		obs_properties_add_path(props,
			property_name,
			description,
			OBS_PATH_DIRECTORY,
			"",
			NULL);

		memset(property_name, 0, strlen(property_name));
		snprintf(property_name, 64, "media_exts%d", i);
		memset(description, 0, strlen(description));
		snprintf(description, 64, "Media Extensions (%d)", i);
		obs_properties_add_text(props,
			property_name,
			description,
			OBS_TEXT_DEFAULT);

		memset(property_name, 0, strlen(property_name));
		snprintf(property_name, 64, "source_to_toggle%d", i);
		memset(description, 0, strlen(description));
		snprintf(description, 64, "Source to toggle visibility (%d)", i);
		obs_properties_add_text(props,
			property_name,
			description,
			OBS_TEXT_DEFAULT);

		memset(property_name, 0, strlen(property_name));
		snprintf(property_name, 64, "source_to_activate%d", i);
		memset(description, 0, strlen(description));
		snprintf(description, 64, "Source to activate visibility (%d)", i);
		obs_properties_add_text(props,
			property_name,
			description,
			OBS_TEXT_DEFAULT);

		memset(property_name, 0, strlen(property_name));
		snprintf(property_name, 64, "source_to_deactivate%d", i);
		memset(description, 0, strlen(description));
		snprintf(description, 64, "Source to deactivate visibility (%d)", i);
		obs_properties_add_text(props,
			property_name,
			description,
			OBS_TEXT_DEFAULT);

		memset(property_name, 0, strlen(property_name));
		snprintf(property_name, 64, "media_fade_duration%d", i);
		memset(description, 0, strlen(description));
		snprintf(description, 64, "Fade Duration (milliseconds) (%d)", i);
		obs_properties_add_int(props,
			property_name,
			description,
			100,
			60000,
			100);

		memset(property_name, 0, strlen(property_name));
		snprintf(property_name, 64, "media_show_duration%d", i);
		memset(description, 0, strlen(description));
		snprintf(description, 64, "Show Duration (milliseconds) (%d)", i);
		obs_properties_add_int(props,
			property_name,
			description,
			100,
			120000,
			100);
	}

	return props;
}

static void channelpoints_destroy(void* data)
{
	struct channelpoints_data* cpd = data;

	if(cpd)
	{
		cpd->Ticking = false;

		StopRedemptionReader();

		//DeleteTimerQueueEx(cpd->TimerQueue, NULL);

		obs_data_t* Settings = obs_source_get_settings(cpd->MainSource);
		obs_data_set_bool(Settings, "cpd_created", false);
		obs_data_release(Settings);

		//obs_sceneitem_group_remove_item(cpd->groupItem, cpd->mainItem);
		//obs_sceneitem_group_remove_item(cpd->groupItem, cpd->imageItem);

		cpd->CurrentRedemptionShown = NULL;

		if(cpd->InitialisedThread)
		{
			//void* ret;
			//os_event_signal(cpd->Event);
			//pthread_join(cpd->Thread, &ret);
		}
		
		obs_source_release(cpd->AlphaFilter);

		if(cpd->CreatedSources)
		{
			obs_source_release(cpd->ImageSource);
		}

		for(int i = 0 ; i < cpd->Redemptions.num ; ++i)
		{
			RedemptionData* Redemption = (RedemptionData*)darray_item(sizeof(RedemptionData), &cpd->Redemptions, i);
			dstr_free(&Redemption->Directory);
			dstr_free(&Redemption->MediaExtensions);
			dstr_free(&Redemption->Title);
			dstr_free(&Redemption->ToggleSource);
			dstr_free(&Redemption->ActivateSource);
			dstr_free(&Redemption->DeactivateSource);
		}

		darray_free(&cpd->Redemptions);

		dstr_free(&cpd->ChannelName);

		if(cpd->GroupSource && cpd->CreatedSources)
		{
			obs_source_release(cpd->GroupSource);
		}

		//os_event_destroy(cpd->Event);

		bfree(cpd);
		cpd = NULL;
	}
}

static void* channelpoints_create(obs_data_t* settings, obs_source_t* source)
{
	struct channelpoints_data* cpd = bzalloc(sizeof(struct channelpoints_data));
	CPD = cpd;
	cpd->Initialised = false;
	cpd->CurrentRedemptionShown = NULL;
	InitialisedCallback = WebViewInitialisedCallback;
	dstr_init(&cpd->ChannelName);
	channelpoints_save(cpd, settings); //Update data from settings
	cpd->MainSource = source;

	cpd->CurrentState = State_Read;

	cpd->TickThread = CreateThread(
		NULL,                   // default security attributes
		0,                      // use default stack size  
		Tick,					// thread function name
		cpd,					// argument to thread function 
		0,                      // use default creation flags 
		NULL);					// returns the thread identifier

	//cpd->TimerQueue = CreateTimerQueue();
	//CreateTimerQueueTimer(&cpd->TimerHandle, cpd->TimerQueue, (WAITORTIMERCALLBACK)UpdateMutationsTick, cpd, 1000, 1000, 0);

	//if(os_event_init(&cpd->Event, OS_EVENT_TYPE_MANUAL) != 0)
	//	goto fail;
	//if(pthread_create(&cpd->Thread, NULL, channelpoints_thread, cpd) != 0)
	//	goto fail;

	cpd->InitialisedThread = true;

	UNUSED_PARAMETER(settings);
	return cpd;

fail:
	channelpoints_destroy(cpd);
	return NULL;
}

struct obs_source_info channelpoints_info = {
	.id = "channel_points_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_CUSTOM_DRAW,
	.get_name = channelpoints_getname,
	.create = channelpoints_create,
	.destroy = channelpoints_destroy,
	.get_properties = channelpoints_properties,
	.activate = channelpoints_activate,
	.get_defaults = channelpoints_get_defaults,
	.save = channelpoints_save
};

OBS_DECLARE_MODULE()

bool obs_module_load(void)
{
	obs_register_source(&channelpoints_info);
	return true;
}