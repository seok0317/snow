// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SnowDeformationTypes.h"
#include "Engine/TextureRenderTarget2D.h"
#include "SnowDeformationSubsystem.generated.h"

class ASnowDeformationVolume;

UCLASS()
class SNOW_DEFORMATION_API USnowDeformationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	USnowDeformationSubsystem();

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// 볼륨 등록 및 해제 (볼륨의 BeginPlay/EndPlay에서 호출)
	void RegisterVolume(ASnowDeformationVolume* Volume);
	void UnregisterVolume(ASnowDeformationVolume* Volume);

	/** 캐릭터 컴포넌트에서 호출: 프레임 동안 수집된 페이로드를 적절한 볼륨으로 라우팅 */
	UFUNCTION(BlueprintCallable, Category = "Snow Deformation")
	void RegisterDeformationPayload(const FSnowDeformationPayload& InPayload);

	/** 특정 월드 위치가 속한 눈 영역의 표면 높이(Z)와 눈 두께(Thickness) 반환 */
	UFUNCTION(BlueprintPure, Category = "Snow Deformation")
	bool GetSnowSurfaceZAtLocation(const FVector& WorldLocation, float& OutSurfaceZ, float& OutThickness) const;

public:
	// 전역 설정 프로퍼티 (각 볼륨이 이 값을 참조함)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation|Erosion")
	bool bEnableErosion = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation|Erosion")
	float ErosionRate = 0.05f;

	UPROPERTY(EditDefaultsOnly, Category = "Snow Deformation|Config")
	int32 RenderTargetResolution = 2048;

private:
	// 현재 월드에 등록된 눈 볼륨들
	UPROPERTY(Transient)
	TArray<TObjectPtr<ASnowDeformationVolume>> ActiveVolumes;
};
