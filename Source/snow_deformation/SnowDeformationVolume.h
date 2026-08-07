// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SnowDeformationVolume.generated.h"

/**
 * [이식 가이드]
 * ASnowDeformationVolume:
 * 네비메시 바운드처럼 레벨에 드래그 앤 드롭하면 눈 장판(Plane)이 자동으로 깔리고 크기가 동기화되는 자동화 액터입니다.
 * 
 * [핵심 기능]
 * 1. OnConstruction: 에디터에서 박스(Box)를 늘리면 나나이트 Plane 메쉬가 1:1로 실시간 자동 스케일링
 * 2. SnowDepth(눈 두께 30cm) 및 EdgeSoftness(테두리 부드러움) 머티리얼 파라미터 자동 전달
 * 3. 45도 경사로 회전(Transform)을 USnowDeformationSubsystem에 전달하여 왜곡 없는 로컬 UV 변환 지원
 * 4. BeginPlay 시 렌더 타깃(RT_SnowHeight)을 동적 머티리얼 인스턴스(MID)에 자동 바인딩
 */
UCLASS()
class SNOW_DEFORMATION_API ASnowDeformationVolume : public AActor
{
	GENERATED_BODY()

public:
	ASnowDeformationVolume();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditMove(bool bFinished) override;
#endif

	/** 서브시스템에 이 볼륨의 FTransform 및 바운드 정보 동기화 */
	UFUNCTION(BlueprintCallable, Category = "Snow Deformation")
	void SyncBoundsToSubsystem();

	/** 동적 머티리얼 인스턴스(MID) 갱신 및 파라미터 전달 */
	UFUNCTION(BlueprintCallable, Category = "Snow Deformation")
	void UpdateMaterialParameters();

	/** 서브시스템에서 핑퐁 렌더 타깃이 갱신(스왑)될 때 호출되는 콜백 */
	UFUNCTION()
	void OnRenderTargetChanged(UTextureRenderTarget2D* NewRT);

public:
	// ----------------------------------------------------------------------
	// 컴포넌트
	// ----------------------------------------------------------------------

	/** 레벨 디자이너가 크기와 회전을 조절할 루트 박스 컴포넌트 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snow Deformation|Components")
	TObjectPtr<UBoxComponent> BoundsBox;

	/** 박스 크기에 맞춰 자동으로 크기가 늘어나는 눈 장판(Plane) 메쉬 컴포넌트 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snow Deformation|Components")
	TObjectPtr<UStaticMeshComponent> SnowPlaneMesh;

	// ----------------------------------------------------------------------
	// 설정 파라미터 (디테일 패널에서 조절 가능)
	// ----------------------------------------------------------------------

	/** 눈이 소복하게 쌓일 기본 두께 (단위: cm, 기본 30cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation|Settings")
	float SnowDepth = 30.0f;

	/** 네모난 장판 테두리를 둥글고 부드럽게 깎아낼 강도 (0.0 ~ 1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation|Settings")
	float EdgeSoftness = 0.2f;

	/** 자동으로 Plane 메쉬를 생성하여 깔아둘지 여부 (false 시 랜드스케이프 지형에 박스만 씌워서 사용) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation|Settings")
	bool bAutoSpawnPlaneMesh = true;

	/** 눈 장판에 적용할 메인 머티리얼 템플릿 (M_SnowGround) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation|Material")
	TObjectPtr<UMaterialInterface> SnowMaterialTemplate;

	/** 발자국 도장용 브러시 머티리얼 템플릿 (M_SnowBrush) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation|Material")
	TObjectPtr<UMaterialInterface> BrushMaterialTemplate;

	/** 풍화 복구용 머티리얼 템플릿 (M_SnowErosion) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation|Material")
	TObjectPtr<UMaterialInterface> ErosionMaterialTemplate;

private:
	/** 런타임에 파라미터를 제어하는 동적 머티리얼 인스턴스 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SnowMID;
};
