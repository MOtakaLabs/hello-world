// Fill out your copyright notice in the Description page of Project Settings.


#include "GSActor.h"
#include <format>
#include "LevelEditorViewport.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "Kismet/KismetSystemLibrary.h" 
#include "GSShaderWorldSubsystem.h"

#include "loader/ply/PlyAttributeCollection.h"

#include "PostProcess/PostProcessMaterialInputs.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/PostProcessMaterial.h"
#include "Camera/CameraActor.h"
#include "ID3D12DynamicRHI.h"
#include "GPUSort.h"


DEFINE_LOG_CATEGORY(LogGSActor);
DECLARE_GPU_STAT(GSActor);

//DECLARE_STATS_GROUP(TEXT("GSActor"), STATGROUP_GSActor, STATCAT_Advanced);
//DECLARE_CYCLE_STAT(TEXT("GSActor Execute"), STAT_GSActor_Execute, STATGROUP_GSActor);

//----------------------------------------------------------------------------------------------------------------------------
namespace PLY
{
	struct FGaussSplatVertex
	{
		UINT NumParticles;
		TResourceArray<FVector4f, VERTEXBUFFER_ALIGNMENT> pos;
		TResourceArray<FVector4f, VERTEXBUFFER_ALIGNMENT> rot;
		TResourceArray<FVector4f, VERTEXBUFFER_ALIGNMENT> scl;

		TResourceArray<FVector4f, VERTEXBUFFER_ALIGNMENT> r_sh0;
		TResourceArray<FVector4f, VERTEXBUFFER_ALIGNMENT> g_sh0;
		TResourceArray<FVector4f, VERTEXBUFFER_ALIGNMENT> b_sh0;

		TResourceArray<FVector4f, VERTEXBUFFER_ALIGNMENT> r_sh1_4[3];
		TResourceArray<FVector4f, VERTEXBUFFER_ALIGNMENT> g_sh1_4[3];
		TResourceArray<FVector4f, VERTEXBUFFER_ALIGNMENT> b_sh1_4[3];
	};

	bool LoadPlyFile(const FString& Filename, FGaussSplatVertex& GSData)
	{
		bool bResult = false;
		Ply3DGS::FAttributeCollection collection;
		
		// position
		auto SetPositionFunc = [&GSData](const Ply3DGS::FAttributeCollection* attri, int32 _order = -1)
			{
				std::vector<float> arr_x;
				std::vector<float> arr_y;
				std::vector<float> arr_z;
				std::vector<float> arr_a;

				attri->GetAttributeArray<float>("x", arr_x);
				attri->GetAttributeArray<float>("y", arr_y);
				attri->GetAttributeArray<float>("z", arr_z);
				attri->GetAttributeArray<float>("opacity", arr_a);

				GSData.pos.SetNumUninitialized(GSData.NumParticles);
				for (UINT i = 0; i < GSData.NumParticles; ++i) {
					GSData.pos[i] = FVector4f(arr_x[i], -arr_z[i], -arr_y[i], 0) * 100.f;	// m -> cm
					GSData.pos[i].W = 1.f / (1.f + FMath::Exp(-arr_a[i]));
				}
			};

		// rotation
		auto SetRotationFunc = [&GSData](const Ply3DGS::FAttributeCollection* attri, int32 _order = -1)
			{
				std::vector<float> arr_x;
				std::vector<float> arr_y;
				std::vector<float> arr_z;
				std::vector<float> arr_w;

				attri->GetAttributeArray<float>("rot_0", arr_x);
				attri->GetAttributeArray<float>("rot_1", arr_y);
				attri->GetAttributeArray<float>("rot_2", arr_z);
				attri->GetAttributeArray<float>("rot_3", arr_w);

				GSData.rot.SetNumUninitialized(GSData.NumParticles);
				for (UINT i = 0; i < GSData.NumParticles; ++i) {
					FVector4f rot = FVector4f(arr_x[i], arr_y[i], arr_z[i], arr_w[i]);
					// normalize
					float len = rot.Size();
					rot /= len;

					GSData.rot[i] = FVector4f(rot.X, rot.Y, rot.Z, rot.W);
				}
			};

		// scale
		auto SetScaleFunc = [&GSData](const Ply3DGS::FAttributeCollection* attri, int32 _order = -1)
			{
				std::vector<float> arr_x;
				std::vector<float> arr_y;
				std::vector<float> arr_z;
				
				attri->GetAttributeArray<float>("scale_0", arr_x);
				attri->GetAttributeArray<float>("scale_1", arr_y);
				attri->GetAttributeArray<float>("scale_2", arr_z);
				
				GSData.scl.SetNumUninitialized(GSData.NumParticles);
				for (UINT i = 0; i < GSData.NumParticles; ++i) {
					GSData.scl[i] = FVector4f(FMath::Exp(arr_x[i]), FMath::Exp(arr_y[i]), FMath::Exp(arr_z[i]), 0.f);
				}
			};

		// r-color0
		auto SetRColor0Func = [&GSData](const Ply3DGS::FAttributeCollection* attri, int32 _order = -1)
			{
				std::vector<float> arr_r0;
				std::vector<float> arr_r1;
				std::vector<float> arr_r2;
				std::vector<float> arr_r3;

				attri->GetAttributeArray<float>("f_dc_0", arr_r0);
				attri->GetAttributeArray<float>("f_rest_0", arr_r1);
				attri->GetAttributeArray<float>("f_rest_1", arr_r2);
				attri->GetAttributeArray<float>("f_rest_2", arr_r3);

				GSData.r_sh0.SetNumUninitialized(GSData.NumParticles);
				for (UINT i = 0; i < GSData.NumParticles; ++i) {
					GSData.r_sh0[i] = FVector4f(arr_r0[i], arr_r1[i], arr_r2[i], arr_r3[i]);
				}
			};
		// g-color0
		auto SetGColor0Func = [&GSData](const Ply3DGS::FAttributeCollection* attri, int32 _order = -1)
			{
				std::vector<float> arr_g0;
				std::vector<float> arr_g1;
				std::vector<float> arr_g2;
				std::vector<float> arr_g3;

				attri->GetAttributeArray<float>("f_dc_1", arr_g0);
				attri->GetAttributeArray<float>("f_rest_15", arr_g1);
				attri->GetAttributeArray<float>("f_rest_16", arr_g2);
				attri->GetAttributeArray<float>("f_rest_17", arr_g3);

				GSData.g_sh0.SetNumUninitialized(GSData.NumParticles);
				for (UINT i = 0; i < GSData.NumParticles; ++i) {
					GSData.g_sh0[i] = FVector4f(arr_g0[i], arr_g1[i], arr_g2[i], arr_g3[i]);
				}
			};
		// b-color0
		auto SetBColor0Func = [&GSData](const Ply3DGS::FAttributeCollection* attri, int32 _order = -1)
			{
				std::vector<float> arr_b0;
				std::vector<float> arr_b1;
				std::vector<float> arr_b2;
				std::vector<float> arr_b3;

				attri->GetAttributeArray<float>("f_dc_2", arr_b0);
				attri->GetAttributeArray<float>("f_rest_30", arr_b1);
				attri->GetAttributeArray<float>("f_rest_31", arr_b2);
				attri->GetAttributeArray<float>("f_rest_32", arr_b3);

				GSData.b_sh0.SetNumUninitialized(GSData.NumParticles);
				for (UINT i = 0; i < GSData.NumParticles; ++i) {
					GSData.b_sh0[i] = FVector4f(arr_b0[i], arr_b1[i], arr_b2[i], arr_b3[i]);
				}
			};

		// r-color1-4
		auto SetR_Sh1_4Func = [&GSData](const Ply3DGS::FAttributeCollection* attri, int32 _order = -1)
			{
				std::vector<float> arr_r0;
				std::vector<float> arr_r1;
				std::vector<float> arr_r2;
				std::vector<float> arr_r3;

				std::string StrR0 = std::format("f_rest_{}", 3 + _order * 4 + 0);
				std::string StrR1 = std::format("f_rest_{}", 3 + _order * 4 + 1);
				std::string StrR2 = std::format("f_rest_{}", 3 + _order * 4 + 2);
				std::string StrR3 = std::format("f_rest_{}", 3 + _order * 4 + 3);

				attri->GetAttributeArray<float>(StrR0, arr_r0);
				attri->GetAttributeArray<float>(StrR1, arr_r1);
				attri->GetAttributeArray<float>(StrR2, arr_r2);
				attri->GetAttributeArray<float>(StrR3, arr_r3);

				int32 idx = _order;
				GSData.r_sh1_4[idx].SetNumUninitialized(GSData.NumParticles);
				for (UINT i = 0; i < GSData.NumParticles; ++i) {
					GSData.r_sh1_4[idx][i] = FVector4f(arr_r0[i], arr_r1[i], arr_r2[i], arr_r3[i]);
				}
			};

		// g-color1-4
		auto SetG_Sh1_4Func = [&GSData](const Ply3DGS::FAttributeCollection* attri, int32 _order = -1)
			{
				std::vector<float> arr_g0;
				std::vector<float> arr_g1;
				std::vector<float> arr_g2;
				std::vector<float> arr_g3;

				std::string StrG0 = std::format("f_rest_{}", 18 + _order * 4 + 0);
				std::string StrG1 = std::format("f_rest_{}", 18 + _order * 4 + 1);
				std::string StrG2 = std::format("f_rest_{}", 18 + _order * 4 + 2);
				std::string StrG3 = std::format("f_rest_{}", 18 + _order * 4 + 3);

				attri->GetAttributeArray<float>(StrG0, arr_g0);
				attri->GetAttributeArray<float>(StrG1, arr_g1);
				attri->GetAttributeArray<float>(StrG2, arr_g2);
				attri->GetAttributeArray<float>(StrG3, arr_g3);

				int32 idx = _order;
				GSData.g_sh1_4[idx].SetNumUninitialized(GSData.NumParticles);
				for (UINT i = 0; i < GSData.NumParticles; ++i) {
					GSData.g_sh1_4[idx][i] = FVector4f(arr_g0[i], arr_g1[i], arr_g2[i], arr_g3[i]);
				}
			};

		// b-color1-4
		auto SetB_Sh1_4Func = [&GSData](const Ply3DGS::FAttributeCollection* attri, int32 _order = -1)
			{
				std::vector<float> arr_b0;
				std::vector<float> arr_b1;
				std::vector<float> arr_b2;
				std::vector<float> arr_b3;

				std::string StrB0 = std::format("f_rest_{}", 33 + _order * 4 + 0);
				std::string StrB1 = std::format("f_rest_{}", 33 + _order * 4 + 1);
				std::string StrB2 = std::format("f_rest_{}", 33 + _order * 4 + 2);
				std::string StrB3 = std::format("f_rest_{}", 33 + _order * 4 + 3);

				attri->GetAttributeArray<float>(StrB0, arr_b0);
				attri->GetAttributeArray<float>(StrB1, arr_b1);
				attri->GetAttributeArray<float>(StrB2, arr_b2);
				attri->GetAttributeArray<float>(StrB3, arr_b3);

				int32 idx = _order;
				GSData.b_sh1_4[idx].SetNumUninitialized(GSData.NumParticles);
				for (UINT i = 0; i < GSData.NumParticles; ++i) {
					GSData.b_sh1_4[idx][i] = FVector4f(arr_b0[i], arr_b1[i], arr_b2[i], arr_b3[i]);
				}
			};
		

		std::ifstream stream(*Filename, std::ios::in | std::ios::binary);
		if (stream.is_open()) {
			Ply3DGS::EPlyErrorCode error = collection.LoadFromStream(stream);
			if (error == Ply3DGS::EPlyErrorCode::PLY_OK) {

				GSData.NumParticles = collection.GetParticleCount();

				TArray<TFunction<void(const Ply3DGS::FAttributeCollection*, int32)>> funcArr;
			
				for (int i = 0; i < 3; ++i) {
					funcArr.Add(SetR_Sh1_4Func);
					funcArr.Add(SetG_Sh1_4Func);
					funcArr.Add(SetB_Sh1_4Func);
				}

				funcArr.Add(SetPositionFunc);
				funcArr.Add(SetRotationFunc);
				funcArr.Add(SetScaleFunc);
			
				funcArr.Add(SetRColor0Func);
				funcArr.Add(SetGColor0Func);
				funcArr.Add(SetBColor0Func);

			//	for (int Idx = 0; Idx < funcArr.Num(); ++Idx) {
			//		funcArr[Idx](&collection, Idx / 3);
			//	}

				ParallelFor(funcArr.Num(), [&](int32 Idx)
					{
						funcArr[Idx](&collection, Idx/3);
					});

				bResult = true;
			}
		}
		return bResult;
	}
}	// namespace ply


//----------------------------------------------------------------------------------------------------------------------------
/*
*  FTriangleVS
*/
class FTriangleVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTriangleVS);
	SHADER_USE_PARAMETER_STRUCT(FTriangleVS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FTriangleVSParameters, )
//		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(FVector3f, gPreViewTranslation3)
		SHADER_PARAMETER(FVector2f, gViewSize2)
		SHADER_PARAMETER(FMatrix44f, gLocal2World4x4)
		SHADER_PARAMETER(FMatrix44f, gTranslatedWorld2View4x4)
		SHADER_PARAMETER(FMatrix44f, gView2Clip4x4)
		SHADER_PARAMETER(FMatrix44f, gTranslatedWorldToClip4x4)
		
	END_SHADER_PARAMETER_STRUCT()

	using FParameters = FTriangleVSParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM6);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("FULL_SH"), 1);
	}
};
IMPLEMENT_SHADER_TYPE(, FTriangleVS, TEXT("/GSRuntime/GaussianSplatting.usf"), TEXT("MainVS"), SF_Vertex);

/*
*  FTriangleGS
*/
class FTriangleGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTriangleGS);
	SHADER_USE_PARAMETER_STRUCT(FTriangleGS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FTriangleGSParameters, )
		//	RENDER_TARGET_BINDING_SLOTS()
		SHADER_PARAMETER(FVector2f, gViewSize2)
	END_SHADER_PARAMETER_STRUCT()

	using FParameters = FTriangleGSParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
	//	static FName NAME_LocalVertexFactory(TEXT("FLocalVertexFactory"));
	//	Parameters.VertexFactoryType == FindVertexFactoryType(NAME_LocalVertexFactory);

		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM6);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	//	OutEnvironment.SetDefine(TEXT("FULL_SH"), 1);
	}
};
IMPLEMENT_SHADER_TYPE(, FTriangleGS, TEXT("/GSRuntime/GaussianSplatting.usf"), TEXT("MainGS"), SF_Geometry);

/*
*  FTrianglePS
*/
class FTrianglePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTrianglePS);
	SHADER_USE_PARAMETER_STRUCT(FTrianglePS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FTrianglePSParameters, )
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	using FParameters = FTrianglePSParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM6);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	//	OutEnvironment.SetDefine(TEXT("FULL_SH"), 1);
	}
};
IMPLEMENT_SHADER_TYPE(, FTrianglePS, TEXT("/GSRuntime/GaussianSplatting.usf"), TEXT("MainPS"), SF_Pixel);




//----------------------------------------------------------------------------------------------------------------------------
/*
*  FTriangleVertexDeclaration
*/
class FTriangleVertexDeclaration : public FRenderResource
{
public:

	virtual ~FTriangleVertexDeclaration()
	{
	}

	virtual void InitRHI(FRHICommandListBase& RHICmdList) override
	{
		FVertexDeclarationElementList Elements;
		uint16 Stride = sizeof(FVector4f);
		int32 Offset = 0;

		Elements.Add(FVertexElement(0, 0, VET_Float4, 0, Stride));	// position
		Elements.Add(FVertexElement(1, 0, VET_Float4, 1, Stride));	// rotation
		Elements.Add(FVertexElement(2, 0, VET_Float4, 2, Stride));	// scale

		Stride = sizeof(FVector4f) * 3;
		Offset = 0;
		Elements.Add(FVertexElement(3, Offset, VET_Float4, 3, Stride));	// sh0_r
		Offset += sizeof(FVector4f);
		Elements.Add(FVertexElement(3, Offset, VET_Float4, 4, Stride));	// sh0_g
		Offset += sizeof(FVector4f);
		Elements.Add(FVertexElement(3, Offset, VET_Float4, 5, Stride));	// sh0_b

	//	Elements.Add(FVertexElement(3, 0, VET_Float4, 3, Stride));	// sh0_r
	//	Elements.Add(FVertexElement(4, 0, VET_Float4, 4, Stride));	// sh0_g
	//	Elements.Add(FVertexElement(5, 0, VET_Float4, 5, Stride));	// sh0_b

		Stride = sizeof(FVector4f);
		Elements.Add(FVertexElement(6, 0, VET_Float4, 6, Stride));
		Elements.Add(FVertexElement(7, 0, VET_Float4, 7, Stride));
		Elements.Add(FVertexElement(8, 0, VET_Float4, 8, Stride));
	
		Elements.Add(FVertexElement(9, 0, VET_Float4, 9, Stride));
		Elements.Add(FVertexElement(10, 0, VET_Float4, 10, Stride));
		Elements.Add(FVertexElement(11, 0, VET_Float4, 11, Stride));
	
		Elements.Add(FVertexElement(12, 0, VET_Float4, 12, Stride));
		Elements.Add(FVertexElement(13, 0, VET_Float4, 13, Stride));
		Elements.Add(FVertexElement(14, 0, VET_Float4, 14, Stride));


		VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
	}
	virtual void ReleaseRHI() override
	{
		VertexDeclarationRHI.SafeRelease();
	}

public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;
};
TGlobalResource<FTriangleVertexDeclaration> GTriangleVertexDeclaration;





//----------------------------------------------------------------------------------------------------------------------------
/*
*  FGaussSplatIndexBuffer
*/
//TGlobalResource<FGaussSplatIndexBuffer> GGSIndexBuffer;

void FGaussSplatIndexBuffer::InitRHI(FRHICommandListBase& RHICmdList)
{
	TResourceArray<UINT, INDEXBUFFER_ALIGNMENT> IndexBuffer;
	uint32 NumIndices = NumElelments;
	IndexBuffer.AddUninitialized(NumIndices);
	for (UINT i = 0; i < NumElelments; ++i) {
		 IndexBuffer[i] = i;
	}
	FRHIResourceCreateInfo CreateInfo(TEXT("FSimple3DIndexBuffer"), &IndexBuffer);
	IndexBufferRHI = RHICmdList.CreateIndexBuffer(sizeof(UINT), IndexBuffer.GetResourceDataSize(), BUF_Static | BUF_ShaderResource | BUF_UnorderedAccess, CreateInfo);
	SRV = RHICmdList.CreateShaderResourceView(IndexBufferRHI, sizeof(UINT), PF_R32_UINT);
	UAV = RHICmdList.CreateUnorderedAccessView(IndexBufferRHI, PF_R32_UINT);
}

 void FGaussSplatIndexBuffer::ReleaseRHI()
 {
	 UAV.SafeRelease();
	 SRV.SafeRelease();
	
	 FIndexBuffer::ReleaseRHI();
 }


/*
*  FGSSortedIndexBuffer
*/
void FGSSortedIndexBuffer::InitRHI(FRHICommandListBase& RHICmdList)
{
	if (!NumElelments) return;

	TResourceArray<UINT, INDEXBUFFER_ALIGNMENT> IndexBuffer;
	uint32 NumIndices = NumElelments;
	IndexBuffer.AddUninitialized(NumIndices);
	for (UINT i = 0; i < NumElelments; ++i) {
		IndexBuffer[i] = i;
	}

	for (int32 BufferIndex = 0; BufferIndex < 2; ++BufferIndex)
	{
		FRHIResourceCreateInfo CreateInfo(TEXT("GSSortedIndexBuffer"), &IndexBuffer);
		IndexBuffers[BufferIndex] = RHICmdList.CreateIndexBuffer(
			sizeof(UINT), 
			IndexBuffer.GetResourceDataSize(), 
			BUF_Static | BUF_ShaderResource | BUF_UnorderedAccess, 
			CreateInfo);
		IndexBufferSRVs[BufferIndex] = RHICmdList.CreateShaderResourceView(
			IndexBuffers[BufferIndex],
			/*Stride=*/ sizeof(uint32),
			/*Format=*/ PF_R32_UINT);
		IndexBufferUAVs[BufferIndex] = RHICmdList.CreateUnorderedAccessView(
			IndexBuffers[BufferIndex],
			/*Format=*/ PF_R32_UINT);
	}

}

void FGSSortedIndexBuffer::ReleaseRHI()
{
	for (int32 BufferIndex = 0; BufferIndex < 2; ++BufferIndex)
	{
		IndexBufferUAVs[BufferIndex].SafeRelease();
		IndexBufferSRVs[BufferIndex].SafeRelease();
		IndexBuffers[BufferIndex].SafeRelease();
	}
}




/*
*  FGSSortedKeyBuffer
*/
void FGSSortedKeyBuffer::InitRHI(FRHICommandListBase& RHICmdList)
{
	if (!NumElelments) return;

	const int32 OffsetsBufferSize = NumElelments * sizeof(uint32);

	for (int32 BufferIndex = 0; BufferIndex < 2; ++BufferIndex)
	{
		FRHIResourceCreateInfo CreateInfo(TEXT("GSSortedKeyBuffer"));
		KeyBuffers[BufferIndex] = RHICmdList.CreateVertexBuffer(
			OffsetsBufferSize,
			BUF_Static | BUF_ShaderResource | BUF_UnorderedAccess,
			CreateInfo);
		KeyBufferSRVs[BufferIndex] = RHICmdList.CreateShaderResourceView(
			KeyBuffers[BufferIndex],
			/*Stride=*/ sizeof(uint32),
			/*Format=*/ PF_R32_UINT);
		KeyBufferUAVs[BufferIndex] = RHICmdList.CreateUnorderedAccessView(
			KeyBuffers[BufferIndex],
			/*Format=*/ PF_R32_UINT);
	}

}

void FGSSortedKeyBuffer::ReleaseRHI()
{
	for (int32 BufferIndex = 0; BufferIndex < 2; ++BufferIndex)
	{
		KeyBufferUAVs[BufferIndex].SafeRelease();
		KeyBufferSRVs[BufferIndex].SafeRelease();
		KeyBuffers[BufferIndex].SafeRelease();
	}
}






 /*
 *  AGSActor
 */
AGSActor::AGSActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Default Scene Root"));
	RootComponent->SetWorldScale3D(FVector(1.0f));

	WVPMat.SetIdentity();
	PreWVPMat.SetIdentity();
}

AGSActor::~AGSActor()
{
	ReleaseBuffers();
}

bool AGSActor::ShouldTickIfViewportsOnly() const
{
	return true;
}

void AGSActor::PostLoad()
{
	Super::PostLoad();

	if (!PlyFileName.IsEmpty()) {
		AGSActor* self = this;
		ENQUEUE_RENDER_COMMAND(AGSActor_PostLoad)(
			[self](FRHICommandListImmediate& RHICmdList)
			{
				self->ReleaseBuffers();

				// create GS buffer form ply file
				self->CreateVBFromPlyFile(RHICmdList, self->PlyFileName);
				
				// initialize sorted index buffer 
				self->SortedIndexBuffer.NumElelments = self->NumParticles;
				self->SortedIndexBuffer.InitRHI(RHICmdList);

				// initialize sorted key buffer 
				self->SortedKeyBuffer.NumElelments = self->NumParticles;
				self->SortedKeyBuffer.InitRHI(RHICmdList);	
			});
	}
}

void AGSActor::PreInitializeComponents()
{
	Super::PreInitializeComponents();
}


void AGSActor::PostInitProperties()
{
	Super::PostInitProperties();
}

void AGSActor::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
}

void AGSActor::PostActorCreated()
{
	Super::PostActorCreated();
}

void AGSActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	UGSShaderWorldSubsystem* subsystem = GetWorld()->GetSubsystem<UGSShaderWorldSubsystem>();
	if (subsystem) {
		subsystem->RegisterActor(this);
	}
	else {
		UE_LOG(LogGSActor, Warning, TEXT("AGSActor::OnConstruction : Failed to register component to manager"));
	}
}

#if WITH_EDITOR
void AGSActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

// Called when the game starts or when spawned
void AGSActor::BeginPlay()
{
	Super::BeginPlay();
	
	UGSShaderWorldSubsystem* subsystem = GetWorld()->GetSubsystem<UGSShaderWorldSubsystem>();
	if (subsystem) {
		subsystem->RegisterActor(this);
	}
	else {
		UE_LOG(LogGSActor, Warning, TEXT("Failed to register component to manager"));
	}
	
}

// Called every frame
void AGSActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (NumParticles != 0) {
	//	FLevelEditorViewportClient* vp = GCurrentLevelEditingViewportClient;
	//	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(vp->Viewport, vp->GetScene(), vp->EngineShowFlags));
	//	FSceneView* View = vp->CalcSceneView(&ViewFamily);
	//	FMatrix wvpMat = GetActorTransform().ToMatrixWithScale() * View->ViewMatrices.GetViewProjectionMatrix();
	
		if (WVPMat.Equals(PreWVPMat)) {
			return;
		}
		PreWVPMat = WVPMat;

		AGSActor* self = this;
		ENQUEUE_RENDER_COMMAND(AGSActor_Tick)(
			[self](FRHICommandListImmediate& RHICmdList)
			{
				self->SortedCount = self->SortKeyGen.Execute_RenderThread( RHICmdList
																		 , self->NumParticles
																		 , (FMatrix44f)self->WVPMat
																		 , self->PosVBSRV
																		 , self->SortedKeyBuffer.KeyBufferUAVs[0]
																		 , self->SortedIndexBuffer.IndexBufferUAVs[0] );
				FGPUSortBuffers SortBuffers;
				for (int32 BufferIndex = 0; BufferIndex < 2; ++BufferIndex)
				{
					SortBuffers.RemoteKeySRVs[BufferIndex] = self->SortedKeyBuffer.KeyBufferSRVs[BufferIndex];
					SortBuffers.RemoteKeyUAVs[BufferIndex] = self->SortedKeyBuffer.KeyBufferUAVs[BufferIndex];
					SortBuffers.RemoteValueSRVs[BufferIndex] = self->SortedIndexBuffer.IndexBufferSRVs[BufferIndex];
					SortBuffers.RemoteValueUAVs[BufferIndex] = self->SortedIndexBuffer.IndexBufferUAVs[BufferIndex];
				}
	
				self->ResultBufferIndex = SortGPUBuffers(RHICmdList, SortBuffers, 0, 0xFFFFFFFF, self->SortedCount, ERHIFeatureLevel::Type::SM6);
			});
	
	//	FString msg = FString::Printf(TEXT("%d"), SortedCount);
	//	UKismetSystemLibrary::PrintString(this, msg, true, true, FColor::Cyan, 2.f, TEXT("None"));
	}
}

void AGSActor::ReleaseBuffers()
{
	NumParticles = 0;

	SH04VB.ReleaseRHI();

	for (int i = 0; i < 3; ++i) {
		R_Sh1_4VB[i].ReleaseRHI();
		G_Sh1_4VB[i].ReleaseRHI();
		B_Sh1_4VB[i].ReleaseRHI();
	}

	PosVB.ReleaseRHI();
	RotVB.ReleaseRHI();
	SclVB.ReleaseRHI();

	R_Sh0VB.ReleaseRHI();
	G_Sh0VB.ReleaseRHI();
	B_Sh0VB.ReleaseRHI();

	SortedIndexBuffer.ReleaseRHI();
	SortedKeyBuffer.ReleaseRHI();
}

void AGSActor::CreateVBFromPlyFile(FRHICommandListBase& RHICmdList, const FString& Filename)
{
	check(IsInRenderingThread());

	PLY::FGaussSplatVertex GSData;

	if (PLY::LoadPlyFile(Filename, GSData)) {
		NumParticles = GSData.NumParticles;

		{
			FRHIResourceCreateInfo CreateInfo(TEXT("FPositionVB"), &GSData.pos);
			PosVB.VertexBufferRHI = RHICmdList.CreateVertexBuffer(GSData.pos.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
			PosVBSRV = RHICmdList.CreateShaderResourceView(PosVB.VertexBufferRHI, sizeof(FVector4f), PF_A32B32G32R32F);
		}

		{
			FRHIResourceCreateInfo CreateInfo(TEXT("FRotationVB"), &GSData.rot);
			RotVB.VertexBufferRHI = RHICmdList.CreateVertexBuffer(GSData.rot.GetResourceDataSize(), BUF_Static, CreateInfo);
		}

		{
			FRHIResourceCreateInfo CreateInfo(TEXT("FscaleVB"), &GSData.scl);
			SclVB.VertexBufferRHI = RHICmdList.CreateVertexBuffer(GSData.scl.GetResourceDataSize(), BUF_Static, CreateInfo);
		}


		{
			TResourceArray<FVector4f, VERTEXBUFFER_ALIGNMENT> sh0;
			sh0.SetNumUninitialized(NumParticles * 3);

			for (UINT i = 0; i < NumParticles; ++i) {
				sh0[i * 3 + 0] = GSData.r_sh0[i];
				sh0[i * 3 + 1] = GSData.g_sh0[i];
				sh0[i * 3 + 2] = GSData.b_sh0[i];
			}

			FRHIResourceCreateInfo CreateInfo(TEXT("SH04VB"), &sh0);
			SH04VB.VertexBufferRHI = RHICmdList.CreateVertexBuffer(sh0.GetResourceDataSize(), BUF_Static, CreateInfo);
		}


		{
			FRHIResourceCreateInfo CreateInfo(TEXT("R_SH0VB"), &GSData.r_sh0);
			R_Sh0VB.VertexBufferRHI = RHICmdList.CreateVertexBuffer(GSData.r_sh0.GetResourceDataSize(), BUF_Static, CreateInfo);
		}

		{
			FRHIResourceCreateInfo CreateInfo(TEXT("G_SH0VB"), &GSData.g_sh0);
			G_Sh0VB.VertexBufferRHI = RHICmdList.CreateVertexBuffer(GSData.g_sh0.GetResourceDataSize(), BUF_Static, CreateInfo);
		}

		{
			FRHIResourceCreateInfo CreateInfo(TEXT("B_SH0VB"), &GSData.b_sh0);
			B_Sh0VB.VertexBufferRHI = RHICmdList.CreateVertexBuffer(GSData.b_sh0.GetResourceDataSize(), BUF_Static, CreateInfo);
		}

		for (int i = 0; i < 3; ++i) {
			{
				FRHIResourceCreateInfo CreateInfo(TEXT("R_SH1_4VB"), &GSData.r_sh1_4[i]);
				R_Sh1_4VB[i].VertexBufferRHI = RHICmdList.CreateVertexBuffer(GSData.r_sh1_4[i].GetResourceDataSize(), BUF_Static, CreateInfo);
			}

			{
				FRHIResourceCreateInfo CreateInfo(TEXT("G_SH1_4VB"), &GSData.g_sh1_4[i]);
				G_Sh1_4VB[i].VertexBufferRHI = RHICmdList.CreateVertexBuffer(GSData.g_sh1_4[i].GetResourceDataSize(), BUF_Static, CreateInfo);
			}

			{
				FRHIResourceCreateInfo CreateInfo(TEXT("B_SH0VB"), &GSData.b_sh1_4[i]);
				B_Sh1_4VB[i].VertexBufferRHI = RHICmdList.CreateVertexBuffer(GSData.b_sh1_4[i].GetResourceDataSize(), BUF_Static, CreateInfo);
			}
		}

	}
}

void AGSActor::Render(FRDGBuilder& GraphBuilder, const FSceneView& inView, const FScreenPassTexture& SceneTexture)
{
	check(IsInRenderingThread());
	AGSActor* self = this;


//	FMatrix wvpMat = GetActorTransform().ToMatrixWithScale() * inView.ViewMatrices.GetViewProjectionMatrix();
//	AddPass(GraphBuilder
//		, RDG_EVENT_NAME("RadixSort")
//		, [self, wvpMat](FRHICommandList& RHICmdList)
//		{
//			self->SortedCount = self->SortKeyGen.Execute_RenderThread(RHICmdList
//				, self->NumParticles
//				, (FMatrix44f)wvpMat
//				, self->PosVBSRV
//				, self->SortedKeyBuffer.KeyBufferUAVs[0]
//				, self->SortedIndexBuffer.IndexBufferUAVs[0]);
//
//			FGPUSortBuffers SortBuffers;
//			for (int32 BufferIndex = 0; BufferIndex < 2; ++BufferIndex)
//			{
//				SortBuffers.RemoteKeySRVs[BufferIndex] = self->SortedKeyBuffer.KeyBufferSRVs[BufferIndex];
//				SortBuffers.RemoteKeyUAVs[BufferIndex] = self->SortedKeyBuffer.KeyBufferUAVs[BufferIndex];
//				SortBuffers.RemoteValueSRVs[BufferIndex] = self->SortedIndexBuffer.IndexBufferSRVs[BufferIndex];
//				SortBuffers.RemoteValueUAVs[BufferIndex] = self->SortedIndexBuffer.IndexBufferUAVs[BufferIndex];
//			}
//
//			self->ResultBufferIndex = SortGPUBuffers(RHICmdList, SortBuffers, 0, 0xFFFFFFFF, self->SortedCount, ERHIFeatureLevel::Type::SM6);
//		});
//


	WVPMat = GetActorTransform().ToMatrixWithScale() * inView.ViewMatrices.GetViewProjectionMatrix();

	if (NumParticles == 0 || SortedCount == 0)
		return;

	RDG_EVENT_SCOPE(GraphBuilder, "GSActor");
	RDG_GPU_STAT_SCOPE(GraphBuilder, GSActor);

	const FViewInfo& ViewInfo = static_cast<const FViewInfo&>(inView);
	const FSceneTextures& SceneTextures = ViewInfo.GetSceneTextures();

	FIntRect ViewRect2 = ViewInfo.ViewRect;
//	FIntRect ViewRect2 = static_cast<const FViewInfo&>(inView).ViewRect;
	FIntRect ViewRect = SceneTexture.ViewRect;

	FTriangleVS::FParameters* VSParams = GraphBuilder.AllocParameters<FTriangleVS::FParameters>();
//	VSParams->View = ViewInfo.ViewUniformBuffer;
	VSParams->gPreViewTranslation3 = (FVector3f)inView.ViewMatrices.GetPreViewTranslation();
	VSParams->gViewSize2 = FVector2f(ViewRect2.Max.X, ViewRect2.Max.Y);
	VSParams->gLocal2World4x4 = (FMatrix44f)GetActorTransform().ToMatrixWithScale();
	VSParams->gTranslatedWorld2View4x4 = (FMatrix44f)inView.ViewMatrices.GetTranslatedViewMatrix();
	VSParams->gView2Clip4x4 = (FMatrix44f)inView.ViewMatrices.GetProjectionNoAAMatrix();
	VSParams->gTranslatedWorldToClip4x4 = (FMatrix44f)(inView.ViewMatrices.GetTranslatedViewMatrix() * inView.ViewMatrices.GetProjectionNoAAMatrix());

	FTriangleGS::FParameters* GSParams = GraphBuilder.AllocParameters<FTriangleGS::FParameters>();
	GSParams->gViewSize2 = FVector2f(ViewRect2.Max.X, ViewRect2.Max.Y);

	FTrianglePS::FParameters* PSParams = GraphBuilder.AllocParameters<FTrianglePS::FParameters>();
	PSParams->RenderTargets[0] = FRenderTargetBinding(SceneTexture.Texture, ERenderTargetLoadAction::ELoad);
//	PSParams->RenderTargets[0] = FRenderTargetBinding(SceneTextures.GBufferC, ERenderTargetLoadAction::ELoad);
//	PSParams->RenderTargets.DepthStencil = FDepthStencilBinding(SceneTextures.Depth.Target, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthRead_StencilNop);


	const FGlobalShaderMap* ViewShaderMap = static_cast<const FViewInfo&>(inView).ShaderMap;
	TShaderMapRef<FTriangleVS> VertexShader(ViewShaderMap);
	TShaderMapRef<FTrianglePS> PixelShader(ViewShaderMap);
	TShaderMapRef<FTriangleGS> GeometryShader(ViewShaderMap);

	ERDGPassFlags::Readback;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("Gaussian Splatting")
		, PSParams
		, ERDGPassFlags::Raster
		, [self, ViewRect, VertexShader, GeometryShader, PixelShader, VSParams, GSParams, PSParams](FRHICommandList& RHICmdList)
		{
			RHICmdList.SetViewport((float)ViewRect.Min.X, (float)ViewRect.Min.Y, 0.0f, (float)ViewRect.Max.X, (float)ViewRect.Max.Y, 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;

			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		//	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI();
		//	GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();

		//	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		//	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();
			

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GTriangleVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.BoundShaderState.SetGeometryShader(GeometryShader.GetGeometryShader());

			GraphicsPSOInit.PrimitiveType = PT_PointList;
	
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PSParams);
			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), *VSParams);
			SetShaderParameters(RHICmdList, GeometryShader, GeometryShader.GetGeometryShader(), *GSParams);

			RHICmdList.SetStreamSource(0, self->PosVB.VertexBufferRHI, 0);
			RHICmdList.SetStreamSource(1, self->RotVB.VertexBufferRHI, 0);
			RHICmdList.SetStreamSource(2, self->SclVB.VertexBufferRHI, 0);
			
			RHICmdList.SetStreamSource(3, self->SH04VB.VertexBufferRHI, 0);

		//	RHICmdList.SetStreamSource(3, self->R_Sh0VB.VertexBufferRHI, 0);
		//	RHICmdList.SetStreamSource(4, self->G_Sh0VB.VertexBufferRHI, 0);
		//	RHICmdList.SetStreamSource(5, self->B_Sh0VB.VertexBufferRHI, 0);

			for (int i = 0; i < 3; ++i) {
				RHICmdList.SetStreamSource(6+i*3+0, self->R_Sh1_4VB[i].VertexBufferRHI, 0);
				RHICmdList.SetStreamSource(6+i*3+1, self->G_Sh1_4VB[i].VertexBufferRHI, 0);
				RHICmdList.SetStreamSource(6+i*3+2, self->B_Sh1_4VB[i].VertexBufferRHI, 0);
			}

			RHICmdList.DrawIndexedPrimitive(
				self->SortedIndexBuffer.IndexBuffers[self->ResultBufferIndex],
				/*BaseVertexIndex=*/ 0,
				/*MinIndex=*/ 0,
				/*NumVertices=*/ self->NumParticles,
				/*StartIndex=*/ 0,
				/*NumPrimitives=*/ self->SortedCount,
				/*NumInstances=*/ 1);
		});

}
