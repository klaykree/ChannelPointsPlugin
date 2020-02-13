#pragma once
#include <util/dstr.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RedemptionData
{
	struct dstr Title;
	struct dstr Directory;
	struct dstr MediaExtensions;
	long long FadeDuration;
	long long ShowDuration;
} RedemptionData;

#ifdef __cplusplus
}
#endif