// Copyright Epic Games, Inc. All Rights Reserved.

#include "SnowDeformationVolume.h"
#include "SnowDeformationSubsystem.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "Kismet/KismetRenderingLibrary.h"

ASnowDeformationVolume::ASnowDeformationVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	BoundsBox = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundsBox"));
	RootComponent = BoundsBox;
	BoundsBox->SetBoxExtent(FVector(2500.0f, 2500.0f, 100.0f));
	BoundsBox->SetCollisionProfileName(TEXT("NoCollision"));
	BoundsBox->SetLineThickness(2.0f);
	BoundsBox->ShapeColor = FColor(135, 206, 250);

	SnowPlaneMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SnowPlaneMesh"));
	SnowPlaneMesh->SetupAttachment(BoundsBox);
	SnowPlaneMesh->SetCollisionProfileName(TEXT("NoCollision"));
	SnowPlaneMesh->SetCastShadow(true);
	SnowPlaneMesh->bEvaluateWorldPositionOffset = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CustomSnowMeshFinder(TEXT("/Game/snow_deformation/SM_Snow.SM_Snow"));
	if (CustomSnowMeshFinder.Succeeded())
	{
		SnowPlaneMesh->SetStaticMesh(CustomSnowMeshFinder.Object);
	}
	else
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> LegacyPlaneMeshFinder(TEXT("/Game/snow_deformation/SM_SnowPlane.SM_SnowPlane"));
		if (LegacyPlaneMeshFinder.Succeeded())
		{
			SnowPlaneMesh->SetStaticMesh(LegacyPlaneMeshFinder.Object);
		}
		else
		{
			static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultPlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
			if (DefaultPlaneFinder.Succeeded())
			{
				SnowPlaneMesh->SetStaticMesh(DefaultPlaneFinder.Object);
			}
		}
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GroundMatFinder(TEXT("/Game/snow_deformation/M_SnowGround.M_SnowGround"));
	if (GroundMatFinder.Succeeded())
	{
		SnowMaterialTemplate = GroundMatFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BrushMatFinder(TEXT("/Game/snow_deformation/M_SnowBrush.M_SnowBrush"));
	if (BrushMatFinder.Succeeded())
	{
		BrushMaterialTemplate = BrushMatFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ErosionMatFinder(TEXT("/Game/snow_deformation/M_SnowErosion.M_SnowErosion"));
	if (ErosionMatFinder.Succeeded())
	{
		ErosionMaterialTemplate = ErosionMatFinder.Object;
	}
}

void ASnowDeformationVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (!BoundsBox || !SnowPlaneMesh)
	{
		return;
	}

	if (bAutoSpawnPlaneMesh)
	{
		SnowPlaneMesh->SetVisibility(true);

		const FVector BoxExtent = BoundsBox->GetScaledBoxExtent();
		float MeshWidth = 100.0f;
		float MeshDepth = 100.0f;

		if (SnowPlaneMesh->GetStaticMesh())
		{
			const FBoxSphereBounds MeshBounds = SnowPlaneMesh->GetStaticMesh()->GetBounds();
			MeshWidth = FMath::Max(MeshBounds.BoxExtent.X * 2.0f, 1.0f);
			MeshDepth = FMath::Max(MeshBounds.BoxExtent.Y * 2.0f, 1.0f);
		}

		const float ScaleX = (BoxExtent.X * 2.0f) / MeshWidth;
		const float ScaleY = (BoxExtent.Y * 2.0f) / MeshDepth;

		SnowPlaneMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -BoundsBox->GetUnscaledBoxExtent().Z + 0.1f));
		SnowPlaneMesh->SetRelativeScale3D(FVector(ScaleX, ScaleY, 1.0f));

		UpdateMaterialParameters();
	}
	else
	{
		SnowPlaneMesh->SetVisibility(false);
	}
}

void ASnowDeformationVolume::BeginPlay()
{
	Super::BeginPlay();

	CreatePingPongResources();
	UpdateMaterialParameters();

	if (UWorld* World = GetWorld())
	{
		if (USnowDeformationSubsystem* Subsystem = World->GetSubsystem<USnowDeformationSubsystem>())
		{
			Subsystem->RegisterVolume(this);
		}
	}
}

void ASnowDeformationVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (USnowDeformationSubsystem* Subsystem = World->GetSubsystem<USnowDeformationSubsystem>())
		{
			Subsystem->UnregisterVolume(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ASnowDeformationVolume::CreatePingPongResources()
{
	int32 Resolution = 2048;
	if (UWorld* World = GetWorld())
	{
		if (USnowDeformationSubsystem* Subsystem = World->GetSubsystem<USnowDeformationSubsystem>())
		{
			Resolution = Subsystem->RenderTargetResolution;
		}
	}

	SnowHeightRT_A = NewObject<UTextureRenderTarget2D>(this);
	if (SnowHeightRT_A)
	{
		SnowHeightRT_A->RenderTargetFormat = RTF_RGBA16f;
		SnowHeightRT_A->ClearColor = FLinearColor::Black;
		SnowHeightRT_A->bAutoGenerateMips = false;
		SnowHeightRT_A->bCanCreateUAV = false;
		SnowHeightRT_A->Filter = TF_Bilinear;
		SnowHeightRT_A->AddressX = TA_Clamp;
		SnowHeightRT_A->AddressY = TA_Clamp;
		SnowHeightRT_A->InitAutoFormat(Resolution, Resolution);
		SnowHeightRT_A->UpdateResourceImmediate(true);
		UKismetRenderingLibrary::ClearRenderTarget2D(GetWorld(), SnowHeightRT_A, FLinearColor::Black);
	}

	SnowHeightRT_B = NewObject<UTextureRenderTarget2D>(this);
	if (SnowHeightRT_B)
	{
		SnowHeightRT_B->RenderTargetFormat = RTF_RGBA16f;
		SnowHeightRT_B->ClearColor = FLinearColor::Black;
		SnowHeightRT_B->bAutoGenerateMips = false;
		SnowHeightRT_B->bCanCreateUAV = false;
		SnowHeightRT_B->Filter = TF_Bilinear;
		SnowHeightRT_B->AddressX = TA_Clamp;
		SnowHeightRT_B->AddressY = TA_Clamp;
		SnowHeightRT_B->InitAutoFormat(Resolution, Resolution);
		SnowHeightRT_B->UpdateResourceImmediate(true);
		UKismetRenderingLibrary::ClearRenderTarget2D(GetWorld(), SnowHeightRT_B, FLinearColor::Black);
	}

	if (BrushMaterialTemplate) BrushMID = UMaterialInstanceDynamic::Create(BrushMaterialTemplate, this);
	if (ErosionMaterialTemplate) ErosionMID = UMaterialInstanceDynamic::Create(ErosionMaterialTemplate, this);

	bIsRTAPrimary = true;
}

void ASnowDeformationVolume::UpdateMaterialParameters()
{
	if (!SnowPlaneMesh) return;

	if (SnowMaterialTemplate)
	{
		if (!SnowMID || SnowMID->Parent != SnowMaterialTemplate)
		{
			SnowMID = UMaterialInstanceDynamic::Create(SnowMaterialTemplate, this);
			SnowPlaneMesh->SetMaterial(0, SnowMID);
		}
	}
	else
	{
		SnowMID = nullptr;
		SnowPlaneMesh->SetMaterial(0, nullptr);
	}

	if (SnowMID && BoundsBox)
	{
		const FVector BoxExtent = BoundsBox->GetScaledBoxExtent();
		SnowMID->SetScalarParameterValue(TEXT("SnowDepth"), SnowDepth);
		SnowMID->SetScalarParameterValue(TEXT("EdgeSoftness"), EdgeSoftness);
		SnowMID->SetScalarParameterValue(TEXT("SnowAreaSize"), BoxExtent.X * 2.0f);
		SnowMID->SetVectorParameterValue(TEXT("SnowAreaCenter"), GetActorLocation());
		SnowMID->SetVectorParameterValue(TEXT("SnowAreaExtent"), FVector(BoxExtent.X * 2.0f, BoxExtent.Y * 2.0f, BoxExtent.Z * 2.0f));

		if (UTextureRenderTarget2D* ActiveRT = GetCurrentReadRenderTarget())
		{
			SnowMID->SetTextureParameterValue(TEXT("RT_SnowHeight"), ActiveRT);
			SnowMID->SetTextureParameterValue(TEXT("snowHeight"), ActiveRT);
			SnowMID->SetTextureParameterValue(TEXT("SnowHeight"), ActiveRT);
			SnowMID->SetTextureParameterValue(TEXT("RT_Height"), ActiveRT);
			SnowMID->SetTextureParameterValue(TEXT("RT_Snow"), ActiveRT);
			SnowMID->SetTextureParameterValue(TEXT("DeformationRT"), ActiveRT);
			SnowMID->SetTextureParameterValue(TEXT("Texture"), ActiveRT);
		}
	}
}

bool ASnowDeformationVolume::IsInsideSnowArea(const FVector& WorldLocation) const
{
	if (!BoundsBox) return false;
	const FVector BoxExtent = BoundsBox->GetScaledBoxExtent();
	if (BoxExtent.X <= 0.0f || BoxExtent.Y <= 0.0f) return false;

	const FVector LocalPos = GetActorTransform().InverseTransformPosition(WorldLocation);
	if (FMath::Abs(LocalPos.X) > BoxExtent.X || FMath::Abs(LocalPos.Y) > BoxExtent.Y) return false;

	const float MaxLocalZ = BoxExtent.Z + 150.0f;
	const float MinLocalZ = -BoxExtent.Z - 100.0f;
	return (LocalPos.Z >= MinLocalZ && LocalPos.Z <= MaxLocalZ);
}

bool ASnowDeformationVolume::WorldLocationToUV(const FVector& WorldLocation, FVector2D& OutUV) const
{
	if (!BoundsBox) return false;
	const FVector BoxExtent = BoundsBox->GetScaledBoxExtent();
	if (BoxExtent.X <= 0.0f || BoxExtent.Y <= 0.0f) return false;

	const FVector LocalPos = GetActorTransform().InverseTransformPosition(WorldLocation);
	if (FMath::Abs(LocalPos.X) > BoxExtent.X || FMath::Abs(LocalPos.Y) > BoxExtent.Y) return false;

	const float MaxLocalZ = BoxExtent.Z + 150.0f;
	const float MinLocalZ = -BoxExtent.Z - 100.0f;
	if (LocalPos.Z < MinLocalZ || LocalPos.Z > MaxLocalZ) return false;

	OutUV.X = (LocalPos.X + BoxExtent.X) / (BoxExtent.X * 2.0f);
	OutUV.Y = (LocalPos.Y + BoxExtent.Y) / (BoxExtent.Y * 2.0f);
	return true;
}

float ASnowDeformationVolume::GetSnowSurfaceZ() const
{
	if (!BoundsBox) return 0.0f;
	return GetActorLocation().Z - BoundsBox->GetScaledBoxExtent().Z + SnowDepth;
}

float ASnowDeformationVolume::GetSnowBaseZ() const
{
	if (!BoundsBox) return 0.0f;
	return GetActorLocation().Z - BoundsBox->GetScaledBoxExtent().Z;
}

void ASnowDeformationVolume::RegisterDeformationPayload(const FSnowDeformationPayload& InPayload)
{
	PendingPayloads.Add(InPayload);
}

void ASnowDeformationVolume::ProcessDeformationBatch(float DeltaTime)
{
	bool bEnableErosion = false;
	if (UWorld* World = GetWorld())
	{
		if (USnowDeformationSubsystem* Subsystem = World->GetSubsystem<USnowDeformationSubsystem>())
		{
			bEnableErosion = Subsystem->bEnableErosion;
		}
	}

	if (PendingPayloads.Num() == 0 && !bEnableErosion) return;

	UTextureRenderTarget2D* ReadRT = GetCurrentReadRenderTarget();
	UTextureRenderTarget2D* WriteRT = GetCurrentWriteRenderTarget();

	if (!ReadRT || !WriteRT || !BrushMID) return;

	FTextureRenderTargetResource* ReadResource = ReadRT->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* WriteResource = WriteRT->GameThread_GetRenderTargetResource();

	if (!ReadResource || !WriteResource) return;

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

	FCanvas DrawCanvas(WriteResource, nullptr, GetWorld(), GetWorld()->FeatureLevel);
	UCanvas* CanvasObj = NewObject<UCanvas>(GetTransientPackage());
	if (CanvasObj)
	{
		CanvasObj->Canvas = &DrawCanvas;
		CanvasObj->SizeX = WriteRT->SizeX;
		CanvasObj->SizeY = WriteRT->SizeY;
		const FVector2D CanvasSize(WriteRT->SizeX, WriteRT->SizeY);

		for (const FSnowDeformationPayload& Payload : PendingPayloads)
		{
			FVector2D HitUV;
			if (!WorldLocationToUV(Payload.HitLocationWorld, HitUV)) continue;

			// World Radius to UV Radius
			const FVector BoxExtent = BoundsBox->GetScaledBoxExtent();
			const float TotalVolumeWidth = BoxExtent.X * 2.0f;
			const float RadiusUV = (TotalVolumeWidth > 0.0f) ? (Payload.RadiusWorld / TotalVolumeWidth) : 0.015f;

			const float PixelX = HitUV.X * CanvasSize.X;
			const float PixelY = HitUV.Y * CanvasSize.Y;
			const float RadiusPx = RadiusUV * CanvasSize.X;
			const float Size = RadiusPx * 2.0f;

			BrushMID->SetScalarParameterValue(TEXT("DepthIntensity"), Payload.DepthIntensity);
			BrushMID->SetTextureParameterValue(TEXT("PreviousRT"), ReadRT);

			const FVector2D ScreenPos(PixelX - RadiusPx, PixelY - RadiusPx);
			const FVector2D ScreenSize(Size, Size);
			
			CanvasObj->K2_DrawMaterial(BrushMID, ScreenPos, ScreenSize, FVector2D::ZeroVector, FVector2D::UnitVector, Payload.RotationAngle, FVector2D(0.5f, 0.5f));
		}
	}

	DrawCanvas.Flush_GameThread(true);
	PendingPayloads.Reset();
	SwapPingPongBuffers();

	// Update the SnowMID pointer to the new Read RT
	if (SnowMID && GetCurrentReadRenderTarget())
	{
		UTextureRenderTarget2D* ActiveRT = GetCurrentReadRenderTarget();
		SnowMID->SetTextureParameterValue(TEXT("RT_SnowHeight"), ActiveRT);
		SnowMID->SetTextureParameterValue(TEXT("snowHeight"), ActiveRT);
		SnowMID->SetTextureParameterValue(TEXT("SnowHeight"), ActiveRT);
		SnowMID->SetTextureParameterValue(TEXT("RT_Height"), ActiveRT);
		SnowMID->SetTextureParameterValue(TEXT("RT_Snow"), ActiveRT);
		SnowMID->SetTextureParameterValue(TEXT("DeformationRT"), ActiveRT);
		SnowMID->SetTextureParameterValue(TEXT("Texture"), ActiveRT);
	}
}

#if WITH_EDITOR
void ASnowDeformationVolume::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateMaterialParameters();
}
#endif
