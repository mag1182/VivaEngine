// Copyright 2022 BlueRose, Inc. All Rights Reserved.

#include "Outline/OutlineSkeletalMeshSceneProxy.h"
#include "Engine/Public/SkeletalMeshTypes.h"
#include "Engine/Public/SkeletalRenderPublic.h"
#include "MultiDrawCommon.h"
#include "Outline/OutlineSkeletalMeshComponent.h"
// #include "Stats/Stats2.h"
// #include "EngineStats.h"
#define LOCTEXT_NAMESPACE "OutlineSkeletalMeshSceneProxy"

FOutlineSkeletalMeshSceneProxy::FOutlineSkeletalMeshSceneProxy(const USkinnedMeshComponent* Component, FSkeletalMeshRenderData* InSkelMeshRenderData,EMultiDrawCullingMode CullingMode,bool bCastShadow):
	FSkeletalMeshSceneProxy(Component,InSkelMeshRenderData)
	,ComponentPtr(Component)
	,CullingMode(CullingMode)
	,bCastShadow(bCastShadow)
	,OverlayMaterial(Component->OverlayMaterial)
	,OverlayMaterialMaxDrawDistance(Component->OverlayMaterialMaxDrawDistance)
{
}

void FOutlineSkeletalMeshSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FOutlineSkeletalMeshSceneProxy_GetMeshElements);

	if( !MeshObject )
	{
		return;
	}	
	MeshObject->PreGDMECallback(ViewFamily.Scene->GetGPUSkinCache(), ViewFamily.FrameNumber);

	const FEngineShowFlags& EngineShowFlags = ViewFamily.EngineShowFlags;

	const UOutlineSkeletalMeshComponent* OutlineSkeletalMeshComponent = dynamic_cast<const UOutlineSkeletalMeshComponent *>(ComponentPtr);
	auto OutlinePassMaterialSet=OutlineSkeletalMeshComponent->OutlinePassMaterialSet;
	
	int32 FirstLODIdx = SkeletalMeshRenderData->GetFirstValidLODIdx(FMath::Max(SkeletalMeshRenderData->PendingFirstLODIdx, SkeletalMeshRenderData->CurrentFirstLODIdx));
	if (FirstLODIdx == INDEX_NONE)
	{
#if DO_CHECK
		UE_LOG(LogSkeletalMesh, Warning, TEXT("Skeletal mesh %s has no valid LODs for rendering."), *GetResourceName().ToString());
#endif
	}
	else
	{
		// for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		// {
		// 	if (VisibilityMap & (1 << ViewIndex))
		// 	{
		// 		const FSceneView* View = Views[ViewIndex];
		// 		MeshObject->UpdateMinDesiredLODLevel(View, GetBounds(), ViewFamily.FrameNumber, FirstLODIdx);
		// 	}
		// }

		const int32 LODIndex = MeshObject->GetLOD();
		check(LODIndex < SkeletalMeshRenderData->LODRenderData.Num());
		const FSkeletalMeshLODRenderData& LODData = SkeletalMeshRenderData->LODRenderData[LODIndex];

		if (LODSections.Num() > 0 && LODIndex >= FirstLODIdx)
		{
			check(SkeletalMeshRenderData->LODRenderData[LODIndex].GetNumVertices() > 0);

			const FLODSectionElements& LODSection = LODSections[LODIndex];

			check(LODSection.SectionElements.Num() == LODData.RenderSections.Num());

			for (FSkeletalMeshSectionIter Iter(LODIndex, *MeshObject, LODData, LODSection); Iter; ++Iter)
			{
				const FSkelMeshRenderSection& Section = Iter.GetSection();
				const int32 SectionIndex = Iter.GetSectionElementIndex();
				const FSectionElementInfo& SectionElementInfo = Iter.GetSectionElementInfo();

				bool bSectionSelected = false;

#if WITH_EDITORONLY_DATA
				// TODO: This is not threadsafe! A render command should be used to propagate SelectedEditorSection to the scene proxy.
				if (MeshObject->SelectedEditorMaterial != INDEX_NONE)
				{
					bSectionSelected = (MeshObject->SelectedEditorMaterial == SectionElementInfo.UseMaterialIndex);
				}
				else
				{
					bSectionSelected = (MeshObject->SelectedEditorSection == SectionIndex);
				}
			
#endif
				// If hidden skip the draw
				check(MeshObject->LODInfo.IsValidIndex(LODIndex));
				const bool IsMaterialHidden=MeshObject->LODInfo[LODIndex].HiddenMaterials.IsValidIndex(SectionElementInfo.UseMaterialIndex) && MeshObject->LODInfo[LODIndex].HiddenMaterials[SectionElementInfo.UseMaterialIndex];
				if (IsMaterialHidden || Section.bDisabled)
				{
					continue;
				}

				FSkeletalMeshSceneProxy::GetDynamicElementsSection(Views, ViewFamily, VisibilityMap, LODData, LODIndex, SectionIndex, bSectionSelected, SectionElementInfo, true, Collector);
				
				//轮廓
                if(OutlinePassMaterialSet.Num()<=0)
                    continue;
                TSharedPtr<FSectionElementInfo> Info=MakeShared<FSectionElementInfo>(FSectionElementInfo(SectionElementInfo));
                UMaterialInterface* CurrentOutlineMaterial=nullptr;
                if(SectionIndex < OutlinePassMaterialSet.Num())
                {
                    CurrentOutlineMaterial=OutlinePassMaterialSet[SectionIndex];
                }
                
                if (CurrentOutlineMaterial == nullptr)
                    continue;
                Info->Material = CurrentOutlineMaterial;
                FOutlineSkeletalMeshSceneProxy::GetDynamicElementsSection(Views, ViewFamily, VisibilityMap, LODData, LODIndex, SectionIndex, bSectionSelected, *Info.Get(), true, Collector);
			}
		}
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			if( PhysicsAssetForDebug )
			{
				DebugDrawPhysicsAsset(ViewIndex, Collector, ViewFamily.EngineShowFlags);
			}

			if (EngineShowFlags.MassProperties && DebugMassData.Num() > 0)
			{
				FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
				if (MeshObject->GetComponentSpaceTransforms())
				{
					const TArray<FTransform>& ComponentSpaceTransforms = *MeshObject->GetComponentSpaceTransforms();

					for (const FDebugMassData& DebugMass : DebugMassData)
					{
						if (ComponentSpaceTransforms.IsValidIndex(DebugMass.BoneIndex))
						{
							const FTransform BoneToWorld = ComponentSpaceTransforms[DebugMass.BoneIndex] * FTransform(GetLocalToWorld());
							DebugMass.DrawDebugMass(PDI, BoneToWorld);
						}
					}
				}
			}

			if (ViewFamily.EngineShowFlags.SkeletalMeshes)
			{
				RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
			}

			if (ViewFamily.EngineShowFlags.Bones || bDrawDebugSkeleton)
			{
				DebugDrawSkeleton(ViewIndex, Collector, ViewFamily.EngineShowFlags);
			}
		}
	}
#endif
}

void FOutlineSkeletalMeshSceneProxy::GetDynamicElementsSection(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, 
	const FSkeletalMeshLODRenderData& LODData, const int32 LODIndex, const int32 SectionIndex, bool bSectionSelected,
	const FSectionElementInfo& SectionElementInfo, bool bInSelectable, FMeshElementCollector& Collector ) const
{
	const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];

	//// If hidden skip the draw
	//if (Section.bDisabled || MeshObject->IsMaterialHidden(LODIndex,SectionElementInfo.UseMaterialIndex))
	//{
	//	return;
	//}

#if !WITH_EDITOR
	const bool bIsSelected = false;
#else // #if !WITH_EDITOR
	bool bIsSelected = IsSelected();

	// if the mesh isn't selected but the mesh section is selected in the AnimSetViewer, find the mesh component and make sure that it can be highlighted (ie. are we rendering for the AnimSetViewer or not?)
	if( !bIsSelected && bSectionSelected && bCanHighlightSelectedSections )
	{
		bIsSelected = true;
	}
#endif // #if WITH_EDITOR

	const bool bIsWireframe = ViewFamily.EngineShowFlags.Wireframe;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];

			FMeshBatch& Mesh = Collector.AllocateMesh();

			CreateBaseMeshBatch(View, LODData, LODIndex, SectionIndex, SectionElementInfo, Mesh);
			
			if(!Mesh.VertexFactory)
			{
				// hide this part
				continue;
			}

			Mesh.bWireframe |= bForceWireframe;
			Mesh.Type = PT_TriangleList;
			Mesh.bSelectable = bInSelectable;

			FMeshBatchElement& BatchElement = Mesh.Elements[0];

		#if WITH_EDITOR
			Mesh.BatchHitProxyId = SectionElementInfo.HitProxy ? SectionElementInfo.HitProxy->Id : FHitProxyId();

			if (bSectionSelected && bCanHighlightSelectedSections)
			{
				Mesh.bUseSelectionOutline = true;
			}
			else
			{
				Mesh.bUseSelectionOutline = !bCanHighlightSelectedSections && bIsSelected;
			}
		#endif

#if WITH_EDITORONLY_DATA
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if (bIsSelected)
			{
				if (ViewFamily.EngineShowFlags.VertexColors && AllowDebugViewmodes())
				{
					// Override the mesh's material with our material that draws the vertex colors
					UMaterial* VertexColorVisualizationMaterial = NULL;
					switch (GVertexColorViewMode)
					{
					case EVertexColorViewMode::Color:
						VertexColorVisualizationMaterial = GEngine->VertexColorViewModeMaterial_ColorOnly;
						break;

					case EVertexColorViewMode::Alpha:
						VertexColorVisualizationMaterial = GEngine->VertexColorViewModeMaterial_AlphaAsColor;
						break;

					case EVertexColorViewMode::Red:
						VertexColorVisualizationMaterial = GEngine->VertexColorViewModeMaterial_RedOnly;
						break;

					case EVertexColorViewMode::Green:
						VertexColorVisualizationMaterial = GEngine->VertexColorViewModeMaterial_GreenOnly;
						break;

					case EVertexColorViewMode::Blue:
						VertexColorVisualizationMaterial = GEngine->VertexColorViewModeMaterial_BlueOnly;
						break;
					}
					check(VertexColorVisualizationMaterial != NULL);

					auto VertexColorVisualizationMaterialInstance = new FColoredMaterialRenderProxy(
						VertexColorVisualizationMaterial->GetRenderProxy(),
						GetSelectionColor(FLinearColor::White, bSectionSelected, IsHovered())
					);

					Collector.RegisterOneFrameMaterialProxy(VertexColorVisualizationMaterialInstance);
					Mesh.MaterialRenderProxy = VertexColorVisualizationMaterialInstance;
				}
			}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#endif // WITH_EDITORONLY_DATA

			BatchElement.MinVertexIndex = Section.BaseVertexIndex;
			Mesh.CastShadow = bCastShadow;
			Mesh.bCanApplyViewModeOverrides = true;
			Mesh.bUseWireframeSelectionColoring = bIsSelected;
			
			switch (CullingMode)
			{
			case EMultiDrawCullingMode::FrontfaceCulling:
				Mesh.ReverseCulling = !Mesh.ReverseCulling;
				break;
			case EMultiDrawCullingMode::BackfaceCulling:
				break;
			case EMultiDrawCullingMode::DoubleSide:
				Mesh.bDisableBackfaceCulling=true;
				break;
			}
			
		#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			BatchElement.VisualizeElementIndex = SectionIndex;
			Mesh.VisualizeLODIndex = LODIndex;
		#endif

			if (ensureMsgf(Mesh.MaterialRenderProxy, TEXT("GetDynamicElementsSection with invalid MaterialRenderProxy. Owner:%s LODIndex:%d UseMaterialIndex:%d"), *GetOwnerName().ToString(), LODIndex, SectionElementInfo.UseMaterialIndex))
			{
				Collector.AddMesh(ViewIndex, Mesh);
			}

			// const int32 NumVertices = Section.GetNumVertices();
			// INC_DWORD_STAT_BY(STAT_GPUSkinVertices,(uint32)(bIsCPUSkinned ? 0 : NumVertices));
			// INC_DWORD_STAT_BY(STAT_SkelMeshTriangles,Mesh.GetNumPrimitives());
			// INC_DWORD_STAT(STAT_SkelMeshDrawCalls);
			
			if (OverlayMaterial != nullptr)
			{
				const bool bHasOverlayCullDistance = 
					OverlayMaterialMaxDrawDistance > 1.f && 
					OverlayMaterialMaxDrawDistance != FLT_MAX && 
					!ViewFamily.EngineShowFlags.DistanceCulledPrimitives;
				
				bool bAddOverlay = true;
				if (bHasOverlayCullDistance)
				{
					// this is already combined with ViewDistanceScale
					float MaxDrawDistanceScale = GetCachedScalabilityCVars().SkeletalMeshOverlayDistanceScale;
					MaxDrawDistanceScale *= GetCachedScalabilityCVars().CalculateFieldOfViewDistanceScale(View->DesiredFOV);
					float DistanceSquared = (GetBounds().Origin - View->ViewMatrices.GetViewOrigin()).SizeSquared();
					if (DistanceSquared > FMath::Square(OverlayMaterialMaxDrawDistance * MaxDrawDistanceScale))
					{
						// distance culled
						bAddOverlay = false;
					}
				}
				
				if (bAddOverlay)
				{
					FMeshBatch& OverlayMeshBatch = Collector.AllocateMesh();
					OverlayMeshBatch = Mesh;
					OverlayMeshBatch.bOverlayMaterial = true;
					OverlayMeshBatch.CastShadow = false;
					OverlayMeshBatch.bSelectable = false;
					OverlayMeshBatch.MaterialRenderProxy = OverlayMaterial->GetRenderProxy();
					// make sure overlay is always rendered on top of base mesh
					OverlayMeshBatch.MeshIdInPrimitive += LODData.RenderSections.Num();
					Collector.AddMesh(ViewIndex, OverlayMeshBatch);
				
					// INC_DWORD_STAT_BY(STAT_SkelMeshTriangles, OverlayMeshBatch.GetNumPrimitives());
					// INC_DWORD_STAT(STAT_SkelMeshDrawCalls);
				}
			}
		}
	}
}

void FOutlineSkeletalMeshSceneProxy::CreateBaseMeshBatch(const FSceneView* View, const FSkeletalMeshLODRenderData& LODData, const int32 LODIndex, const int32 SectionIndex, const FSectionElementInfo& SectionElementInfo, FMeshBatch& Mesh,ESkinVertexFactoryMode VFMode) const
{
	Mesh.VertexFactory = MeshObject->GetSkinVertexFactory(View, LODIndex, SectionIndex, VFMode);
	Mesh.MaterialRenderProxy = SectionElementInfo.Material->GetRenderProxy();
	const EBlendMode& BlendMode= SectionElementInfo.Material->GetBlendMode();
	const bool IsTranslucent=BlendMode != BLEND_Opaque && BlendMode != BLEND_Masked;
	Mesh.bOverlayMaterial=IsTranslucent;

#if RHI_RAYTRACING
	Mesh.SegmentIndex = SectionIndex;
	Mesh.CastRayTracedShadow = SectionElementInfo.bEnableShadowCasting && bCastDynamicShadow;
#endif

	FMeshBatchElement& BatchElement = Mesh.Elements[0];
	BatchElement.FirstIndex = LODData.RenderSections[SectionIndex].BaseIndex;
	BatchElement.IndexBuffer = LODData.MultiSizeIndexContainer.GetIndexBuffer();
	BatchElement.MinVertexIndex = LODData.RenderSections[SectionIndex].GetVertexBufferIndex();
	BatchElement.MaxVertexIndex = LODData.RenderSections[SectionIndex].GetVertexBufferIndex() + LODData.RenderSections[SectionIndex].GetNumVertices() - 1;

	FOutlineSkeletalMeshObjectGPUSkin* MeshObjectGPUSkin=static_cast<FOutlineSkeletalMeshObjectGPUSkin*>(MeshObject);
	FGPUSkinCacheEntry* GPUSkinCacheEntry=MeshObjectGPUSkin->GetGPUSkinCacheEntry();

#if RHI_RAYTRACING
	FGPUSkinCacheEntry* Entry= VFMode == ESkinVertexFactoryMode::RayTracing ? MeshObject->GetSkinCacheEntryForRayTracing() : GPUSkinCacheEntry;
	FGPUSkinBatchElementUserData* VertexFactoryUserData = Entry ? &Entry->BatchElementsUserData[SectionIndex] : nullptr;
#else
	FGPUSkinBatchElementUserData* VertexFactoryUserData = GPUSkinCacheEntry ? &GPUSkinCacheEntry->BatchElementsUserData[SectionIndex] : nullptr;
#endif
	BatchElement.VertexFactoryUserData = VertexFactoryUserData;
	BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
	BatchElement.NumPrimitives = LODData.RenderSections[SectionIndex].NumTriangles;

	UpdateLooseParametersUniformBuffer(View, SectionIndex, Mesh, VertexFactoryUserData);
}

void FOutlineSkeletalMeshSceneProxy::UpdateLooseParametersUniformBuffer(const FSceneView* View, const int32 SectionIndex, const FMeshBatch& Mesh, const FGPUSkinBatchElementUserData* BatchUserData) const
{
	// Loose parameters uniform buffer is only needed for PassThroughVF
	if (!Mesh.VertexFactory || Mesh.VertexFactory->GetType() != &FGPUSkinPassthroughVertexFactory::StaticType)
	{
		return;
	}
	
	FLocalVertexFactoryLooseParameters Parameters;

	FVertexFactory* NonConstVertexFactory = const_cast<FVertexFactory*>(Mesh.VertexFactory);
	FOutlineGPUSkinPassthroughVertexFactory* PassthroughVertexFactory = static_cast<FOutlineGPUSkinPassthroughVertexFactory*>(NonConstVertexFactory);
	// Bone data is updated whenever animation triggers a dynamic update, animation can skip frames hence the frequency is not necessary every frame.
	// So check if bone data is updated this frame, if not then the previous frame data is stale and not suitable for motion blur.
	bool bBoneDataUpdatedThisFrame = (View->Family->FrameNumber == PassthroughVertexFactory->GetUpdatedFrameNumber());
	// If world is paused, use current frame bone matrices, so velocity is canceled and skeletal mesh isn't blurred from motion.
	bool bVerticesInMotion = !View->Family->bWorldIsPaused && bBoneDataUpdatedThisFrame;

	const bool bUsesSkinCache = BatchUserData != nullptr;
	if (bUsesSkinCache)
	{
		auto Entry = BatchUserData->Entry;
		FGPUSkinCacheEntry::FSectionDispatchData const& DispatchData = Entry->GetDispatchData()[SectionIndex];
		auto PreviousPositionBuffer= DispatchData.PreviousPositionBuffer != nullptr ? &DispatchData.PreviousPositionBuffer->Buffer : nullptr;
		auto PositionBuffer= DispatchData.PositionBuffer != nullptr ? &DispatchData.PositionBuffer->Buffer : nullptr;
		Parameters.GPUSkinPassThroughPreviousPositionBuffer = bVerticesInMotion ?
			PreviousPositionBuffer->SRV :
			PositionBuffer->SRV;
	}
	else // Mesh deformer
		{
			Parameters.GPUSkinPassThroughPreviousPositionBuffer = (bVerticesInMotion && PassthroughVertexFactory->GetPrevPositionRDG()) ?
			PassthroughVertexFactory->GetPrevPositionRDG()->GetOrCreateSRV(FRHIBufferSRVCreateInfo(PF_R32_FLOAT)) :
			PassthroughVertexFactory->GetPositionsSRV();
		}
	
	PassthroughVertexFactory->GetLooseParametersUniformBuffer().UpdateUniformBufferImmediate(Parameters);
}

#undef LOCTEXT_NAMESPACE