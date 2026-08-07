// Copyright Epic Games, Inc. All Rights Reserved.

#include "SnowDeformationVolume.h"
#include "SnowDeformationSubsystem.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

ASnowDeformationVolume::ASnowDeformationVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	// 1. 루트 박스 컴포넌트 생성 (기본 크기: 50m x 50m x 2m)
	BoundsBox = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundsBox"));
	RootComponent = BoundsBox;
	BoundsBox->SetBoxExtent(FVector(2500.0f, 2500.0f, 100.0f));
	BoundsBox->SetCollisionProfileName(TEXT("NoCollision"));
	BoundsBox->SetLineThickness(2.0f);
	BoundsBox->ShapeColor = FColor(135, 206, 250); // 하늘색 와이어프레임

	// 2. 자식 눈 장판(Plane) 메쉬 컴포넌트 생성 (나나이트 WPO 실시간 활성화)
	SnowPlaneMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SnowPlaneMesh"));
	SnowPlaneMesh->SetupAttachment(BoundsBox);
	SnowPlaneMesh->SetCollisionProfileName(TEXT("NoCollision"));
	SnowPlaneMesh->SetCastShadow(true);
	SnowPlaneMesh->bEvaluateWorldPositionOffset = true;

	// 프로젝트 전용 SM_Snow 우선 로드, 없으면 SM_SnowPlane 및 기본 Plane 순으로 폴백
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

	// 기본 머티리얼 템플릿 자동 바인딩
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

		// 1. 박스 Extent 크기에 맞춰 Plane 메쉬 Scale 1:1 자동 매핑 (스태틱 메시의 실제 크기 기반 정밀 매핑)
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

		// 2. 동적 머티리얼 인스턴스(MID) 파라미터 실시간 전달
		UpdateMaterialParameters();
	}
	else
	{
		SnowPlaneMesh->SetVisibility(false);
	}

	SyncBoundsToSubsystem();
}

void ASnowDeformationVolume::BeginPlay()
{
	Super::BeginPlay();

	UpdateMaterialParameters();
	SyncBoundsToSubsystem();

	UE_LOG(LogTemp, Warning, TEXT("[SnowVolume::BeginPlay] Volume: %s | MeshComp: %s | StaticMesh: %s | SnowTemplate: %s | BrushTemplate: %s | SnowMID: %s"),
		*GetNameSafe(this),
		*GetNameSafe(SnowPlaneMesh),
		SnowPlaneMesh ? *GetNameSafe(SnowPlaneMesh->GetStaticMesh()) : TEXT("None"),
		*GetNameSafe(SnowMaterialTemplate),
		*GetNameSafe(BrushMaterialTemplate),
		*GetNameSafe(SnowMID));

	if (!SnowMaterialTemplate && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(54322, 5.0f, FColor::Red, TEXT("[Snow Volume ERROR] SnowMaterialTemplate is NULL! Please assign M_SnowGround in SnowDeformationVolume Details!"));
	}

	if (!BrushMaterialTemplate && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(54323, 5.0f, FColor::Red, TEXT("[Snow Volume ERROR] BrushMaterialTemplate is NULL! Please assign M_SnowBrush in SnowDeformationVolume Details!"));
	}

	// 게임 시작 시 서브시스템에 브러시/풍화 머티리얼 전달 및 렌더 타깃 갱신 델리게이트 바인딩
	if (UWorld* World = GetWorld())
	{
		if (USnowDeformationSubsystem* Subsystem = World->GetSubsystem<USnowDeformationSubsystem>())
		{
			if (BrushMaterialTemplate)
			{
				Subsystem->SetBrushMaterialTemplate(BrushMaterialTemplate);
			}

			if (ErosionMaterialTemplate)
			{
				Subsystem->SetErosionMaterialTemplate(ErosionMaterialTemplate);
			}

			Subsystem->OnRenderTargetUpdated.AddDynamic(this, &ASnowDeformationVolume::OnRenderTargetChanged);

			if (SnowMID && Subsystem->GetCurrentReadRenderTarget())
			{
				UTextureRenderTarget2D* ActiveRT = Subsystem->GetCurrentReadRenderTarget();
				SnowMID->SetTextureParameterValue(TEXT("RT_SnowHeight"), ActiveRT);
				SnowMID->SetTextureParameterValue(TEXT("snowHeight"), ActiveRT);
				SnowMID->SetTextureParameterValue(TEXT("SnowHeight"), ActiveRT);
				SnowMID->SetTextureParameterValue(TEXT("RT_Height"), ActiveRT);
				SnowMID->SetTextureParameterValue(TEXT("RT_Snow"), ActiveRT);
				SnowMID->SetTextureParameterValue(TEXT("DeformationRT"), ActiveRT);
				SnowMID->SetTextureParameterValue(TEXT("Texture"), ActiveRT);

				UE_LOG(LogTemp, Warning, TEXT("[SnowVolume::BeginPlay] Initial RT bound to SnowMID: %s"), *GetNameSafe(ActiveRT));
			}
		}
	}
}

void ASnowDeformationVolume::OnRenderTargetChanged(UTextureRenderTarget2D* NewRT)
{
	if (SnowMID && NewRT)
	{
		SnowMID->SetTextureParameterValue(TEXT("RT_SnowHeight"), NewRT);
		SnowMID->SetTextureParameterValue(TEXT("snowHeight"), NewRT);
		SnowMID->SetTextureParameterValue(TEXT("SnowHeight"), NewRT);
		SnowMID->SetTextureParameterValue(TEXT("RT_Height"), NewRT);
		SnowMID->SetTextureParameterValue(TEXT("RT_Snow"), NewRT);
		SnowMID->SetTextureParameterValue(TEXT("DeformationRT"), NewRT);
		SnowMID->SetTextureParameterValue(TEXT("Texture"), NewRT);

		if (BoundsBox)
		{
			const FVector BoxExtent = BoundsBox->GetScaledBoxExtent();
			SnowMID->SetVectorParameterValue(TEXT("SnowAreaCenter"), GetActorLocation());
			SnowMID->SetScalarParameterValue(TEXT("SnowAreaSize"), BoxExtent.X * 2.0f);
			SnowMID->SetScalarParameterValue(TEXT("SnowDepth"), SnowDepth);
			SnowMID->SetScalarParameterValue(TEXT("EdgeSoftness"), EdgeSoftness);
		}

		UE_LOG(LogTemp, Display, TEXT("[SnowVolume::OnRenderTargetChanged] Updated SnowMID (%s) with RT: %s"),
			*GetNameSafe(SnowMID), *GetNameSafe(NewRT));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[SnowVolume::OnRenderTargetChanged] FAILED! SnowMID: %s | NewRT: %s"),
			*GetNameSafe(SnowMID), *GetNameSafe(NewRT));
	}
}

void ASnowDeformationVolume::UpdateMaterialParameters()
{
	if (!SnowPlaneMesh)
	{
		return;
	}

	// 1. 머티리얼 템플릿이 지정되어 있으면 MID를 최신 템플릿 기준으로 생성/교체
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

	// 2. 동적 머티리얼 인스턴스에 파라미터 전달
	if (SnowMID && BoundsBox)
	{
		const FVector BoxExtent = BoundsBox->GetScaledBoxExtent();
		const FVector ActorCenter = GetActorLocation();

		SnowMID->SetScalarParameterValue(TEXT("SnowDepth"), SnowDepth);
		SnowMID->SetScalarParameterValue(TEXT("EdgeSoftness"), EdgeSoftness);
		SnowMID->SetScalarParameterValue(TEXT("SnowAreaSize"), BoxExtent.X * 2.0f);
		SnowMID->SetVectorParameterValue(TEXT("SnowAreaCenter"), ActorCenter);
		SnowMID->SetVectorParameterValue(TEXT("SnowAreaExtent"), FVector(BoxExtent.X * 2.0f, BoxExtent.Y * 2.0f, BoxExtent.Z * 2.0f));

		if (UWorld* World = GetWorld())
		{
			if (USnowDeformationSubsystem* Subsystem = World->GetSubsystem<USnowDeformationSubsystem>())
			{
				if (Subsystem->GetCurrentReadRenderTarget())
				{
					UTextureRenderTarget2D* ActiveRT = Subsystem->GetCurrentReadRenderTarget();
					SnowMID->SetTextureParameterValue(TEXT("RT_SnowHeight"), ActiveRT);
					SnowMID->SetTextureParameterValue(TEXT("snowHeight"), ActiveRT);
					SnowMID->SetTextureParameterValue(TEXT("SnowHeight"), ActiveRT);
					SnowMID->SetTextureParameterValue(TEXT("RT_Height"), ActiveRT);
					SnowMID->SetTextureParameterValue(TEXT("RT_Snow"), ActiveRT);
				}
			}
		}
	}
}

void ASnowDeformationVolume::SyncBoundsToSubsystem()
{
	UWorld* World = GetWorld();
	if (!World || !BoundsBox)
	{
		return;
	}

	USnowDeformationSubsystem* Subsystem = World->GetSubsystem<USnowDeformationSubsystem>();
	if (!Subsystem)
	{
		return;
	}

	// 45도 회전된 경사로도 완벽히 지원하기 위해 전체 Transform(위치, 회전, 스케일)을 전달
	const FTransform VolumeTransform = GetActorTransform();
	const FVector BoxExtent = BoundsBox->GetScaledBoxExtent();

	Subsystem->SetSnowVolumeTransform(VolumeTransform, BoxExtent, SnowDepth);
}

#if WITH_EDITOR
void ASnowDeformationVolume::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateMaterialParameters();
	SyncBoundsToSubsystem();
}

void ASnowDeformationVolume::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);
	if (bFinished)
	{
		SyncBoundsToSubsystem();
	}
}
#endif
