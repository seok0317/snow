// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SnowDeformationTypes.h"
#include "SnowDeformationComponent.generated.h"

class USnowDeformationSubsystem;
class USkeletalMeshComponent;

/**
 * [이식 가이드]
 * USnowDeformationComponent:
 * 캐릭터(플레이어, 원격 플레이어, 몬스터, 사체 등)에 부착되는 연속 궤적(Continuous Sweep) 스노우 디포메이션 컴포넌트입니다.
 * 
 * [핵심 기능]
 * 1. 발목뿐 아니라 종아리(calf), 엉덩이(pelvis) 등 여러 부위의 3D 이동 궤적을 실시간 추적
 * 2. 캡슐/구체 스윕(Capsule Sweep)으로 눈 지형에 파묻힌 깊이와 면적을 정밀 계산
 * 3. 이동 방향으로 길게 늘어난 캡슐 도장(Stretched Capsule Stroke) 페이로드를 생성하여 렌더링 비용을 획기적으로 절감 (1 Stroke = 1 Draw)
 * 4. 로컬 카메라 기준 거리 컬링(MaxCameraDistance)으로 멀리 있는 캐릭터 연산 자동 스킵
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SNOW_DEFORMATION_API USnowDeformationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USnowDeformationComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ----------------------------------------------------------------------
	// 수동 트리거 API (노티파이 및 스킬 연동)
	// ----------------------------------------------------------------------

	/**
	 * 애니메이션 노티파이(AnimNotify)에서 단일 발자국을 찍을 때 호출
	 */
	UFUNCTION(BlueprintCallable, Category = "Snow Deformation")
	void TriggerFootstep(FName SocketName);

	/**
	 * 임의의 위치에 직접 눈 굴곡을 찍는 함수 (착지 충격파, 폭발, 주먹 내려치기 등)
	 */
	UFUNCTION(BlueprintCallable, Category = "Snow Deformation")
	void ApplyDeformationAtLocation(const FVector& WorldLocation, float InRadiusWorld = 30.0f, float InIntensity = 1.0f, const FVector& InVelocity = FVector::ZeroVector);

	/**
	 * 텔레포트나 리스폰 직후 이전 궤적 위치를 초기화하여 허공에 긴 궤적이 생기는 것을 방지
	 */
	UFUNCTION(BlueprintCallable, Category = "Snow Deformation")
	void ResetTrackedSockets();

protected:
	/** 매 틱마다 등록된 소켓들의 궤적(P1 -> P2)을 검사하여 눈을 가르고 지나갔는지 스윕 연산 */
	void ProcessContinuousSweeps(float DeltaTime);

	/** 로컬 카메라와 캐릭터 간의 거리를 검사하여 컬링할지 결정 */
	bool IsWithinCullDistance() const;

	/** 캐릭터의 스켈레탈 메시 컴포넌트 캐싱 */
	void CacheSkeletalMesh();

public:
	// ----------------------------------------------------------------------
	// 설정 프로퍼티
	// ----------------------------------------------------------------------

	/**
	 * 눈과 상호작용하여 궤적을 남길 소켓 목록 (발목, 종아리, 엉덩이 등 자유롭게 추가 가능)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation|Tracking")
	TArray<FSnowTrackedSocket> TrackedSockets;

	/** 라인트레이스 / 스윕 충돌 채널 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation|Trace")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	/** 최소 궤적 길이 (cm 단위, 이보다 작은 미세 떨림은 누적 후 처리) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation|Tracking")
	float MinStrokeDistance = 2.0f;

	/** 순간이동/리스폰 감지 최대 거리 (cm 단위, 이 거리 이상 한 번에 튀면 궤적 무시) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation|Tracking")
	float MaxTeleportDistance = 150.0f;

	/**
	 * 로컬 카메라 기준 최대 연산 거리 (단위: cm, 4000 = 40m)
	 * 이 거리보다 멀리 있는 캐릭터는 스윕 연산을 스킵합니다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation|Optimization")
	float MaxCameraDistance = 4000.0f;

	/** 에디터 뷰포트에서 스윕 궤적 디버그 시각화 여부 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation|Debug")
	bool bDrawDebug = false;

private:
	/** 소유 액터의 스켈레탈 메시 컴포넌트 */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> CachedMeshComp;

	/** 월드 서브시스템 캐시 */
	UPROPERTY(Transient)
	TObjectPtr<USnowDeformationSubsystem> CachedSubsystem;
};
