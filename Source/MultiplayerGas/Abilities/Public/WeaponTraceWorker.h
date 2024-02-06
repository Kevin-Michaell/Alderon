// Copyright 2018-2020 Prime Time Ltd. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "GameplayTagContainer.h"
#include "PathOfTitans/PathOfTitans.h"
#include "ITypes.h"

#define TWO_FRAMES_AT_30FPS  0.066f
#define ONE_FRAMES_AT_60FPS  0.016f
#define ONE_FRAMES_AT_120FPS  0.008f

class IBaseCharacter;

/**
 * 
 */
class PATHOFTITANS_API FWeaponTraceWorker : public FRunnable
{
public:

	TQueue<FQueuedTraceSet, EQueueMode::Spsc> WorkQueue;
	
	//Thread safe counter 
	FThreadSafeCounter StopTaskCounter;

public:
	static FWeaponTraceWorker* Get();


	FWeaponTraceWorker();
	virtual ~FWeaponTraceWorker();

	//FRunnable interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;

	void ShutDown();

	void QueueTraces(const FQueuedTraceSet& QueueSet);

private:
	//Thread to run the FRunnable on
	FRunnableThread* Thread;

	// Singleton instance
	static FWeaponTraceWorker* Instance;

private:
	void ProcessTraceSet(FQueuedTraceSet CurrentTraceSet);
	
	UFUNCTION()
	void PropagateTraceResults(FQueuedTraceSet TraceSet);
};
