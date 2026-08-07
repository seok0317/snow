// Copyright Epic Games, Inc. All Rights Reserved.

#include "SnowDeformationSubsystem.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "Kismet/KismetRenderingLibrary.h"

USnowDeformationSubsystem::USnowDeformationSubsystem()
{
}

bool USnowDeformationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// 전용 서버(Dedicated Server)에서는 렌더링이 없으므로 서브시스템 생성을 원천 차단
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

	// 핑퐁 듀얼 렌더 타깃(RT_A, RT_B) 및 브러시 동적 머티리얼 초기화
	CreatePingPongResources();
}

void USnowDeformationSubsystem::Deinitialize()
{
	PendingPayloads.Empty();
	SnowHeightRT_A = nullptr;
	SnowHeightRT_B = nullptr;
	BrushMID = nullptr;
	ErosionMID = nullptr;

	Super::Deinitialize();
}

void USnowDeformationSubsystem::CreatePingPongResources()
{
	// 1. 핑퐁 렌더 타깃 A 생성 (FCanvas 호환을 위한 16비트 Float RGBA 포맷 + 바이리니어 필터링)
	SnowHeightRT_A = NewObject<UTextureRenderTarget2D>(this, TEXT("RT_SnowHeight_A"));
	if (SnowHeightRT_A)
	{
		SnowHeightRT_A->RenderTargetFormat = RTF_RGBA16f;
		SnowHeightRT_A->ClearColor = FLinearColor::Black;
		SnowHeightRT_A->bAutoGenerateMips = false;
		SnowHeightRT_A->bCanCreateUAV = false;
		SnowHeightRT_A->Filter = TF_Bilinear;
		SnowHeightRT_A->AddressX = TA_Clamp;
		SnowHeightRT_A->AddressY = TA_Clamp;
		SnowHeightRT_A->InitAutoFormat(RenderTargetResolution, RenderTargetResolution);
		SnowHeightRT_A->UpdateResourceImmediate(true);
		UKismetRenderingLibrary::ClearRenderTarget2D(GetWorld(), SnowHeightRT_A, FLinearColor::Black);
	}

	// 2. 핑퐁 렌더 타깃 B 생성 (FCanvas 호환을 위한 16비트 Float RGBA 포맷 + 바이리니어 필터링)
	SnowHeightRT_B = NewObject<UTextureRenderTarget2D>(this, TEXT("RT_SnowHeight_B"));
	if (SnowHeightRT_B)
	{
		SnowHeightRT_B->RenderTargetFormat = RTF_RGBA16f;
		SnowHeightRT_B->ClearColor = FLinearColor::Black;
		SnowHeightRT_B->bAutoGenerateMips = false;
		SnowHeightRT_B->bCanCreateUAV = false;
		SnowHeightRT_B->Filter = TF_Bilinear;
		SnowHeightRT_B->AddressX = TA_Clamp;
		SnowHeightRT_B->AddressY = TA_Clamp;
		SnowHeightRT_B->InitAutoFormat(RenderTargetResolution, RenderTargetResolution);
		SnowHeightRT_B->UpdateResourceImmediate(true);
		UKismetRenderingLibrary::ClearRenderTarget2D(GetWorld(), SnowHeightRT_B, FLinearColor::Black);
	}

	// 3. 브러시 및 풍화 동적 머티리얼 인스턴스(MID) 생성
	if (BrushMaterialTemplate)
	{
		BrushMID = UMaterialInstanceDynamic::Create(BrushMaterialTemplate, this);
	}

	if (ErosionMaterialTemplate)
	{
		ErosionMID = UMaterialInstanceDynamic::Create(ErosionMaterialTemplate, this);
	}

	bIsRTAPrimary = true;

	UE_LOG(LogTemp, Warning, TEXT("[SnowSubsystem::Init] RT_A: %s | RT_B: %s | BrushMID: %s | ErosionMID: %s"),
		*GetNameSafe(SnowHeightRT_A), *GetNameSafe(SnowHeightRT_B), *GetNameSafe(BrushMID), *GetNameSafe(ErosionMID));
}

void USnowDeformationSubsystem::RegisterDeformationPayload(const FSnowDeformationPayload& InPayload)
{
	PendingPayloads.Add(InPayload);
}

void USnowDeformationSubsystem::SetSnowVolumeTransform(const FTransform& InTransform, const FVector& InScaledExtent, float InSnowThickness)
{
	SnowVolumeTransform = InTransform;
	SnowVolumeExtent = InScaledExtent;
	SnowThickness = InSnowThickness;

	UE_LOG(LogTemp, Warning, TEXT("[SnowSubsystem::SetTransform] Center=(%.1f, %.1f, %.1f) | Extent=(%.1f, %.1f, %.1f) | Thickness=%.1f"),
		SnowVolumeTransform.GetLocation().X, SnowVolumeTransform.GetLocation().Y, SnowVolumeTransform.GetLocation().Z,
		SnowVolumeExtent.X, SnowVolumeExtent.Y, SnowVolumeExtent.Z,
		SnowThickness);
}

void USnowDeformationSubsystem::SetBrushMaterialTemplate(UMaterialInterface* InMaterial)
{
	BrushMaterialTemplate = InMaterial;
	if (BrushMaterialTemplate)
	{
		BrushMID = UMaterialInstanceDynamic::Create(BrushMaterialTemplate, this);
	}
	UE_LOG(LogTemp, Warning, TEXT("[SnowSubsystem::SetBrush] Material Template: %s -> BrushMID: %s"),
		*GetNameSafe(InMaterial), *GetNameSafe(BrushMID));
}

void USnowDeformationSubsystem::SetErosionMaterialTemplate(UMaterialInterface* InMaterial)
{
	ErosionMaterialTemplate = InMaterial;
	if (ErosionMaterialTemplate)
	{
		ErosionMID = UMaterialInstanceDynamic::Create(ErosionMaterialTemplate, this);
	}
	UE_LOG(LogTemp, Warning, TEXT("[SnowSubsystem::SetErosion] Material Template: %s -> ErosionMID: %s"),
		*GetNameSafe(InMaterial), *GetNameSafe(ErosionMID));
}

bool USnowDeformationSubsystem::WorldLocationToUV(const FVector& WorldLocation, FVector2D& OutUV) const
{
	if (SnowVolumeExtent.X <= 0.0f || SnowVolumeExtent.Y <= 0.0f)
	{
		return false;
	}

	// 45도 기울어진 경사로에서도 왜곡 없는 로컬 UV 변환 (InverseTransformPosition)
	const FVector LocalPos = SnowVolumeTransform.InverseTransformPosition(WorldLocation);

	// 로컬 X, Y 바운드 검사 (-Extent ~ +Extent)
	if (FMath::Abs(LocalPos.X) > SnowVolumeExtent.X || FMath::Abs(LocalPos.Y) > SnowVolumeExtent.Y)
	{
		return false;
	}

	// 로컬 Z축 바운드 검사 (볼륨 박스 위아래 150cm 여유 마진 적용)
	const float MaxLocalZ = SnowVolumeExtent.Z + 150.0f;
	const float MinLocalZ = -SnowVolumeExtent.Z - 100.0f;
	if (LocalPos.Z < MinLocalZ || LocalPos.Z > MaxLocalZ)
	{
		return false;
	}

	// 0.0 ~ 1.0 UV 좌표로 정규화 매핑
	OutUV.X = (LocalPos.X + SnowVolumeExtent.X) / (SnowVolumeExtent.X * 2.0f);
	OutUV.Y = (LocalPos.Y + SnowVolumeExtent.Y) / (SnowVolumeExtent.Y * 2.0f);
	return true;
}

FVector USnowDeformationSubsystem::UVToWorldLocation(const FVector2D& InUV) const
{
	const float LocalX = (InUV.X * SnowVolumeExtent.X * 2.0f) - SnowVolumeExtent.X;
	const float LocalY = (InUV.Y * SnowVolumeExtent.Y * 2.0f) - SnowVolumeExtent.Y;
	const float LocalZ = -SnowVolumeExtent.Z;

	return SnowVolumeTransform.TransformPosition(FVector(LocalX, LocalY, LocalZ));
}

bool USnowDeformationSubsystem::IsInsideSnowArea(const FVector& WorldLocation) const
{
	if (SnowVolumeExtent.X <= 0.0f || SnowVolumeExtent.Y <= 0.0f)
	{
		return false;
	}

	const FVector LocalPos = SnowVolumeTransform.InverseTransformPosition(WorldLocation);

	if (FMath::Abs(LocalPos.X) > SnowVolumeExtent.X || FMath::Abs(LocalPos.Y) > SnowVolumeExtent.Y)
	{
		return false;
	}

	const float MaxLocalZ = SnowVolumeExtent.Z + 150.0f;
	const float MinLocalZ = -SnowVolumeExtent.Z - 100.0f;
	return (LocalPos.Z >= MinLocalZ && LocalPos.Z <= MaxLocalZ);
}

void USnowDeformationSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 발자국 궤적이 있거나, 풍화 복원이 켜져 있을 때만 배치 렌더링 수행
	if (PendingPayloads.Num() > 0 || bEnableErosion)
	{
		RenderSnowDeformationBatch(DeltaTime);
	}
}

void USnowDeformationSubsystem::RenderSnowDeformationBatch(float DeltaTime)
{
	if (PendingPayloads.Num() == 0 && !bEnableErosion)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !BrushMID)
	{
		return;
	}

	UTextureRenderTarget2D* ReadRT = GetCurrentReadRenderTarget();
	UTextureRenderTarget2D* WriteRT = GetCurrentWriteRenderTarget();

	if (!ReadRT || !WriteRT)
	{
		return;
	}

	FTextureRenderTargetResource* ReadResource = ReadRT->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* WriteResource = WriteRT->GameThread_GetRenderTargetResource();

	if (!ReadResource || !WriteResource)
	{
		return;
	}

	// ==========================================
	// 1. [GPU 하드웨어 DMA 무손실 복사]
	//    이전 프레임의 모든 발자국을 새 도화지(WriteRT)에 완벽 복사
	// ==========================================
	ENQUEUE_RENDER_COMMAND(SnowCopyRTCommand)(
		[ReadResource, WriteResource](FRHICommandListImmediate& RHICmdList)
		{
			FTextureRHIRef SourceRHI = ReadResource->GetRenderTargetTexture();
			FTextureRHIRef TargetRHI = WriteResource->GetRenderTargetTexture();
			if (SourceRHI && TargetRHI)
			{
				RHICmdList.CopyTexture(SourceRHI, TargetRHI, FRHICopyTextureInfo());
			}
		}
	);

	// ==========================================
	// 2. Kismet 라이브러리의 강제 Clear를 우회하는 Native FCanvas 직접 생성
	// ==========================================
	FCanvas DrawCanvas(
		WriteResource,
		nullptr,
		World,
		World->FeatureLevel
	);

	UCanvas* CanvasObj = NewObject<UCanvas>(GetTransientPackage());
	if (CanvasObj)
	{
		CanvasObj->Canvas = &DrawCanvas;
		CanvasObj->SizeX = WriteRT->SizeX;
		CanvasObj->SizeY = WriteRT->SizeY;
		const FVector2D CanvasSize(WriteRT->SizeX, WriteRT->SizeY);

		// ==========================================
		// 3. 복사된 도화지 위에 새 발자국 덮어찍기
		// ==========================================
		for (const FSnowDeformationPayload& Payload : PendingPayloads)
		{
			const float PixelX = Payload.HitUV.X * CanvasSize.X;
			const float PixelY = Payload.HitUV.Y * CanvasSize.Y;
			const float RadiusPx = Payload.Radius * CanvasSize.X;
			const float Size = RadiusPx * 2.0f;

			BrushMID->SetScalarParameterValue(TEXT("DepthIntensity"), Payload.DepthIntensity);

			const FVector2D ScreenPos(PixelX - RadiusPx, PixelY - RadiusPx);
			const FVector2D ScreenSize(Size, Size);
			const FVector2D PivotPoint(0.5f, 0.5f);

			CanvasObj->K2_DrawMaterial(
				BrushMID,
				ScreenPos,
				ScreenSize,
				FVector2D::ZeroVector,
				FVector2D::UnitVector,
				Payload.RotationAngle,
				PivotPoint
			);
		}
	}

	// 캔버스에 등록된 브러시 드로우 명령을 렌더 스레드로 전송 (CopyTexture 바로 뒤에 순차 실행됨)
	DrawCanvas.Flush_GameThread(true);

	// 4. 페이로드 초기화 및 핑퐁 스왑
	PendingPayloads.Reset();
	SwapPingPongBuffers();
	OnRenderTargetUpdated.Broadcast(GetCurrentReadRenderTarget());
}

TStatId USnowDeformationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USnowDeformationSubsystem, STATGROUP_Tickables);
}
