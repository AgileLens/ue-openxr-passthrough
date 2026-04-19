# Changelog

All notable changes to this plugin will be documented in this file. Format loosely based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). This project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [1.0.0] — 2026-04-19

### Added
- Initial public release, extracted from the MarkerPlugin co-location project.
- `FOpenXRPassthroughModule` — implements `IOpenXRExtensionPlugin`.
- Requests `XR_FB_passthrough` and `XR_EXT_composition_layer_inverted_alpha` as optional extensions.
- Auto-detects PCVR (OPAQUE-only blend modes) vs standalone Quest (ALPHA_BLEND supported) on session create.
- PCVR path: creates fullscreen `XR_FB_passthrough` underlay, sets `BLEND_TEXTURE_SOURCE_ALPHA_BIT | UNPREMULTIPLIED_ALPHA_BIT` on the projection layer, disables `xr.OpenXRInvertAlpha`, enables `OpenXR.AlphaInvertPass`.
- Runtime toggle: `SetPassthroughEnabled(bool)`, `IsPassthroughEnabled()`, `IsPassthroughAvailable()`.
- Event handling for `XR_FB_passthrough` state changes (reinit required, non-recoverable errors).

### Known limitations
- Win64 only. No Blueprint library yet — wrap the module calls yourself if you need BP access.
- No support for styled passthrough (edge rendering, LUT, keyed hands). `xrPassthroughLayerSetStyleFB` is resolved but only used for full-opacity reconstruction.
- No geometry (projected / triangle mesh) passthrough layer — reconstruction only.
