// compile with: /D_UNICODE /DUNICODE /DWIN32 /D_WINDOWS /c
#pragma once
#include <util/darray.h>
#include "redemption-data.hpp"

#ifdef __cplusplus
extern "C" {
#endif

extern void (*InitialisedCallback)();

int GetLatestRedemption(struct darray* Redemptions, int RedemptionCount);
void StartRedemptionReader();
void StopRedemptionReader();
bool ChangeChannelURL(const char* ChannelName);

#ifdef __cplusplus
}
#endif