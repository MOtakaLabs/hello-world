// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ScreenPass.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Sort/GaussSplatSortKeyGen.h"
#include "GSparticles.h"
#include "GSActor.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogGSActor, Log, All);


class FGaussSplatIndexBuffer : public FIndexBuffer
{
public:
	// Initialize the RHI for this rendering resource 
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;

	virtual void ReleaseRHI() override;

	UINT NumElelments = 16;
	FShaderResourceViewRHIRef SRV;
	FUnorderedAccessViewRHIRef UAV;
};


class FGSSortedIndexBuffer : public FRenderResource
{
public:
	// Initialize the RHI for this rendering resource 
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;

	virtual void ReleaseRHI() override;

	UINT NumElelments = 0;
	FBufferRHIRef IndexBuffers[2];
	FShaderResourceViewRHIRef IndexBufferSRVs[2];
	FUnorderedAccessViewRHIRef IndexBufferUAVs[2];
};

class FGSSortedKeyBuffer : public FRenderResource
{
public:
	// Initialize the RHI for this rendering resource 
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;

	virtual void ReleaseRHI() override;

	UINT NumElelments = 0;
	FBufferRHIRef KeyBuffers[2];
	FShaderResourceViewRHIRef KeyBufferSRVs[2];
	FUnorderedAccessViewRHIRef KeyBufferUAVs[2];
};

UCLASS()
class GSRUNTIME_API AGSActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AGSActor();
	virtual ~AGSActor();

	virtual bool ShouldTickIfViewportsOnly() const override;
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	//	virtual void PostActorCreated() override;
	virtual void PostLoad() override;

	//	virtual void BeginPlay() override;
	virtual void PostInitProperties() override;

	virtual void PreInitializeComponents() override;

	
	//	virtual void PostReinitProperties() override;
	//	virtual void TickActor(float DeltaTime, ELevelTick TickType, FActorTickFunction& ThisTickFunction) override;
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostActorCreated() override;
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedChainEvent) override;
#endif	

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	
	void Render(FRDGBuilder& GraphBuilder, const FSceneView& inView, const FScreenPassTexture& SceneTexture);

	int32 GetCurrentFrame() const
	{
		return (int)(fCurrentSecond * FRAME_RATE);
	}

private:
	void ReleaseBuffers();
	bool CreateVBFromPlyFile(FRHICommandListBase& RHICmdList, const FString& Filename);


	void ReadAnimDataFromPly_RenderThread(FRHICommandListBase& RHICmdList, int32 _frameNo);

public:
	UPROPERTY(EditAnywhere, Category = "3DGS")
	FString PlyFileName;

	UPROPERTY(EditAnywhere, Category = "3DGS|Animation")
	bool bPlayAnimation = false;

	UPROPERTY(EditAnywhere, Category = "3DGS|Animation", meta = (EditCondition = "bPlayAnimation == true", EditConditionHides))
	FString AnimFilePath;

	UPROPERTY(EditAnywhere, Category = "3DGS|Animation", meta = (EditCondition = "bPlayAnimation == true", EditConditionHides))
	FString AnimFilePrefix;

	UPROPERTY(VisibleAnywhere, Category = "3DGS|Animation", meta = (EditCondition = "bPlayAnimation == true", EditConditionHides))
	int CurrentFrame = 0;

	UPROPERTY(EditAnywhere, Category = "3DGS", meta = (DisplayName = "GS List"))
	TArray<FGaussSplatInfo> GSInfos;


private:
	FMatrix WVPMat;
	FMatrix PreWVPMat;
	
	UINT NumParticles = 0;
	UINT SortedCount = 0;

	FVertexBuffer PosRotVB;
	FVertexBuffer SclVB;
	FVertexBuffer SH04VB;

	FVertexBuffer R_Sh1_4VB[3];
	FVertexBuffer G_Sh1_4VB[3];
	FVertexBuffer B_Sh1_4VB[3];

	FShaderResourceViewRHIRef PosRotVBSRV;

	FGSSortedIndexBuffer SortedIndexBuffer;
	FGSSortedKeyBuffer SortedKeyBuffer;
	
	FGaussSplatSortKeyGen SortKeyGen;
	int32 ResultBufferIndex = 0;

	const int32 FRAME_RATE = 30;
	double fCurrentSecond = 0.;
	double ElapsedTime = 0.;
	
	int PreFrame = 0;
	double fPlaySecond = 0.03333333f * 300;
};
