#include "EnginePCH.h"
#include "AssetManager.h"
#include "Render/Buffer.h"
#include "Render/Material.h"

#include "Render/Vertex.h"
#include "Render/Texture2D.h"
#include "Render/RenderCommand.h"

#include "Render/RenderResourceManager.h"

#include "Asset/ObjImporter/ObjImporter.h"
#include "Asset/FbxImporter/FbxImporter.h"
#include "Asset/MeshAssetFile.h"
#include "Component/SkinnedMeshComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "UObject/UObjectIterator.h"

#include "Render/GeometryGenerator.h"
#include "UObject/UObjectGlobals.h"

#include "Render/ImageLoader.h"
#include "Core/ResourcePaths.h"

#include "Sequence/LevelSequence.h"
#include "Serialization/JsonArchive.h"

namespace
{
	void SetMaterialTexture(int32 SlotIndex, UMaterial* Material,
		const FString& TexturePath, bool bGenerateMipmaps = true, bool bSRGB = true)
	{
		while (static_cast<int32>(Material->Textures.Num()) <= SlotIndex)
		{
			Material->Textures.Add(nullptr);
		}
		if (!TexturePath.empty())
		{
			UTexture2D* Texture = UAssetManager::Get().LoadTexture(TexturePath, bGenerateMipmaps, bSRGB);
			Material->Textures[SlotIndex] = Texture;
		}
	}

	// MaterialSlots(쿡 결과)를 실제 UMaterial로 바꾼다. 스태틱,스켈레탈 공통
	bool CreateSlotMaterials(const TArray<FStaticMaterialSlot>& Slots, TArray<UMaterial*>& OutMaterials)
	{
		UMaterial* DefaultMaterial = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
		if (!DefaultMaterial)
		{
			return false;
		}

		for (const FStaticMaterialSlot& Slot : Slots)
		{
			UMaterial* Material = UMaterial::CreateInstance(DefaultMaterial);
			if (!Material)
			{
				return false;
			}
			Material->BaseColor = Slot.BaseColor;
			Material->Shininess = Slot.Shininess;
			Material->SpecularColor = Slot.SpecularColor;
			if (Material->BaseColor.A < 1.0f)
			{
				Material->BlendState = EBlendState::AlphaBlend;
				Material->DepthStencilState = EDepthStencilState::ReadOnly;
			}

			if (!Slot.DiffuseTexturePath.empty())
			{
				UTexture2D* Texture = UAssetManager::Get().LoadTexture(Slot.DiffuseTexturePath);

				if (Texture)
				{
					Material->Textures[0] = Texture;
				}
			}
			SetMaterialTexture(1, Material, Slot.NormalTexturePath, true, false);
			OutMaterials.Add(Material);
		}
		return true;
	}

	// 새 데이터와 GPU 자원을 모두 만든 뒤 마지막에 교체한다. 실패하면 Mesh는 그대로다 (리임포트가 기존 메시에 쓴다)
	bool FillStaticMesh(UStaticMesh& Mesh, const FStaticMeshData* MeshData)
	{
		if (!MeshData || !MeshData->Vertices.Num() || !MeshData->Indices.Num())
			return false;

		FStaticMeshData Optimized = *MeshData;

		// Mesh에 MaterialSlot 없을 경우 Default Material 할당
		if (Optimized.MaterialSlots.IsEmpty())
		{
			FStaticMaterialSlot DefaultSlot;
			DefaultSlot.Name = "Default";
			Optimized.MaterialSlots.Add(DefaultSlot);
		}

		// Mesh에 Section이 없을 경우 전체 메쉬을 섹션 하나로 세팅
		if (Optimized.Sections.IsEmpty())
		{
			FStaticMeshSection DefaultSection;
			DefaultSection.StartIndex = 0;
			DefaultSection.IndexCount = static_cast<uint32>(Optimized.Indices.Num());
			DefaultSection.MaterialSlotIndex = 0;

			Optimized.Sections.Add(DefaultSection);
		}

		Optimized.OptimizeTriangleOrderForVertexCache();
		TUniquePtr<FVertexBuffer> VB = RenderCommand::CreateStaticVertexBuffer(Optimized.Vertices.GetData(), sizeof(FVertexPNCT) * static_cast<uint32>(Optimized.Vertices.size()), sizeof(FVertexPNCT));
		TUniquePtr<FIndexBuffer> IB = RenderCommand::CreateStaticIndexBuffer(Optimized.Indices.GetData(), static_cast<uint32>(Optimized.Indices.size()));
		if (VB == nullptr || IB == nullptr) return false;

		TArray<UMaterial*> Materials;
		if (!CreateSlotMaterials(Optimized.MaterialSlots, Materials))
		{
			return false;
		}

		Mesh.MeshData = std::move(Optimized);

		// Vertex/Index GPU 업로드
		Mesh.VertexBuffer = std::move(VB);
		Mesh.IndexBuffer = std::move(IB);
		Mesh.Materials = std::move(Materials);
		Mesh.AdditionalLODs.Reset();   // LOD는 옛 정점으로 만들어졌다

		Mesh.MeshData.BuildTriangleBVH();
		Mesh.RebuildRenderData();

		return true;
	}

	UStaticMesh* CreateStaticMesh(const FStaticMeshData* MeshData)
	{
		UStaticMesh* Mesh = NewObject<UStaticMesh>();
		if (!FillStaticMesh(*Mesh, MeshData))
		{
			delete Mesh;
			return nullptr;
		}
		return Mesh;
	}

	// 스켈레탈 에셋은 정점 버퍼를 만들지 않는다 (포즈마다 달라서 컴포넌트의 FSkeletalMeshObject가 만든다).
	// 인덱스 버퍼와 머티리얼만 만든다. 실패하면 Mesh는 그대로다
	bool FillSkeletalMesh(USkeletalMesh& Mesh, TUniquePtr<FSkeletalMeshData> MeshData)
	{
		if (!MeshData || MeshData->Vertices.IsEmpty() || MeshData->Indices.IsEmpty() || MeshData->Skeleton.Bones.IsEmpty())
			return false;

		TUniquePtr<FIndexBuffer> IB = RenderCommand::CreateStaticIndexBuffer(MeshData->Indices.GetData(), static_cast<uint32>(MeshData->Indices.Num()));
		if (IB == nullptr) return false;

		TArray<UMaterial*> Materials;
		if (!CreateSlotMaterials(MeshData->MaterialSlots, Materials))
		{
			return false;
		}

		Mesh.MeshData = std::move(*MeshData);
		Mesh.IndexBuffer = std::move(IB);
		Mesh.Materials = std::move(Materials);
		++Mesh.Revision;
		return true;
	}

	USkeletalMesh* CreateSkeletalMesh(TUniquePtr<FSkeletalMeshData> MeshData)
	{
		USkeletalMesh* Mesh = NewObject<USkeletalMesh>();
		if (!FillSkeletalMesh(*Mesh, std::move(MeshData)))
		{
			delete Mesh;
			return nullptr;
		}
		return Mesh;
	}

	FString MakeAssetKey(const FString& Path)
	{
		return fs::relative(Path, "Assets").generic_string();
	}

	bool IsInsideAssetRoot(const fs::path& Path)
	{
		std::error_code Error;
		const fs::path Root = fs::weakly_canonical("Assets", Error);
		const fs::path Full = fs::weakly_canonical(Path, Error);
		if (Error)
		{
			return false;
		}
		return std::mismatch(Root.begin(), Root.end(), Full.begin(), Full.end()).first == Root.end();
	}

	// Assets 밖의 텍스처를 메시 옆 폴더로 복사하고 슬롯 경로를 바꾼다
	// 원본이 프로젝트 밖에 있거나 나중에 사라져도, 팀원 PC에서도 에셋이 같은 텍스처를 찾게 된다
	// bOverwrite: 리임포트는 지난번에 복사한 텍스처를 새 파일로 덮어쓴다 (안 그러면 할 때마다 _1, _2가 쌓인다)
	void CopyExternalTextures(TArray<FStaticMaterialSlot>& Slots, const FString& AssetPath, const bool bOverwrite = false)
	{
		// humanoid.fbx.hasset -> humanoid_Textures
		const fs::path SourceName = fs::path(FMeshAssetFile::StripExtension(AssetPath)).filename();
		const fs::path TextureFolder = fs::path(AssetPath).parent_path() / (SourceName.stem().string() + "_Textures");

		TMap<FString, FString> Copied;   // 원본 경로 -> 복사된 경로. 여러 슬롯이 같은 텍스처를 쓴다
		const auto Localize = [&](FString& TexturePath)
		{
			if (TexturePath.empty() || IsInsideAssetRoot(TexturePath))
				return;

			if (const FString* Found = Copied.Find(TexturePath))
			{
				TexturePath = *Found;
				return;
			}

			std::error_code Error;
			fs::create_directories(TextureFolder, Error);

			// 다른 폴더의 같은 이름 텍스처가 서로 덮어쓰지 않게 한다
			const fs::path FileName = fs::path(TexturePath).filename();
			fs::path Destination = TextureFolder / FileName;
			const auto IsTaken = [&](const fs::path& Candidate)
			{
				const FString CandidatePath = Candidate.generic_string();
				for (const auto& [Source, Local] : Copied)
				{
					if (Local == CandidatePath)
						return true;
				}
				return !bOverwrite && fs::exists(Candidate, Error);
			};
			for (int32 Suffix = 1; IsTaken(Destination); ++Suffix)
			{
				Destination = TextureFolder / std::format("{}_{}{}", FileName.stem().string(), Suffix, FileName.extension().string());
			}

			fs::copy_file(TexturePath, Destination, fs::copy_options::overwrite_existing, Error);
			if (Error)
			{
				HTR_LOG(Warning, "[IMPORT] cannot copy texture '{}': {}", TexturePath, Error.message());
				return;
			}

			const FString LocalPath = Destination.generic_string();
			Copied.Add(TexturePath, LocalPath);
			TexturePath = LocalPath;
		};

		for (FStaticMaterialSlot& Slot : Slots)
		{
			Localize(Slot.DiffuseTexturePath);
			Localize(Slot.NormalTexturePath);
		}
	}
}

UAssetManager& UAssetManager::Get()
{
	static UAssetManager* Instance = NewObject<UAssetManager>();
	return *Instance;
}

void UAssetManager::Init(const FAssetLoadProgress& OnProgress)
{
	FGeometryGenerator::CreateDefaultMeshDatas();
	// 머티리얼이 참조하므로 반드시 먼저 만든다
	Get().CreateDefaultTextures();
	Get().CreateDefaultMaterial();
	Get().LoadFontAtlas(ResourcePaths::DefaultFont, ResourcePaths::DefaultFontAtlas);
	Get().ScanAssets("Assets", OnProgress);
	Get().CreateDefaultMeshes();
	Get().CreateParticleMaterial();
	Get().CreateBillboardMaterial();
}

void UAssetManager::ScanAssets(const fs::path& AssetRoot, const FAssetLoadProgress& OnProgress)
{
	if (!fs::exists(AssetRoot))
	{
		// Assets는 사용자 콘텐츠라 없어도 엔진은 Resources만으로 뜬다
		HTR_LOG(Warning, "Asset root not found: {}", AssetRoot.generic_string());
		return;
	}

	TArray<fs::path> Files;
	for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(AssetRoot))
	{
		if (Entry.is_regular_file())
		{
			Files.Add(Entry.path());
		}
	}

	const int32 Total = Files.Num();
	for (int32 i = 0; i < Total; ++i)
	{
		FString Key = fs::relative(Files[i], AssetRoot).generic_string();
		FString Path = Files[i].generic_string();
		AssetPathMap.Add(Key, Path);
		LoadAsset(Key, Path);

		if (OnProgress)
		{
			OnProgress(i + 1, Total, Key);
		}
	}
}

void UAssetManager::LoadAsset(const FString& Key, const FString& Path)
{
	FString Extension = fs::path(Path).extension().string();
	if (Extension == ".png")
	{
		if (UTexture2D* Texture = LoadTexture(Path))
		{
			AssetMap[Key] = Texture;
		}
	}
	if (Extension == ".json")
	{
		FString TexturePath = fs::path(Path).replace_extension(".png").string();
		if (UFont* Font = LoadFontAtlas(Path, TexturePath))
		{
			AssetMap[Key] = Font;
		}
	}
	if (Extension == ".obj")
	{
		// 임포트한 에셋이 있으면 그것을 쓴다. 없으면 코드와 씬이 참조하는 메시가 계속 로드되도록 자동으로 읽는다
		if (!LoadMeshAsset(FMeshAssetFile::MakeAssetPath(Path)))
		{
			LoadObjStaticMesh(Path);
		}
	}
	if (Extension == ".sequence")
	{
		LoadLevelSequence(Path);
	}

	// 메시 파일은 대소문자가 섞여 들어온다 (.FBX)
	FString LowerExtension = Extension;
	std::transform(LowerExtension.begin(), LowerExtension.end(), LowerExtension.begin(),
		[](const unsigned char Character) { return static_cast<char>(std::tolower(Character)); });

	// 원본이 없어도 이 파일만으로 로드된다
	if (LowerExtension == FMeshAssetFile::Extension)
	{
		LoadMeshAsset(Path);
	}
	if (LowerExtension == ".fbx")
	{
		// FBX는 옵션을 골라야 하므로 여기서 파싱하지 않는다. 임포트 결과가 없거나 읽지 못하면(버전 차이, 손상)
		// 목록에 남겨 에디터가 임포트 창을 띄우게 한다. 게임은 창이 없으므로 이 메시 없이 시작한다
		if (!LoadMeshAsset(FMeshAssetFile::MakeAssetPath(Path)))
		{
			HTR_LOG(Warning, "FBX not imported: {}", Path);
			UnimportedFbxPaths.Add(Path);
		}
	}
}

void UAssetManager::CreateDefaultTextures()
{
	// 텍스처가 지정되지 않은 머티리얼이 검게 나오지 않도록 하는 1x1 흰색 텍스처.
	// 셰이더에서 곱해도 결과가 변하지 않으므로 "텍스처 없음"의 기본값으로 쓴다.
	const uint32 WhitePixel = 0xFFFFFFFF;

	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = 1;
	Desc.Height = 1;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_IMMUTABLE;
	Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	TUniquePtr<FTexture2D> Resource = RenderCommand::CreateTexture2D(Desc, &WhitePixel);
	if (!Resource)
	{
		return;
	}

	UTexture2D* WhiteTexture = NewObject<UTexture2D>();
	WhiteTexture->SetResource(std::move(Resource));
	WhiteTexture->SetPath("WhiteTexture");

	AssetMap["WhiteTexture"] = WhiteTexture;

	// 노멀맵이 지정되지 않은 머티리얼을 위한 1x1 평면 노멀맵 텍스처.
	// uint32 리터럴로 쓰면 리틀 엔디안 때문에 RGBA 순서가 뒤집히므로 바이트 순서대로 적는다
	const uint8 FlatNormalPixel[4] = { 128, 128, 255, 255 }; // RGBA => (0.5, 0.5, 1.0)

	D3D11_TEXTURE2D_DESC NormalDesc{};
	NormalDesc.Width = 1;
	NormalDesc.Height = 1;
	NormalDesc.MipLevels = 1;
	NormalDesc.ArraySize = 1;
	NormalDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	NormalDesc.SampleDesc.Count = 1;
	NormalDesc.Usage = D3D11_USAGE_IMMUTABLE;
	NormalDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	TUniquePtr<FTexture2D> NormalResource = RenderCommand::CreateTexture2D(NormalDesc, FlatNormalPixel);
	if (!NormalResource)
	{
		return;
	}

	UTexture2D* FlatNormalTexture = NewObject<UTexture2D>();
	FlatNormalTexture->SetResource(std::move(NormalResource));
	FlatNormalTexture->SetPath("FlatNormalTexture");

	AssetMap["FlatNormalTexture"] = FlatNormalTexture;
}

void UAssetManager::CreateDefaultMeshes()
{
	// 메시 데이터 업로드
	FStaticMeshData* CubeData = FGeometryGenerator::GetMeshData("Cube");
	RegisterAsset("Cube", CreateStaticMesh(CubeData));

	FStaticMeshData* ConeData = FGeometryGenerator::GetMeshData("Cone");
	RegisterAsset("Cone", CreateStaticMesh(ConeData));

	FStaticMeshData* SphereData = FGeometryGenerator::GetMeshData("Sphere");
	RegisterAsset("Sphere", CreateStaticMesh(SphereData));

	FStaticMeshData* CylinderData = FGeometryGenerator::GetMeshData("Cylinder");
	RegisterAsset("Cylinder", CreateStaticMesh(CylinderData));

	FStaticMeshData* PlaneData = FGeometryGenerator::GetMeshData("Plane");
	RegisterAsset("Plane", CreateStaticMesh(PlaneData));

	FStaticMeshData* ArrowMeshData = FGeometryGenerator::GetMeshData("Arrow");
	RegisterAsset("Arrow", CreateStaticMesh(ArrowMeshData));

	FStaticMeshData* RingMeshData = FGeometryGenerator::GetMeshData("Ring");
	RegisterAsset("Ring", CreateStaticMesh(RingMeshData));

	FStaticMeshData* ScaleBarData = FGeometryGenerator::GetMeshData("ScaleBar");
	RegisterAsset("ScaleBar", CreateStaticMesh(ScaleBarData));

	FStaticMeshData* GizmoSphereData = FGeometryGenerator::GetMeshData("GizmoSphere");
	RegisterAsset("GizmoSphere", CreateStaticMesh(GizmoSphereData));

	const FParticleVertex Vertices[] =
	{
		{ FVector(0.0f, -0.5f,  0.5f), FVector2(0.0f, 0.0f) },
		{ FVector(0.0f, 0.5f,  0.5f), FVector2(1.0f, 0.0f) },
		{ FVector(0.0f, 0.5f, -0.5f), FVector2(1.0f, 1.0f) },
		{ FVector(0.0f, -0.5f, -0.5f), FVector2(0.0f, 1.0f) }
	};

	const uint32 Indices[] =
	{
		0, 1, 2,
		0, 2, 3
	};

	UStaticMesh* Mesh = NewObject<UStaticMesh>();
	Mesh->VertexBuffer = RenderCommand::CreateStaticVertexBuffer(
		Vertices,
		sizeof(Vertices), sizeof(FParticleVertex));
	Mesh->IndexBuffer = RenderCommand::CreateStaticIndexBuffer(
		Indices,
		ARRAYSIZE(Indices));

	FVertexPNCT Vertex{};
	Vertex.Position = FVector(0.0f, -0.5f, 0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);
	Vertex.Position = FVector(0.0f, 0.5f, 0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);
	Vertex.Position = FVector(0.0f, 0.5f, -0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);
	Vertex.Position = FVector(0.0f, -0.5f, -0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);

	Mesh->MeshData.Indices = { 0, 1, 2, 0, 2, 3, 0,2,1, 0,3,2 };

	Mesh->MeshData.AABB.Min = FVector(-0.001f, -0.5, -0.5);
	Mesh->MeshData.AABB.Max = FVector(0.001f, 0.5, 0.5);
	Mesh->RebuildRenderData();

	RegisterAsset("Quad", Mesh);
}

void UAssetManager::CreateDefaultMaterial()
{
	UMaterial* DefaultMat = NewObject<UMaterial>();
	DefaultMat->Shader = FRenderResourceManager::GetUberLitProgram(FViewRenderMode{}, false);
	DefaultMat->Textures.Add(GetAssetByPath<UTexture2D>("WhiteTexture"));
	 //DefaultMat->Textures.Add(GetAssetByPath<UTexture2D>("FlatNormalTexture"));

	DefaultMat->ParamLayout = EMaterialParamLayout::StaticMesh;
	DefaultMat->ParamBuffer = RenderCommand::CreateConstantBuffer(sizeof(FStaticMeshMaterialParams));
	RegisterAsset("DefaultMaterial", DefaultMat);
}

void UAssetManager::CreateParticleMaterial()
{
	UMaterial* ParticleMat = NewObject<UMaterial>();
	ParticleMat->Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/ParticleSubUVShader.hlsl");
	ParticleMat->Textures.Add(LoadTexture(ResourcePaths::DefaultParticleTexture));
	ParticleMat->BlendState = EBlendState::AlphaBlend;
	ParticleMat->DepthStencilState = EDepthStencilState::ReadOnly;
	ParticleMat->ParamLayout = EMaterialParamLayout::ParticleSubUV;
	ParticleMat->ParamBuffer = RenderCommand::CreateConstantBuffer(256);
	RegisterAsset("SubUVMaterial", ParticleMat);
}

void UAssetManager::CreateBillboardMaterial()
{
	UMaterial* BillboardMat = NewObject<UMaterial>();
	BillboardMat->Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/BillboardShader.hlsl");
	BillboardMat->Textures.Add(LoadTexture(ResourcePaths::DefaultParticleTexture));
	BillboardMat->BlendState = EBlendState::AlphaBlend;
	BillboardMat->DepthStencilState = EDepthStencilState::ReadOnly;
	BillboardMat->ParamLayout = EMaterialParamLayout::Billboard;
	BillboardMat->ParamBuffer = RenderCommand::CreateConstantBuffer(sizeof(FBillboardMaterialParams));
	RegisterAsset("BillboardMaterial", BillboardMat);
}

void UAssetManager::Shutdown()
{
	Get().AssetMap.Empty();
}


void UAssetManager::RegisterAsset(const FString& Key, UAsset* Asset)
{
	Asset->SetPath(Key);
	AssetMap[Key] = Asset;

	// 이 에셋이 없는 상태로 열린 씬이 있으면 원래 참조로 되돌린다
	AssetReferenceSerialization::ResolveMissing(Key, Asset);
}

FString UAssetManager::GetAssetKeyFromPath(const FString& Path)
{
	return MakeAssetKey(Path);
}

UTexture2D* UAssetManager::LoadTexture(const FString& InPath, bool bGenerateMips, bool bSRGB)
{
	if (UAsset** Found = AssetMap.Find(InPath))
	{
		return Cast<UTexture2D>(*Found);
	}

	FImageData Data = ImageLoader::LoadAuto(InPath);
	if (!Data.IsValid()) return nullptr;

	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = Data.Width;
	Desc.Height = Data.Height;
	Desc.ArraySize = 1;
	Desc.Format = Data.Format;              // 로더가 정한 포맷 (.hdr이면 float)
	// 8비트 이미지 파일은 sRGB로 저장돼 있다. .hdr(float)은 이미 Linear라 그대로 둔다.
	if (bSRGB && Desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM)
		Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	Desc.SampleDesc.Count = 1;
	Desc.MipLevels = bGenerateMips ? 0 : 1;
	Desc.Usage = bGenerateMips ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
	Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (bGenerateMips ? D3D11_BIND_RENDER_TARGET : 0);
	Desc.MiscFlags = bGenerateMips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;

	TUniquePtr<FTexture2D> Resource = RenderCommand::CreateTexture2D(Desc, Data);

	if (!Resource) return nullptr;

	UTexture2D* Asset = NewObject<UTexture2D>();
	Asset->SetResource(std::move(Resource));
	RegisterAsset(InPath, Asset);

	return Asset;
}

UFont* UAssetManager::LoadFontAtlas(const FString& JsonPath, const FString& AtlasTexturePath)
{
	if (UAsset** Found = AssetMap.Find(JsonPath))
	{
		return Cast<UFont>(*Found);
	}

	UFont* Font = NewObject<UFont>();

	if (!Font->LoadFontAtlasJson(JsonPath))
	{
		return nullptr;
	}

	// 폰트 아틀라스는 글자 커버리지 데이터라 감마 디코딩하지 않는다
	Font->AtlasTexture = LoadTexture(AtlasTexturePath, false, false);
	if (!Font->AtlasTexture)
	{
		return nullptr;
	}

	RegisterAsset(JsonPath, Font);
	return Font;
}

UStaticMesh* UAssetManager::LoadObjStaticMesh(const FString& Path)
{
	const FString Key = MakeAssetKey(Path);
	if (UStaticMesh* Cached = GetAssetByPath<UStaticMesh>(Key))
	{
		return Cached;
	}

	TUniquePtr<FStaticMeshData> Data = FObjImporter::LoadStaticMeshData(Path);
	UStaticMesh* Mesh = CreateStaticMesh(Data.get());   // Data가 nullptr이면 nullptr 반환
	if (!Mesh)
	{
		return nullptr;
	}

	Get().RegisterAsset(Key, Mesh);
	return Mesh;
}

ULevelSequence* UAssetManager::LoadLevelSequence(const FString& Path)
{
	const FString Key = MakeAssetKey(Path);
	if (ULevelSequence* Cached = GetAssetByPath<ULevelSequence>(Key))
	{
		return Cached;
	}

	ULevelSequence* LevelSequence = NewObject<ULevelSequence>();
	if (!FJsonArchive::LoadSequence(LevelSequence, Path))
	{
		HTR_LOG(Warning, "Can't load level sequence");
		delete LevelSequence;
		return nullptr;
	}

	Get().RegisterAsset(Key, LevelSequence);

	return LevelSequence;
}

bool UAssetManager::LoadMeshAsset(const FString& AssetPath)
{
	// 같은 폴더의 원본을 스캔할 때 이미 읽었을 수 있다
	const FString Key = MakeAssetKey(FMeshAssetFile::StripExtension(AssetPath));
	if (GetAssetByPath<UAsset>(Key))
	{
		return true;
	}

	if (!FMeshAssetFile::Exists(AssetPath))
	{
		return false;
	}

	FMeshAssetContents Contents;
	if (!FMeshAssetFile::Read(AssetPath, Contents))
	{
		return false;
	}

	UAsset* Mesh = nullptr;
	if (Contents.Info.Type == EMeshAssetType::Skeletal)
	{
		Mesh = CreateSkeletalMesh(std::move(Contents.SkeletalMesh));
	}
	else
	{
		Mesh = CreateStaticMesh(Contents.StaticMesh.get());
	}

	if (!Mesh)
	{
		return false;
	}

	Get().RegisterAsset(Key, Mesh);
	return true;
}

UAsset* UAssetManager::ImportMesh(const FString& SourcePath, const FString& AssetPath, const FFbxImportOptions& Option)
{
	const FString Key = MakeAssetKey(FMeshAssetFile::StripExtension(AssetPath));

	FString Extension = fs::path(SourcePath).extension().string();
	std::transform(Extension.begin(), Extension.end(), Extension.begin(),
		[](const unsigned char Character) { return static_cast<char>(std::tolower(Character)); });

	// 같은 키에 에셋은 하나뿐이다. 스킨이 있으면 스켈레탈로, 아니면(또는 실패하면) 스태틱으로 만든다
	if (Extension == ".fbx" && Option.bImportSkeletalMesh)
	{
		if (TUniquePtr<FSkeletalMeshData> Data = FFbxImporter::LoadSkeletalMeshData(SourcePath, Option))
		{
			CopyExternalTextures(Data->MaterialSlots, AssetPath);
			FMeshAssetFile::Write(AssetPath, SourcePath, Option, *Data);

			USkeletalMesh* Mesh = CreateSkeletalMesh(std::move(Data));
			if (Mesh)
			{
				Get().RegisterAsset(Key, Mesh);
			}
			return Mesh;
		}
	}

	TUniquePtr<FStaticMeshData> Data;
	if (Extension == ".obj")
	{
		Data = FObjImporter::ImportStaticMeshData(SourcePath);
	}
	else if (Extension == ".fbx" && Option.bImportStaticMesh)
	{
		Data = FFbxImporter::LoadStaticMeshData(SourcePath, Option);
	}
	if (!Data)
	{
		return nullptr;
	}

	CopyExternalTextures(Data->MaterialSlots, AssetPath);
	UStaticMesh* Mesh = CreateStaticMesh(Data.get());
	if (!Mesh)
	{
		return nullptr;
	}

	FMeshAssetFile::Write(AssetPath, SourcePath, Option, *Data);
	Get().RegisterAsset(Key, Mesh);
	return Mesh;
}

bool UAssetManager::ReimportMesh(const FString& SourcePath, const FString& AssetPath, const FFbxImportOptions& Option)
{
	UAsset* Existing = GetAssetByPath<UAsset>(MakeAssetKey(FMeshAssetFile::StripExtension(AssetPath)));

	FString Extension = fs::path(SourcePath).extension().string();
	std::transform(Extension.begin(), Extension.end(), Extension.begin(),
		[](const unsigned char Character) { return static_cast<char>(std::tolower(Character)); });

	// 포인터를 그대로 두고 내용만 바꾼다. 씬·컴포넌트·다른 에셋이 들고 있는 참조를 찾아다닐 필요가 없다
	// 그래서 종류는 바꿀 수 없다 (컴포넌트의 프로퍼티 타입이 메시 종류에 묶여 있다)
	if (USkeletalMesh* Skeletal = Cast<USkeletalMesh>(Existing))
	{
		if (Extension != ".fbx")
		{
			HTR_LOG(Warning, "[IMPORT] {}: a skeletal mesh can only be reimported from an FBX", AssetPath);
			return false;
		}

		FFbxImportOptions SkeletalOption = Option;
		SkeletalOption.bImportSkeletalMesh = true;
		TUniquePtr<FSkeletalMeshData> Data = FFbxImporter::LoadSkeletalMeshData(SourcePath, SkeletalOption);
		if (!Data)
		{
			return false;
		}

		CopyExternalTextures(Data->MaterialSlots, AssetPath, true);

		const int32 PreviousSlotCount = Skeletal->GetMeshData().MaterialSlots.Num();
		if (!FillSkeletalMesh(*Skeletal, std::move(Data)))
		{
			return false;
		}
		// Fill이 데이터를 옮겨 가서 메시 쪽을 저장한다. 실패해도 메모리의 메시는 이미 새것이라 컴포넌트는 갱신한다
		const bool bWritten = FMeshAssetFile::Write(AssetPath, SourcePath, SkeletalOption, Skeletal->GetMeshData());

		for (TObjectIterator<USkinnedMeshComponent> Component; Component; ++Component)
		{
			if (Component->GetSkeletalMesh() == Skeletal)
			{
				Component->OnMeshReimported(PreviousSlotCount);
			}
		}
		return bWritten;
	}

	if (UStaticMesh* Static = Cast<UStaticMesh>(Existing))
	{
		FFbxImportOptions StaticOption = Option;
		StaticOption.bImportStaticMesh = true;
		StaticOption.bImportSkeletalMesh = false;

		TUniquePtr<FStaticMeshData> Data;
		if (Extension == ".obj")
		{
			Data = FObjImporter::ImportStaticMeshData(SourcePath);
		}
		else if (Extension == ".fbx")
		{
			Data = FFbxImporter::LoadStaticMeshData(SourcePath, StaticOption);
		}
		if (!Data)
		{
			return false;
		}

		CopyExternalTextures(Data->MaterialSlots, AssetPath, true);

		const int32 PreviousSlotCount = Static->GetMeshData().MaterialSlots.Num();
		if (!FillStaticMesh(*Static, Data.get()))
		{
			return false;
		}
		const bool bWritten = FMeshAssetFile::Write(AssetPath, SourcePath, StaticOption, *Data);

		for (TObjectIterator<UStaticMeshComponent> Component; Component; ++Component)
		{
			if (Component->GetStaticMesh() == Static)
			{
				Component->OnMeshReimported(PreviousSlotCount);
			}
		}
		return bWritten;
	}

	// 파일이 깨져서 로드되지 못한 에셋이다. 바꿀 객체가 없으니 새로 임포트해서 그 자리에 쓴다
	return ImportMesh(SourcePath, AssetPath, Option) != nullptr;
}

bool UAssetManager::ReorientStaticMesh(UStaticMesh& Mesh, const std::function<FVector(const FVector&)>& Rotate)
{
	if (Mesh.GetLODCount() > 1 || Mesh.MeshData.Vertices.IsEmpty())
		return false;   // LOD가 이미 옛 방향으로 만들어졌으면 서로 어긋난다

	FStaticMeshData& Data = Mesh.MeshData;
	FBox Bounds{ FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX) };
	for (FVertexPNCT& Vertex : Data.Vertices)
	{
		Vertex.Position = Rotate(Vertex.Position);
		Vertex.Normal = Rotate(Vertex.Normal);
		Bounds.Min = FVector(std::min(Bounds.Min.X, Vertex.Position.X), std::min(Bounds.Min.Y, Vertex.Position.Y), std::min(Bounds.Min.Z, Vertex.Position.Z));
		Bounds.Max = FVector(std::max(Bounds.Max.X, Vertex.Position.X), std::max(Bounds.Max.Y, Vertex.Position.Y), std::max(Bounds.Max.Z, Vertex.Position.Z));
	}
	Data.AABB = Bounds;

	// 인덱스는 그대로다 (회전은 삼각형 감기 방향을 바꾸지 않는다). 정점 버퍼만 다시 올린다.
	TUniquePtr<FVertexBuffer> VB = RenderCommand::CreateStaticVertexBuffer(Data.Vertices.GetData(),
		sizeof(FVertexPNCT) * static_cast<uint32>(Data.Vertices.Num()), sizeof(FVertexPNCT));
	if (!VB)
		return false;
	Mesh.VertexBuffer = std::move(VB);

	Data.BuildTriangleBVH();   // 피킹용 삼각형 BVH도 새 좌표로
	return true;
}

void UAssetManager::ForEachObjStaticMesh(const std::function<void(const FString& Key, UStaticMesh& Mesh)>& Func)
{
	for (auto& [Key, Asset] : Get().AssetMap)
	{
		if (!Asset || !Asset->IsA<UStaticMesh>() || !fs::path(Key).extension().string().ends_with(".obj"))
			continue;
		Func(Key, *Cast<UStaticMesh>(Asset));
	}
}

FLODGenerateResult UAssetManager::GenerateStaticMeshLODs(UStaticMesh& Mesh, const FLODGenerateRequest& Request)
{
	TArray<FStaticMeshData> Generated;
	FLODGenerateResult Result = FStaticMeshLODGenerator::Generate(Mesh.GetMeshData(), Generated);

	if (!Result.bSuccess) return Result;

	if (!(Request.ScreenThresholds[0] >
		Request.ScreenThresholds[1] &&
		Request.ScreenThresholds[1] >
		Request.ScreenThresholds[2] &&
		Request.ScreenThresholds[2] > 0.0f))
	{
		Result.bSuccess = false;
		Result.FailureReason = "Screen thresholds must descend";
		return Result;
	}

	TArray<FStaticMeshLODResource> NewResources;

	for (FStaticMeshData& Data : Generated)
	{
		FStaticMeshLODResource Resource;
		Resource.Data = std::move(Data);
		Resource.Data.OptimizeTriangleOrderForVertexCache();

		Resource.VertexBuffer = RenderCommand::CreateStaticVertexBuffer(
			Resource.Data.Vertices.GetData(),
			sizeof(FVertexPNCT) *
			static_cast<uint32>(Resource.Data.Vertices.Num()),
			sizeof(FVertexPNCT));

		Resource.IndexBuffer = RenderCommand::CreateStaticIndexBuffer(
			Resource.Data.Indices.GetData(),
			static_cast<uint32>(Resource.Data.Indices.Num()));

		if (!Resource.VertexBuffer || !Resource.IndexBuffer)
		{
			Result.bSuccess = false;
			Result.FailureReason = "LOD GPU buffer creation failed";
			return Result;
		}

		NewResources.Add(std::move(Resource));
	}

	// bSaveToAsset이면 이 지점에서 파일 저장.
	// 저장 실패 시 기존 Mesh.AdditionalLODs는 그대로 둔다.

	Mesh.AdditionalLODs = std::move(NewResources);
	Mesh.ScreenThresholds = Request.ScreenThresholds;
	Mesh.RebuildRenderData();
	return Result;
}
