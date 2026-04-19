// Copyright Agile Lens. All Rights Reserved.
// PCVR passthrough via XR_FB_passthrough — no Meta XR Plugin dependency.

#include "OpenXRPassthrough.h"
#include "IOpenXRHMD.h"
#include "IXRTrackingSystem.h"
#include "Misc/CoreDelegates.h"

IMPLEMENT_MODULE(FOpenXRPassthroughModule, OpenXRPassthrough)

#define RESOLVE_XR_FUNC(Name) \
	xrGetInstanceProcAddr(InInstance, #Name, (PFN_xrVoidFunction*)&Name##_)

// ====================================================================
// Module lifecycle
// ====================================================================

void FOpenXRPassthroughModule::StartupModule()
{
	RegisterOpenXRExtensionModularFeature();
	UE_LOG(LogTemp, Log, TEXT("[OpenXRPassthrough] Module started — registered as extension plugin"));
}

void FOpenXRPassthroughModule::ShutdownModule()
{
	UnregisterOpenXRExtensionModularFeature();
}

// ====================================================================
// Extension registration
// ====================================================================

bool FOpenXRPassthroughModule::GetOptionalExtensions(TArray<const ANSICHAR*>& OutExtensions)
{
	OutExtensions.Add(XR_FB_PASSTHROUGH_EXTENSION_NAME);
	OutExtensions.Add(XR_EXT_COMPOSITION_LAYER_INVERTED_ALPHA_EXTENSION_NAME);
	UE_LOG(LogTemp, Log, TEXT("[OpenXRPassthrough] Requested optional extensions: %s, %s"),
		ANSI_TO_TCHAR(XR_FB_PASSTHROUGH_EXTENSION_NAME),
		ANSI_TO_TCHAR(XR_EXT_COMPOSITION_LAYER_INVERTED_ALPHA_EXTENSION_NAME));
	return true;  // not required — gracefully degrade if unavailable
}

// ====================================================================
// Instance + session lifecycle
// ====================================================================

void FOpenXRPassthroughModule::PostCreateInstance(XrInstance InInstance)
{
	Instance = InInstance;

	// Check if the extension was actually enabled by the runtime
	// We do this by trying to resolve the function pointers
	XrResult Result = xrGetInstanceProcAddr(InInstance, "xrCreatePassthroughFB",
		(PFN_xrVoidFunction*)&xrCreatePassthroughFB_);

	if (XR_FAILED(Result) || xrCreatePassthroughFB_ == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[OpenXRPassthrough] XR_FB_passthrough NOT available on this runtime"));
		bExtensionAvailable = false;
		return;
	}

	bExtensionAvailable = true;

	// Resolve remaining function pointers
	RESOLVE_XR_FUNC(xrDestroyPassthroughFB);
	RESOLVE_XR_FUNC(xrPassthroughStartFB);
	RESOLVE_XR_FUNC(xrPassthroughPauseFB);
	RESOLVE_XR_FUNC(xrCreatePassthroughLayerFB);
	RESOLVE_XR_FUNC(xrDestroyPassthroughLayerFB);
	RESOLVE_XR_FUNC(xrPassthroughLayerResumeFB);
	RESOLVE_XR_FUNC(xrPassthroughLayerPauseFB);
	RESOLVE_XR_FUNC(xrPassthroughLayerSetStyleFB);

	UE_LOG(LogTemp, Log, TEXT("[OpenXRPassthrough] XR_FB_passthrough AVAILABLE — all functions resolved"));
}

const void* FOpenXRPassthroughModule::OnCreateSession(
	XrInstance InInstance, XrSystemId InSystem, const void* InNext)
{
	// No chaining needed for passthrough — return InNext as-is
	return InNext;
}

void FOpenXRPassthroughModule::PostCreateSession(XrSession InSession)
{
	Session = InSession;

	// Log supported blend modes for diagnostics
	if (GEngine && GEngine->XRSystem.IsValid())
	{
		IOpenXRHMD* HMD = GEngine->XRSystem->GetIOpenXRHMD();
		if (HMD)
		{
			TArray<XrEnvironmentBlendMode> SupportedModes = HMD->GetSupportedEnvironmentBlendModes();
			FString ModeList;
			for (XrEnvironmentBlendMode M : SupportedModes)
			{
				if (!ModeList.IsEmpty()) ModeList += TEXT(", ");
				switch (M)
				{
					case XR_ENVIRONMENT_BLEND_MODE_OPAQUE: ModeList += TEXT("OPAQUE"); break;
					case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE: ModeList += TEXT("ADDITIVE"); break;
					case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND: ModeList += TEXT("ALPHA_BLEND"); break;
					default: ModeList += FString::Printf(TEXT("Unknown(%d)"), (int32)M); break;
				}
			}
			XrEnvironmentBlendMode CurrentMode = HMD->GetEnvironmentBlendMode();
			UE_LOG(LogTemp, Log, TEXT("[OpenXRPassthrough] Supported blend modes: [%s], Current: %d"),
				*ModeList, (int32)CurrentMode);
		}
	}

	// On PCVR (Quest Link), ALPHA_BLEND is not supported — only OPAQUE.
	// So BP_Passthrough's "Set Environment Blend Mode" approach does nothing.
	// We must use XR_FB_passthrough composition layer as underlay instead.
	// Only auto-create if blend mode is OPAQUE (i.e. we're on PCVR).
	bool bIsOpaque = true;
	if (GEngine && GEngine->XRSystem.IsValid())
	{
		IOpenXRHMD* HMD2 = GEngine->XRSystem->GetIOpenXRHMD();
		if (HMD2)
		{
			TArray<XrEnvironmentBlendMode> Modes = HMD2->GetSupportedEnvironmentBlendModes();
			bIsOpaque = !Modes.Contains(XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND);
		}
	}

	if (bExtensionAvailable && bIsOpaque)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[OpenXRPassthrough] PCVR detected (OPAQUE only) — creating XR_FB_passthrough layer"));

		// Quest Link doesn't support XR_EXT_composition_layer_inverted_alpha or
		// XR_FB_composition_layer_alpha_blend, so the compositor can't be told that
		// alpha is in UE's internal convention (0=opaque, 1=transparent).
		// Disable the compositor-level flag and instead enable the render pass that
		// actually inverts pixel alpha before swapchain submission.
		IConsoleVariable* InvertAlphaCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("xr.OpenXRInvertAlpha"));
		if (InvertAlphaCVar)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[OpenXRPassthrough] Disabling xr.OpenXRInvertAlpha for PCVR (no inverted-alpha extension)"));
			InvertAlphaCVar->Set(false);
		}

		// Enable the alpha invert render pass — converts UE convention (0=opaque)
		// to standard convention (1=opaque) so the compositor blends correctly
		// with BLEND_TEXTURE_SOURCE_ALPHA_BIT on the projection layer.
		IConsoleVariable* AlphaInvertPassCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("OpenXR.AlphaInvertPass"));
		if (AlphaInvertPassCVar)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[OpenXRPassthrough] Enabling OpenXR.AlphaInvertPass for PCVR passthrough"));
			AlphaInvertPassCVar->Set(true);
		}
		else
		{
			// Fallback: try the general alpha invert pass
			IConsoleVariable* GeneralAlphaInvertCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AlphaInvertPass"));
			if (GeneralAlphaInvertCVar)
			{
				UE_LOG(LogTemp, Log,
					TEXT("[OpenXRPassthrough] Enabling r.AlphaInvertPass for PCVR passthrough (fallback)"));
				GeneralAlphaInvertCVar->Set(true);
			}
		}

		CreatePassthrough();
	}
	else if (bExtensionAvailable)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[OpenXRPassthrough] ALPHA_BLEND supported — deferring to BP_Passthrough"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[OpenXRPassthrough] XR_FB_passthrough not available on this runtime"));
	}
}

void FOpenXRPassthroughModule::OnDestroySession(XrSession InSession)
{
	DestroyPassthrough();
	Session = XR_NULL_HANDLE;
}

// ====================================================================
// Event handling
// ====================================================================

void FOpenXRPassthroughModule::OnEvent(XrSession InSession,
	const XrEventDataBaseHeader* InHeader)
{
	if (!InHeader) return;

	if (InHeader->type == XR_TYPE_EVENT_DATA_PASSTHROUGH_STATE_CHANGED_FB)
	{
		const auto* StateEvent =
			reinterpret_cast<const XrEventDataPassthroughStateChangedFB*>(InHeader);

		UE_LOG(LogTemp, Log, TEXT("[OpenXRPassthrough] Passthrough state changed: flags=0x%X"),
			StateEvent->flags);

		if (StateEvent->flags & XR_PASSTHROUGH_STATE_CHANGED_REINIT_REQUIRED_BIT_FB)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[OpenXRPassthrough] Reinit required — recreating passthrough"));
			DestroyPassthrough();
			CreatePassthrough();
		}

		if (StateEvent->flags & XR_PASSTHROUGH_STATE_CHANGED_NON_RECOVERABLE_ERROR_BIT_FB)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[OpenXRPassthrough] Non-recoverable error — disabling passthrough"));
			DestroyPassthrough();
			bExtensionAvailable = false;
		}
	}
}

// ====================================================================
// Composition layer submission (RHI thread)
// ====================================================================

void FOpenXRPassthroughModule::UpdateCompositionLayers_RHIThread(
	XrSession InSession, TArray<XrCompositionLayerBaseHeader*>& Headers)
{
	if (!bPassthroughRunning || PassthroughLayer == XR_NULL_HANDLE)
		return;

	// Set BLEND_TEXTURE_SOURCE_ALPHA + UNPREMULTIPLIED_ALPHA on the projection layer
	// so the compositor uses scene alpha to determine what shows through to the
	// passthrough underlay. Without these, opaque geometry bleeds passthrough.
	for (int32 i = 0; i < Headers.Num(); ++i)
	{
		if (Headers[i] && Headers[i]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION)
		{
			auto* ProjLayer = reinterpret_cast<XrCompositionLayerProjection*>(Headers[i]);
			ProjLayer->layerFlags |= XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT
			                       | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
		}
	}

	// Insert passthrough layer BEFORE the projection layer (index 0)
	// so it renders as an underlay — the scene renders on top with alpha.
	Headers.Insert(
		reinterpret_cast<XrCompositionLayerBaseHeader*>(&CompositionLayerPassthrough),
		0);
}

// ====================================================================
// Passthrough creation / destruction
// ====================================================================

void FOpenXRPassthroughModule::CreatePassthrough()
{
	if (!bExtensionAvailable || !xrCreatePassthroughFB_ || Session == XR_NULL_HANDLE)
		return;

	// Step 1: Create passthrough feature (auto-start)
	XrPassthroughCreateInfoFB CreateInfo = { XR_TYPE_PASSTHROUGH_CREATE_INFO_FB };
	CreateInfo.next = nullptr;
	CreateInfo.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;

	XrResult Result = xrCreatePassthroughFB_(Session, &CreateInfo, &Passthrough);
	if (XR_FAILED(Result))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[OpenXRPassthrough] xrCreatePassthroughFB FAILED: %d"), (int32)Result);
		return;
	}

	// Step 2: Create fullscreen passthrough layer (reconstruction = underlay)
	XrPassthroughLayerCreateInfoFB LayerCreateInfo = { XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB };
	LayerCreateInfo.passthrough = Passthrough;
	LayerCreateInfo.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
	LayerCreateInfo.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;

	Result = xrCreatePassthroughLayerFB_(Session, &LayerCreateInfo, &PassthroughLayer);
	if (XR_FAILED(Result))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[OpenXRPassthrough] xrCreatePassthroughLayerFB FAILED: %d"), (int32)Result);
		xrDestroyPassthroughFB_(Passthrough);
		Passthrough = XR_NULL_HANDLE;
		return;
	}

	// Step 3: Set style (full opacity, no edge rendering)
	if (xrPassthroughLayerSetStyleFB_)
	{
		XrPassthroughStyleFB Style = { XR_TYPE_PASSTHROUGH_STYLE_FB };
		Style.textureOpacityFactor = 1.0f;
		Style.edgeColor = { 0.f, 0.f, 0.f, 0.f };
		xrPassthroughLayerSetStyleFB_(PassthroughLayer, &Style);
	}

	// Step 4: Prepare persistent composition layer struct
	FMemory::Memzero(CompositionLayerPassthrough);
	CompositionLayerPassthrough.type = XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB;
	CompositionLayerPassthrough.next = nullptr;
	CompositionLayerPassthrough.flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
	CompositionLayerPassthrough.space = XR_NULL_HANDLE;
	CompositionLayerPassthrough.layerHandle = PassthroughLayer;

	bPassthroughRunning = true;
	UE_LOG(LogTemp, Log, TEXT("[OpenXRPassthrough] Passthrough CREATED and RUNNING"));
}

void FOpenXRPassthroughModule::DestroyPassthrough()
{
	bPassthroughRunning = false;

	if (PassthroughLayer != XR_NULL_HANDLE && xrDestroyPassthroughLayerFB_)
	{
		xrDestroyPassthroughLayerFB_(PassthroughLayer);
		PassthroughLayer = XR_NULL_HANDLE;
	}

	if (Passthrough != XR_NULL_HANDLE && xrDestroyPassthroughFB_)
	{
		xrDestroyPassthroughFB_(Passthrough);
		Passthrough = XR_NULL_HANDLE;
	}

	UE_LOG(LogTemp, Log, TEXT("[OpenXRPassthrough] Passthrough destroyed"));
}

// ====================================================================
// Runtime toggle
// ====================================================================

void FOpenXRPassthroughModule::SetPassthroughEnabled(bool bEnable)
{
	if (!bExtensionAvailable) return;

	if (bEnable && !bPassthroughRunning)
	{
		if (PassthroughLayer != XR_NULL_HANDLE && xrPassthroughLayerResumeFB_)
		{
			xrPassthroughLayerResumeFB_(PassthroughLayer);
			bPassthroughRunning = true;
			UE_LOG(LogTemp, Log, TEXT("[OpenXRPassthrough] Passthrough RESUMED"));
		}
		else
		{
			CreatePassthrough();
		}
	}
	else if (!bEnable && bPassthroughRunning)
	{
		if (PassthroughLayer != XR_NULL_HANDLE && xrPassthroughLayerPauseFB_)
		{
			xrPassthroughLayerPauseFB_(PassthroughLayer);
			bPassthroughRunning = false;
			UE_LOG(LogTemp, Log, TEXT("[OpenXRPassthrough] Passthrough PAUSED"));
		}
	}
}

#undef RESOLVE_XR_FUNC
