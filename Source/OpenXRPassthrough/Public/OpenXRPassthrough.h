// Copyright Agile Lens. All Rights Reserved.
// PCVR passthrough via XR_FB_passthrough — no Meta XR Plugin dependency.

#pragma once

#include "Modules/ModuleManager.h"
#include "OpenXRCore.h"
#include "IOpenXRExtensionPlugin.h"

/**
 * OpenXR extension module that enables Quest Link passthrough on PCVR
 * using the XR_FB_passthrough extension.  Registers as an
 * IOpenXRExtensionPlugin so UE's OpenXR runtime picks it up automatically.
 *
 * Only compiled/loaded on Win64 — standalone Quest uses alpha-blend passthrough
 * natively and does not need this.
 */
class FOpenXRPassthroughModule : public IModuleInterface, public IOpenXRExtensionPlugin
{
public:
	// ---- IModuleInterface ----
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// ---- IOpenXRExtensionPlugin ----
	virtual FString GetDisplayName() override { return TEXT("OpenXRPassthrough"); }

	virtual bool GetOptionalExtensions(TArray<const ANSICHAR*>& OutExtensions) override;
	virtual void PostCreateInstance(XrInstance InInstance) override;
	virtual const void* OnCreateSession(XrInstance InInstance, XrSystemId InSystem, const void* InNext) override;
	virtual void PostCreateSession(XrSession InSession) override;
	virtual void OnDestroySession(XrSession InSession) override;
	virtual void OnEvent(XrSession InSession, const XrEventDataBaseHeader* InHeader) override;

	// Insert passthrough composition layer before the projection layer (underlay)
	virtual void UpdateCompositionLayers_RHIThread(XrSession InSession,
		TArray<XrCompositionLayerBaseHeader*>& Headers) override;

	/** Toggle passthrough on/off at runtime. */
	void SetPassthroughEnabled(bool bEnable);
	bool IsPassthroughEnabled() const { return bPassthroughRunning; }
	bool IsPassthroughAvailable() const { return bExtensionAvailable; }

private:
	// OpenXR handles
	XrInstance Instance = XR_NULL_HANDLE;
	XrSession Session = XR_NULL_HANDLE;
	XrPassthroughFB Passthrough = XR_NULL_HANDLE;
	XrPassthroughLayerFB PassthroughLayer = XR_NULL_HANDLE;

	// Composition layer struct — persistent, submitted every frame
	XrCompositionLayerPassthroughFB CompositionLayerPassthrough;

	// State
	bool bExtensionAvailable = false;
	bool bPassthroughRunning = false;

	// Function pointers (resolved via xrGetInstanceProcAddr)
	PFN_xrCreatePassthroughFB xrCreatePassthroughFB_ = nullptr;
	PFN_xrDestroyPassthroughFB xrDestroyPassthroughFB_ = nullptr;
	PFN_xrPassthroughStartFB xrPassthroughStartFB_ = nullptr;
	PFN_xrPassthroughPauseFB xrPassthroughPauseFB_ = nullptr;
	PFN_xrCreatePassthroughLayerFB xrCreatePassthroughLayerFB_ = nullptr;
	PFN_xrDestroyPassthroughLayerFB xrDestroyPassthroughLayerFB_ = nullptr;
	PFN_xrPassthroughLayerResumeFB xrPassthroughLayerResumeFB_ = nullptr;
	PFN_xrPassthroughLayerPauseFB xrPassthroughLayerPauseFB_ = nullptr;
	PFN_xrPassthroughLayerSetStyleFB xrPassthroughLayerSetStyleFB_ = nullptr;

	void CreatePassthrough();
	void DestroyPassthrough();
};
