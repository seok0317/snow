// Copyright Epic Games, Inc. All Rights Reserved.

#include "SnowDeformationSubsystem.h"
#include "SnowDeformationVolume.h"
#include "Engine/World.h"

USnowDeformationSubsystem::USnowDeformationSubsystem()
{
}

bool USnowDeformationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (IsRunningDedicatedServer())
	{
		return false;
	}

	UWorld* World = Cast<UWorld>(Outer);
	if (!World || !World->IsGameWorld())
	{
		return false;
	}

	return true;
}

void USnowDeformationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void USnowDeformationSubsystem::Deinitialize()
{
	ActiveVolumes.Empty();
	Super::Deinitialize();
}

void USnowDeformationSubsystem::RegisterVolume(ASnowDeformationVolume* Volume)
{
	if (Volume && !ActiveVolumes.Contains(Volume))
	{
		ActiveVolumes.Add(Volume);
	}
}

void USnowDeformationSubsystem::UnregisterVolume(ASnowDeformationVolume* Volume)
{
	if (Volume)
	{
		ActiveVolumes.Remove(Volume);
	}
}

void USnowDeformationSubsystem::RegisterDeformationPayload(const FSnowDeformationPayload& InPayload)
{
	for (ASnowDeformationVolume* Volume : ActiveVolumes)
	{
		if (Volume && Volume->IsInsideSnowArea(InPayload.HitLocationWorld))
		{
			// 볼륨 내부이면, 해당 볼륨에게 페이로드 전달 (UV 변환은 볼륨 내부에서 수행)
			Volume->RegisterDeformationPayload(InPayload);
			// 하나의 도장이 여러 겹치는 볼륨에 다 찍혀야 할 수도 있으므로 break 하지 않음
		}
	}
}

bool USnowDeformationSubsystem::GetSnowSurfaceZAtLocation(const FVector& WorldLocation, float& OutSurfaceZ, float& OutThickness) const
{
	for (ASnowDeformationVolume* Volume : ActiveVolumes)
	{
		if (Volume && Volume->IsInsideSnowArea(WorldLocation))
		{
			OutSurfaceZ = Volume->GetSnowSurfaceZ();
			OutThickness = Volume->GetSnowThickness();
			return true; // 가장 먼저 찾은 볼륨의 높이를 반환
		}
	}
	return false;
}

void USnowDeformationSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 각 볼륨별로 모인 발자국들을 일괄 렌더링(도장 찍기)하도록 지시
	for (ASnowDeformationVolume* Volume : ActiveVolumes)
	{
		if (Volume)
		{
			Volume->ProcessDeformationBatch(DeltaTime);
		}
	}
}

TStatId USnowDeformationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USnowDeformationSubsystem, STATGROUP_Tickables);
}
