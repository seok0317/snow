// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SnowDeformationTypes.generated.h"

/**
 * [이식 가이드]
 * FSnowDeformationPayload:
 * GameThread에서 RenderThread로 전달되는 도장(Stamp) 및 궤적(Stroke) 데이터입니다.
 * 
 * [단일 점(Point) vs 연속 궤적(Stroke)]
 * - StrokeLength == 0.0f: 제자리 발자국 도장 (원형/타원형)
 * - StrokeLength > 0.0f: 종아리/발목/엉덩이가 눈을 가르고 지나간 연속 캡슐 궤적 (Stretched Capsule Stamp)
 */
USTRUCT(BlueprintType)
struct FSnowDeformationPayload
{
	GENERATED_BODY()

	/** 충돌/접촉한 월드 좌표 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation")
	FVector HitLocationWorld = FVector::ZeroVector;

	/** 브러시 가로 두께 반경 (월드 좌표 cm 단위) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation")
	float RadiusWorld = 15.0f;

	/**
	 * 이동 궤적 길이 (P1 -> P2 이동 거리, UV 스페이스 기준)
	 * 0.0f이면 원형 도장, 0.0f보다 크면 이동 방향으로 늘어난 캡슐 도장으로 드로우됩니다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation")
	float StrokeLength = 0.0f;

	/** 이동 궤적의 진행 방향 각도 (Yaw 각도 - 디그리, 도장 회전용) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation")
	float RotationAngle = 0.0f;

	/** 눈이 파이는 깊이 강도 (0.0 ~ 1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation")
	float DepthIntensity = 1.0f;

	/** 이동 속도 벡터 (향후 Step 5 나이아가라 파티클 비산 연동용) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation")
	FVector Velocity = FVector::ZeroVector;

	/** 연속 스윕 궤적 여부 (true: 종아리 쓸림 궤적, false: 점 접촉) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation")
	bool bIsContinuousStroke = false;
};

/**
 * 캐릭터에서 눈과 상호작용할 추적 부위(본/소켓) 설정 구조체
 */
USTRUCT(BlueprintType)
struct FSnowTrackedSocket
{
	GENERATED_BODY()

	/** 추적할 스켈레탈 메시의 소켓 또는 본 이름 (예: "foot_l", "calf_l", "pelvis") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation")
	FName SocketName = NAME_None;

	/** 해당 부위의 물리적 두께 반경 (월드 좌표 cm 단위, 예: 발목 8cm, 종아리 12cm, 엉덩이 25cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation")
	float RadiusWorld = 10.0f;

	/** 파이는 깊이 배율 (기본값: 1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow Deformation")
	float DepthMultiplier = 1.0f;

	/** 이전 프레임 위치 (런타임 캐시) */
	UPROPERTY(Transient)
	FVector PreviousLocation = FVector::ZeroVector;

	/** 이전 위치가 유효한지 여부 (텔레포트/스폰 직후 방지용) */
	UPROPERTY(Transient)
	bool bHasValidPrevLocation = false;
};
