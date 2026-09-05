// Copyright Epic Games, Inc. All Rights Reserved.

#include "SnowDeformationComponent.h"
#include "SnowDeformationSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

USnowDeformationComponent::USnowDeformationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	bDrawDebug = true;

	FSnowTrackedSocket LeftFoot;
	LeftFoot.SocketName = TEXT("foot_l");
	LeftFoot.RadiusWorld = 150.0f;
	LeftFoot.DepthMultiplier = 0.7f;
	TrackedSockets.Add(LeftFoot);

	FSnowTrackedSocket RightFoot;
	RightFoot.SocketName = TEXT("foot_r");
	RightFoot.RadiusWorld = 150.0f;
	RightFoot.DepthMultiplier = 0.7f;
	TrackedSockets.Add(RightFoot);

	FSnowTrackedSocket LeftCalf;
	LeftCalf.SocketName = TEXT("calf_l");
	LeftCalf.RadiusWorld = 120.0f;
	LeftCalf.DepthMultiplier = 0.4f;
	TrackedSockets.Add(LeftCalf);

	FSnowTrackedSocket RightCalf;
	RightCalf.SocketName = TEXT("calf_r");
	RightCalf.RadiusWorld = 120.0f;
	RightCalf.DepthMultiplier = 0.4f;
	TrackedSockets.Add(RightCalf);

	bDrawDebug = false;
	MinStrokeDistance = 3.0f;
}

void USnowDeformationComponent::BeginPlay()
{
	Super::BeginPlay();

	if (IsRunningDedicatedServer())
	{
		SetComponentTickEnabled(false);
		return;
	}

	CacheSkeletalMesh();

	if (UWorld* World = GetWorld())
	{
		CachedSubsystem = World->GetSubsystem<USnowDeformationSubsystem>();
	}

	ResetTrackedSockets();
}

void USnowDeformationComponent::CacheSkeletalMesh()
{
	if (AActor* OwnerActor = GetOwner())
	{
		if (ACharacter* Character = Cast<ACharacter>(OwnerActor))
		{
			CachedMeshComp = Character->GetMesh();
		}
		else
		{
			CachedMeshComp = OwnerActor->FindComponentByClass<USkeletalMeshComponent>();
		}
	}
}

void USnowDeformationComponent::ResetTrackedSockets()
{
	for (FSnowTrackedSocket& Socket : TrackedSockets)
	{
		Socket.bHasValidPrevLocation = false;
		Socket.PreviousLocation = FVector::ZeroVector;
	}
}

bool USnowDeformationComponent::IsWithinCullDistance() const
{
	UWorld* World = GetWorld();
	if (!World) return false;

	APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(World, 0);
	if (!CameraManager) return true;

	const FVector CameraLoc = CameraManager->GetCameraLocation();
	const FVector ActorLoc = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;

	const float DistSq = FVector::DistSquared(CameraLoc, ActorLoc);
	return DistSq <= FMath::Square(MaxCameraDistance);
}

void USnowDeformationComponent::ProcessContinuousSweeps(float DeltaTime)
{
	if (!CachedMeshComp || !IsWithinCullDistance()) return;

	if (!CachedSubsystem)
	{
		if (UWorld* World = GetWorld())
		{
			CachedSubsystem = World->GetSubsystem<USnowDeformationSubsystem>();
		}
	}

	if (!CachedSubsystem) return;

	for (FSnowTrackedSocket& Socket : TrackedSockets)
	{
		if (!CachedMeshComp->DoesSocketExist(Socket.SocketName)) continue;

		const FVector CurrentLoc = CachedMeshComp->GetSocketLocation(Socket.SocketName);

		if (!Socket.bHasValidPrevLocation)
		{
			Socket.PreviousLocation = CurrentLoc;
			Socket.bHasValidPrevLocation = true;
			continue;
		}

		const FVector PrevLoc = Socket.PreviousLocation;
		const float MoveDist = FVector::Dist(PrevLoc, CurrentLoc);

		if (MoveDist > MaxTeleportDistance)
		{
			Socket.PreviousLocation = CurrentLoc;
			continue;
		}

		if (MoveDist < MinStrokeDistance) continue;

		const float StepSize = FMath::Max(8.0f, Socket.RadiusWorld * 0.25f);
		const int32 NumSubSteps = FMath::Clamp(FMath::CeilToInt(MoveDist / StepSize), 1, 6);
		const FVector Velocity = (DeltaTime > 0.0f) ? ((CurrentLoc - PrevLoc) / DeltaTime) : FVector::ZeroVector;

		for (int32 StepIdx = 0; StepIdx < NumSubSteps; ++StepIdx)
		{
			const float Alpha = (float)(StepIdx + 1) / (float)NumSubSteps;
			const FVector SamplePos = FMath::Lerp(PrevLoc, CurrentLoc, Alpha);

			// 서브시스템에 해당 위치의 눈 표면 높이 요청
			float SnowSurfaceZ = 0.0f;
			float SnowThickness = 1.0f;
			if (CachedSubsystem->GetSnowSurfaceZAtLocation(SamplePos, SnowSurfaceZ, SnowThickness))
			{
				const bool bIsFoot = Socket.SocketName.ToString().Contains(TEXT("foot"));
				const float ContactZ = bIsFoot ? (SamplePos.Z - 10.0f) : SamplePos.Z;
				const float SubmergedDepth = SnowSurfaceZ - ContactZ;
				
				if (SubmergedDepth <= 0.0f) continue;

				float SpeedMultiplier = 1.0f;
				if (Velocity.SizeSquared2D() > 28000.0f)
				{
					SpeedMultiplier = 2.5f;
				}

				float BaseRatio = FMath::Clamp(SubmergedDepth / FMath::Max(1.0f, SnowThickness), 0.0f, 1.0f);
				if (SpeedMultiplier > 1.0f)
				{
					BaseRatio = FMath::Max(BaseRatio, 0.8f);
				}

				const float DepthRatio = BaseRatio * SpeedMultiplier * Socket.DepthMultiplier;
				if (DepthRatio <= 0.01f) continue;

				float FootAngle = GetOwner() ? GetOwner()->GetActorRotation().Yaw : 0.0f;
				if (Velocity.SizeSquared2D() > 100.0f)
				{
					FootAngle = FMath::RadiansToDegrees(FMath::Atan2(Velocity.Y, Velocity.X));
				}

				FSnowDeformationPayload Payload;
				Payload.HitLocationWorld = SamplePos;
				Payload.RadiusWorld = Socket.RadiusWorld;
				Payload.StrokeLength = 0.0f;
				Payload.RotationAngle = FootAngle;
				Payload.DepthIntensity = DepthRatio;
				Payload.Velocity = Velocity;
				Payload.bIsContinuousStroke = false;

				CachedSubsystem->RegisterDeformationPayload(Payload);
			}
		}
		Socket.PreviousLocation = CurrentLoc;
	}
}

void USnowDeformationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ProcessContinuousSweeps(DeltaTime);
}

void USnowDeformationComponent::TriggerFootstep(FName SocketName)
{
	if (!IsWithinCullDistance() || !CachedMeshComp) return;

	if (!CachedSubsystem)
	{
		if (UWorld* World = GetWorld()) CachedSubsystem = World->GetSubsystem<USnowDeformationSubsystem>();
	}

	if (!CachedSubsystem) return;
	if (!CachedMeshComp->DoesSocketExist(SocketName)) return;

	const FVector SocketLoc = CachedMeshComp->GetSocketLocation(SocketName);

	float SnowSurfaceZ = 0.0f;
	float SnowThickness = 1.0f;
	if (CachedSubsystem->GetSnowSurfaceZAtLocation(SocketLoc, SnowSurfaceZ, SnowThickness))
	{
		FSnowDeformationPayload Payload;
		Payload.HitLocationWorld = SocketLoc;
		Payload.RadiusWorld = 10.0f; // 기본 반경 설정
		Payload.StrokeLength = 0.0f;
		Payload.RotationAngle = GetOwner() ? GetOwner()->GetActorRotation().Yaw : 0.0f;
		Payload.DepthIntensity = 1.0f;
		Payload.Velocity = GetOwner() ? GetOwner()->GetVelocity() : FVector::ZeroVector;
		Payload.bIsContinuousStroke = false;

		CachedSubsystem->RegisterDeformationPayload(Payload);
	}
}

void USnowDeformationComponent::ApplyDeformationAtLocation(const FVector& WorldLocation, float InRadiusWorld, float InIntensity, const FVector& InVelocity)
{
	if (!CachedSubsystem)
	{
		if (UWorld* World = GetWorld()) CachedSubsystem = World->GetSubsystem<USnowDeformationSubsystem>();
	}

	if (!CachedSubsystem) return;

	float SnowSurfaceZ = 0.0f;
	float SnowThickness = 1.0f;
	if (CachedSubsystem->GetSnowSurfaceZAtLocation(WorldLocation, SnowSurfaceZ, SnowThickness))
	{
		FSnowDeformationPayload Payload;
		Payload.HitLocationWorld = WorldLocation;
		Payload.RadiusWorld = InRadiusWorld;
		Payload.StrokeLength = 0.0f;
		Payload.RotationAngle = 0.0f;
		Payload.DepthIntensity = InIntensity;
		Payload.Velocity = InVelocity;
		Payload.bIsContinuousStroke = false;

		CachedSubsystem->RegisterDeformationPayload(Payload);
	}
}
