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

	// 기본 추적 부위 등록 (실제 사람 발 크기인 반경 25cm, 종아리 20cm로 현실적 튜닝)
	FSnowTrackedSocket LeftFoot;
	LeftFoot.SocketName = TEXT("foot_l");
	LeftFoot.RadiusWorld = 28.0f; // 지름 약 56cm (발 + 밀려나는 눈 둑 포함 크기)
	LeftFoot.DepthMultiplier = 0.7f;
	TrackedSockets.Add(LeftFoot);

	FSnowTrackedSocket RightFoot;
	RightFoot.SocketName = TEXT("foot_r");
	RightFoot.RadiusWorld = 28.0f;
	RightFoot.DepthMultiplier = 0.7f;
	TrackedSockets.Add(RightFoot);

	FSnowTrackedSocket LeftCalf;
	LeftCalf.SocketName = TEXT("calf_l");
	LeftCalf.RadiusWorld = 22.0f;
	LeftCalf.DepthMultiplier = 0.4f;
	TrackedSockets.Add(LeftCalf);

	FSnowTrackedSocket RightCalf;
	RightCalf.SocketName = TEXT("calf_r");
	RightCalf.RadiusWorld = 22.0f;
	RightCalf.DepthMultiplier = 0.4f;
	TrackedSockets.Add(RightCalf);

	bDrawDebug = false;
	MinStrokeDistance = 3.0f;
}

void USnowDeformationComponent::BeginPlay()
{
	Super::BeginPlay();

	// 전용 서버(Dedicated Server)에서는 렌더링/스윕 연산을 실행하지 않음
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

	UE_LOG(LogTemp, Warning, TEXT("[SnowComp::BeginPlay] Owner: %s | MeshComp: %s | Sockets Count: %d | Subsystem: %s"),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(CachedMeshComp),
		TrackedSockets.Num(),
		*GetNameSafe(CachedSubsystem));
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
	if (!World)
	{
		return false;
	}

	APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(World, 0);
	if (!CameraManager)
	{
		return true;
	}

	const FVector CameraLoc = CameraManager->GetCameraLocation();
	const FVector ActorLoc = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;

	const float DistSq = FVector::DistSquared(CameraLoc, ActorLoc);
	return DistSq <= FMath::Square(MaxCameraDistance);
}

void USnowDeformationComponent::ProcessContinuousSweeps(float DeltaTime)
{
	if (!CachedMeshComp || !IsWithinCullDistance())
	{
		return;
	}

	if (!CachedSubsystem)
	{
		if (UWorld* World = GetWorld())
		{
			CachedSubsystem = World->GetSubsystem<USnowDeformationSubsystem>();
		}
	}

	if (!CachedSubsystem)
	{
		return;
	}

	const float SnowSurfaceZ = CachedSubsystem->GetSnowSurfaceZ();
	const float SnowBaseZ = CachedSubsystem->GetSnowBaseZ();
	const float SnowThickness = FMath::Max(1.0f, CachedSubsystem->GetSnowThickness());
	const FVector2D AreaExtent = CachedSubsystem->GetSnowAreaExtent();

	if (AreaExtent.X <= 0.0f || AreaExtent.Y <= 0.0f)
	{
		return;
	}

	// 등록된 모든 소켓(발, 종아리, 엉덩이 등)의 이동 궤적을 순회
	for (FSnowTrackedSocket& Socket : TrackedSockets)
	{
		if (!CachedMeshComp->DoesSocketExist(Socket.SocketName))
		{
			continue;
		}

		const FVector CurrentLoc = CachedMeshComp->GetSocketLocation(Socket.SocketName);

		// 첫 프레임이거나 리셋 직후면 현재 위치만 캐싱하고 다음 프레임부터 계산
		if (!Socket.bHasValidPrevLocation)
		{
			Socket.PreviousLocation = CurrentLoc;
			Socket.bHasValidPrevLocation = true;
			continue;
		}

		const FVector PrevLoc = Socket.PreviousLocation;
		const float MoveDist = FVector::Dist(PrevLoc, CurrentLoc);

		// 순간이동/리스폰 감지 시 궤적 리셋
		if (MoveDist > MaxTeleportDistance)
		{
			Socket.PreviousLocation = CurrentLoc;
			continue;
		}

		// 최소 이동 거리보다 작으면 $P_1$을 갱신하지 않고 누적
		if (MoveDist < MinStrokeDistance)
		{
			continue;
		}

		// 눈 볼륨 영역 내에서 이동 궤적을 촘촘한 서브스텝(15~20cm 간격)으로 보간하여 완벽히 연속적인 고랑 생성
		const float StepSize = FMath::Max(15.0f, Socket.RadiusWorld * 0.35f);
		const int32 NumSubSteps = FMath::Clamp(FMath::CeilToInt(MoveDist / StepSize), 1, 6);
		const float TotalVolumeWidth = AreaExtent.X * 2.0f;
		const float RadiusUV = (TotalVolumeWidth > 0.0f) ? (Socket.RadiusWorld / TotalVolumeWidth) : 0.015f;
		const FVector Velocity = (DeltaTime > 0.0f) ? ((CurrentLoc - PrevLoc) / DeltaTime) : FVector::ZeroVector;

		for (int32 StepIdx = 0; StepIdx < NumSubSteps; ++StepIdx)
		{
			const float Alpha = (float)(StepIdx + 1) / (float)NumSubSteps;
			const FVector SamplePos = FMath::Lerp(PrevLoc, CurrentLoc, Alpha);

			FVector2D HitUV;
			if (CachedSubsystem->WorldLocationToUV(SamplePos, HitUV))
			{
				// 발바닥 접지 높이 정밀 계산 (foot 본은 발목에 위치하므로 -10cm 오프셋 적용)
				const bool bIsFoot = Socket.SocketName.ToString().Contains(TEXT("foot"));
				const float ContactZ = bIsFoot ? (SamplePos.Z - 10.0f) : SamplePos.Z;
				const float SubmergedDepth = SnowSurfaceZ - ContactZ;
				
				// 발바닥이 눈 표면보다 위에 떠 있으면(점프/체공 중) 절대 파지 않음
				if (SubmergedDepth <= 0.0f)
				{
					continue;
				}

				// 실제 침투 깊이 비율 계산 (0.0 ~ 1.0)
				const float DepthRatio = FMath::Clamp(SubmergedDepth / SnowThickness, 0.0f, 1.0f) * Socket.DepthMultiplier;
				if (DepthRatio <= 0.01f)
				{
					continue;
				}

				// 발의 이동 방향 또는 캐릭터 시선 방향으로 회전 각도 정렬 (도 단위)
				float FootAngle = GetOwner() ? GetOwner()->GetActorRotation().Yaw : 0.0f;
				if (Velocity.SizeSquared2D() > 100.0f)
				{
					FootAngle = FMath::RadiansToDegrees(FMath::Atan2(Velocity.Y, Velocity.X));
				}

				FSnowDeformationPayload Payload;
				Payload.HitUV = HitUV;
				Payload.Radius = RadiusUV;
				Payload.StrokeLength = 0.0f;
				Payload.RotationAngle = FootAngle;
				Payload.DepthIntensity = DepthRatio;
				Payload.Velocity = Velocity;
				Payload.bIsContinuousStroke = false;

				CachedSubsystem->RegisterDeformationPayload(Payload);
			}
		}

		// $P_1 \leftarrow P_2$ 갱신
		Socket.PreviousLocation = CurrentLoc;
	}
}

void USnowDeformationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 매 틱마다 종아리/발목의 연속 궤적 스윕 연산 수행
	ProcessContinuousSweeps(DeltaTime);
}

void USnowDeformationComponent::TriggerFootstep(FName SocketName)
{
	if (!IsWithinCullDistance() || !CachedMeshComp)
	{
		return;
	}

	if (!CachedSubsystem)
	{
		if (UWorld* World = GetWorld())
		{
			CachedSubsystem = World->GetSubsystem<USnowDeformationSubsystem>();
		}
	}

	if (!CachedSubsystem)
	{
		return;
	}

	if (!CachedMeshComp->DoesSocketExist(SocketName))
	{
		return;
	}

	const FVector SocketLoc = CachedMeshComp->GetSocketLocation(SocketName);

	FVector2D HitUV;
	if (CachedSubsystem->WorldLocationToUV(SocketLoc, HitUV))
	{
		const FVector2D AreaExtent = CachedSubsystem->GetSnowAreaExtent();

		FSnowDeformationPayload Payload;
		Payload.HitUV = HitUV;
		Payload.Radius = (AreaExtent.X > 0.0f) ? (10.0f / AreaExtent.X) : 0.015f;
		Payload.StrokeLength = 0.0f; // 단일 점 도장
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
		if (UWorld* World = GetWorld())
		{
			CachedSubsystem = World->GetSubsystem<USnowDeformationSubsystem>();
		}
	}

	if (!CachedSubsystem)
	{
		return;
	}

	FVector2D HitUV;
	if (CachedSubsystem->WorldLocationToUV(WorldLocation, HitUV))
	{
		const FVector2D AreaExtent = CachedSubsystem->GetSnowAreaExtent();

		FSnowDeformationPayload Payload;
		Payload.HitUV = HitUV;
		Payload.Radius = (AreaExtent.X > 0.0f) ? (InRadiusWorld / AreaExtent.X) : 0.02f;
		Payload.StrokeLength = 0.0f;
		Payload.RotationAngle = 0.0f;
		Payload.DepthIntensity = InIntensity;
		Payload.Velocity = InVelocity;
		Payload.bIsContinuousStroke = false;

		CachedSubsystem->RegisterDeformationPayload(Payload);
	}
}
