// Copyright 2018-2020 Prime Time Ltd. All rights reserved.

#include "Abilities/WeaponTraceWorker.h"
//#include "EngineMinimal.h"
#include "HAL/RunnableThread.h"
#include "Stats/IStats.h"
#include "Player/IBaseCharacter.h"

#define HIT_BUFFER_SIZE 64
static_assert(HIT_BUFFER_SIZE > 0, "Invalid hit buffer size.");

//#include "Engine/World.h"
//#include "PhysicsFiltering.h"


FWeaponTraceWorker* FWeaponTraceWorker::Instance = nullptr;



FWeaponTraceWorker* FWeaponTraceWorker::Get()
{
	if (Instance == nullptr)
	{
		Instance = new FWeaponTraceWorker();
	}
	return Instance;
}

FWeaponTraceWorker::FWeaponTraceWorker()
{
	Thread = FRunnableThread::Create(this, TEXT("FWeaponTraceWorker"), 0U, TPri_Highest);
}

FWeaponTraceWorker::~FWeaponTraceWorker()
{
	ShutDown();
}

bool FWeaponTraceWorker::Init()
{
	return true;
}

uint32 FWeaponTraceWorker::Run()
{
	while (StopTaskCounter.GetValue() == 0)
	{
		
		FQueuedTraceSet CurrentTraceSet;
		if (WorkQueue.Dequeue(CurrentTraceSet))
		{
			ProcessTraceSet(CurrentTraceSet);
		}

		FPlatformProcess::ConditionalSleep([&]()
		{
			bool bQueueEmpty = WorkQueue.IsEmpty();
			bool bIsStopping = StopTaskCounter.GetValue() > 0;
			return !bQueueEmpty || bIsStopping;
		}, ONE_FRAMES_AT_120FPS);
	}

	return 0;
}

void FWeaponTraceWorker::Stop()
{
	StopTaskCounter.Increment();
}

void FWeaponTraceWorker::ShutDown()
{
	Stop();
	Thread->WaitForCompletion();
}

void FWeaponTraceWorker::QueueTraces(const FQueuedTraceSet& QueueSet)
{
	if (!QueueSet.CharacterPtr.IsValid())
	{
		return;
	}

	WorkQueue.Enqueue(QueueSet);

	//ProcessTraceSet(QueueSet);

}

void FWeaponTraceWorker::ProcessTraceSet(FQueuedTraceSet CurrentTraceSet)
{
	if (!CurrentTraceSet.CharacterPtr.IsValid())
	{
		return;
	}

	UWorld* World = CurrentTraceSet.WeaponWorld;

	if (!IsValid(World))
	{
		return;
	}

	bool bHit = false;
	
	for (FQueuedTraceItem& TraceItem : CurrentTraceSet.Items)
	{
		if (IsValid(World))
		{
			bHit |= World->SweepMultiByChannel(TraceItem.OutResults,
				TraceItem.Start,
				TraceItem.End,
				TraceItem.Rotation,
				TraceItem.CollisionChannel,
				TraceItem.Shape,
				TraceItem.QueryParams);

			//In this case bHit will only be true if there is a BLOCKING hit (and we want overlap too)
			bHit |= TraceItem.OutResults.Num() > 0;

			//TArray<AActor*> Items;
			//TArray<FHitResult> Items2;
			//UKismetSystemLibrary::BoxTraceMulti(World, TraceItem.Start, TraceItem.End, TraceItem.Shape.GetBox(), TraceItem.Rotation.Rotator(), TraceTypeQuery1, false, Items, EDrawDebugTrace::ForDuration, Items2, true, FLinearColor::Green, FLinearColor::Red, 1.0f);
		}
	}

	if (bHit)
	{

		FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(

			FSimpleDelegateGraphTask::FDelegate::CreateRaw(this, &FWeaponTraceWorker::PropagateTraceResults, CurrentTraceSet),
			GET_STATID(STAT_PerformDamageSweeps), NULL, ENamedThreads::GameThread
		);
	}
}

void FWeaponTraceWorker::PropagateTraceResults(FQueuedTraceSet TraceSet)
{
	if (!TraceSet.CharacterPtr.IsValid())
	{
		return;
	}

	TraceSet.CharacterPtr->ProcessCompletedTraceSet(TraceSet);
}

