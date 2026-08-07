// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SnowDeformationTypes.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SnowDeformationSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSnowRenderTargetUpdated, UTextureRenderTarget2D*, ActiveRenderTarget);

/**
 * [이식 가이드]
 * USnowDeformationSubsystem:
 * Step 3: 스마트 브러시 FCanvas 도장 찍기 및 핑퐁 버퍼 자동 풍화(Erosion) 매니저
 * 
 * [핵심 파이프라인]
 * 1. Dedicated Server 차단 (리슨 서버 호스트 / 클라이언트 전용)
 * 2. 풍화(Erosion): 매 틱 M_SnowErosion 머티리얼로 이전 RT의 깊이값을 서서히 감소시켜 평평하게 복원
 * 3. 스마트 브러시 도장: 수집된 모든 캡슐 스트로크를 단 1회의 FCanvas 배치로 Write RT에 일괄 드로우
 * 4. 핑퐁 스왑(Swap): 렌더링 완료 후 Read/Write 버퍼를 스왑하여 지형 머티리얼에 실시간 공급
 */
UCLASS()
class SNOW_DEFORMATION_API USnowDeformationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	USnowDeformationSubsystem();

	// ----------------------------------------------------------------------
	// UTickableWorldSubsystem 라이프사이클 오버라이드
	// ----------------------------------------------------------------------

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// ----------------------------------------------------------------------
	// 스노우 디포메이션 API (Blueprint & C++ 공용)
	// ----------------------------------------------------------------------

	/** 캐릭터 컴포넌트에서 호출: 프레임 동안 수집된 페이로드를 큐에 등록 */
	UFUNCTION(BlueprintCallable, Category = "Snow Deformation")
	void RegisterDeformationPayload(const FSnowDeformationPayload& InPayload);

	/** 3D 월드 좌표 -> 로컬 UV(0.0 ~ 1.0) 변환 (45도 경사로 왜곡 방지) */
	UFUNCTION(BlueprintPure, Category = "Snow Deformation")
	bool WorldLocationToUV(const FVector& WorldLocation, FVector2D& OutUV) const;

	/** 2D UV 좌표 -> 3D 월드 좌표 역변환 */
	UFUNCTION(BlueprintPure, Category = "Snow Deformation")
	FVector UVToWorldLocation(const FVector2D& InUV) const;

	/** 특정 위치가 눈 영역 내부에 속하는지 검사 */
	UFUNCTION(BlueprintPure, Category = "Snow Deformation")
	bool IsInsideSnowArea(const FVector& WorldLocation) const;

	/** 볼륨 액터로부터 FTransform 및 바운드 동기화 */
	UFUNCTION(BlueprintCallable, Category = "Snow Deformation")
	void SetSnowVolumeTransform(const FTransform& InTransform, const FVector& InScaledExtent, float InSnowThickness);

	/** 브러시 머티리얼 템플릿 동적 설정 */
	UFUNCTION(BlueprintCallable, Category = "Snow Deformation")
	void SetBrushMaterialTemplate(UMaterialInterface* InMaterial);

	/** 풍화 머티리얼 템플릿 동적 설정 */
	UFUNCTION(BlueprintCallable, Category = "Snow Deformation")
	void SetErosionMaterialTemplate(UMaterialInterface* InMaterial);

	/** 눈 기본 두께(cm) 설정 */
	UFUNCTION(BlueprintCallable, Category = "Snow Deformation")
	void SetSnowThickness(float InThickness) { SnowThickness = FMath::Max(0.0f, InThickness); }

	/** 눈 표면 최고 높이 (Z 월드 좌표, 바닥 기준 + SnowThickness) */
	UFUNCTION(BlueprintPure, Category = "Snow Deformation")
	float GetSnowSurfaceZ() const { return SnowVolumeTransform.GetLocation().Z - SnowVolumeExtent.Z + SnowThickness; }

	/** 눈 바닥 높이 (Z 월드 좌표) */
	UFUNCTION(BlueprintPure, Category = "Snow Deformation")
	float GetSnowBaseZ() const { return SnowVolumeTransform.GetLocation().Z - SnowVolumeExtent.Z; }

	/** 눈 두께 반환 (cm) */
	UFUNCTION(BlueprintPure, Category = "Snow Deformation")
	float GetSnowThickness() const { return SnowThickness; }

	/** 현재 읽기용 활성 렌더 타깃 반환 (지형 머티리얼 셰이더 공급용) */
	UFUNCTION(BlueprintPure, Category = "Snow Deformation")
	UTextureRenderTarget2D* GetCurrentReadRenderTarget() const { return bIsRTAPrimary ? SnowHeightRT_A : SnowHeightRT_B; }

	/** 현재 쓰기용 렌더 타깃 반환 */
	UFUNCTION(BlueprintPure, Category = "Snow Deformation")
	UTextureRenderTarget2D* GetCurrentWriteRenderTarget() const { return bIsRTAPrimary ? SnowHeightRT_B : SnowHeightRT_A; }

	/** 지형 영역 가로/세로 크기 반환 (cm) */
	UFUNCTION(BlueprintPure, Category = "Snow Deformation")
	FVector2D GetSnowAreaExtent() const { return FVector2D(SnowVolumeExtent.X * 2.0f, SnowVolumeExtent.Y * 2.0f); }

	/** 핑퐁 버퍼 교차 스왑 */
	UFUNCTION(BlueprintCallable, Category = "Snow Deformation")
	void SwapPingPongBuffers() { bIsRTAPrimary = !bIsRTAPrimary; }

	/** 렌더 타깃이 갱신(스왑)될 때 호출되는 델리게이트 */
	UPROPERTY(BlueprintAssignable, Category = "Snow Deformation")
	FOnSnowRenderTargetUpdated OnRenderTargetUpdated;

public:
	// ----------------------------------------------------------------------
	// 풍화(Erosion) 및 브러시 설정 프로퍼티
	// ----------------------------------------------------------------------

	/** 풍화 작용(발자국이 서서히 메워지는 효과) 활성화 여부 (디버그/테스트를 위해 기본 false) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation|Erosion")
	bool bEnableErosion = false;

	/** 초당 눈 복원 속도 (0.05 = 초당 약 5%씩 복구, 20초 후 원상복귀) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation|Erosion")
	float ErosionRate = 0.05f;

	/** 발자국 도장용 브러시 머티리얼 템플릿 (M_SnowBrush) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation|Materials")
	TObjectPtr<UMaterialInterface> BrushMaterialTemplate;

	/** 풍화 복구용 머티리얼 템플릿 (M_SnowErosion) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation|Materials")
	TObjectPtr<UMaterialInterface> ErosionMaterialTemplate;

protected:
	/** 핑퐁 듀얼 렌더 타깃 및 브러시/풍화 동적 머티리얼 초기화 */
	void CreatePingPongResources();

	/** FCanvas를 활용한 배치 렌더링 및 핑퐁 복원 실행 */
	void RenderSnowDeformationBatch(float DeltaTime);

private:
	/** 이번 프레임에 수집된 발자국/궤적 페이로드 큐 */
	TArray<FSnowDeformationPayload> PendingPayloads;

	/** 핑퐁 렌더 타깃 A (R16f 포맷) */
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> SnowHeightRT_A;

	/** 핑퐁 렌더 타깃 B (R16f 포맷) */
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> SnowHeightRT_B;

	/** 현재 어떤 RT가 읽기용(Primary)인지 나타내는 플래그 */
	bool bIsRTAPrimary = true;

	/** 도장용 동적 머티리얼 인스턴스 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BrushMID;

	/** 풍화용 동적 머티리얼 인스턴스 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ErosionMID;

	/** 렌더 타깃 해상도 (기본: 1024 x 1024) */
	UPROPERTY(EditDefaultsOnly, Category = "Snow Deformation|Config")
	int32 RenderTargetResolution = 1024;

	/** 볼륨 액터의 전체 Transform (위치, 회전, 스케일) */
	UPROPERTY(Transient)
	FTransform SnowVolumeTransform = FTransform::Identity;

	/** 볼륨 액터의 Half Extent 크기 (X, Y, Z) */
	UPROPERTY(Transient)
	FVector SnowVolumeExtent = FVector(2500.0f, 2500.0f, 100.0f);

	/** 눈이 소복하게 쌓인 기본 두께 (기본값: 30cm) */
	UPROPERTY(EditDefaultsOnly, Category = "Snow Deformation|Config")
	float SnowThickness = 30.0f;
};
