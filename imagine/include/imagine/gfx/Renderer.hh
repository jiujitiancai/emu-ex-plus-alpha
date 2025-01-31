#pragma once

/*  This file is part of Imagine.

	Imagine is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Imagine is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Imagine.  If not, see <http://www.gnu.org/licenses/> */

#include <imagine/config/defs.hh>
#include <imagine/gfx/defs.hh>
#include <imagine/gfx/PixmapTexture.hh>
#include <imagine/gfx/PixmapBufferTexture.hh>
#include <imagine/pixmap/PixelFormat.hh>

#ifdef CONFIG_GFX_OPENGL
#include <imagine/gfx/opengl/GLRenderer.hh>
#endif

#include <vector>
#include <span>

namespace IG::Data
{
class PixmapSource;
}

namespace IG
{
class Window;
}

namespace IG::Gfx
{

class RendererTask;
class Program;

struct TextureBufferModeDesc
{
	const char *name = "";
	TextureBufferMode mode{};

	constexpr TextureBufferModeDesc() {}
	constexpr TextureBufferModeDesc(const char *name, TextureBufferMode mode):name{name}, mode{mode} {}
	constexpr bool operator ==(TextureBufferMode mode_) const { return mode == mode_; }

};

struct DrawableConfig
{
	IG::PixelFormat pixelFormat{};
	ColorSpace colorSpace{};
	constexpr bool operator ==(const DrawableConfig&) const = default;
};

struct DrawableConfigDesc
{
	const char *name;
	DrawableConfig config;

	constexpr bool operator ==(const DrawableConfig &c) const { return config == c; }
};

class Renderer : public RendererImpl
{
public:
	using RendererImpl::RendererImpl;
	Renderer(ApplicationContext);
	~Renderer();
	void configureRenderer();
	bool isConfigured() const;
	const RendererTask &task() const;
	RendererTask &task();
	ApplicationContext appContext() const;
	void initMainTask(Window *initialWindow, DrawableConfig c = {});
	bool attachWindow(Window &, DrawableConfig c = {});
	void detachWindow(Window &);
	bool setDrawableConfig(Window &, DrawableConfig);
	bool canRenderToMultiplePixelFormats() const;
	NativeWindowFormat nativeWindowFormat() const;
	void setWindowValidOrientations(Window &win, Orientation validO);
	void animateProjectionMatrixRotation(Window &win, float srcAngle, float destAngle);
	static ClipRect makeClipRect(const Window &win, IG::WindowRect rect);
	bool supportsSyncFences() const;
	void setPresentationTime(Window &, IG::FrameTime time) const;
	bool supportsPresentationTime() const;
	unsigned maxSwapChainImages() const;
	void setCorrectnessChecks(bool on);
	std::vector<DrawableConfigDesc> supportedDrawableConfigs() const;
	bool hasBgraFormat(TextureBufferMode) const;

	// shaders

	Shader makeShader(std::span<std::string_view> srcs, ShaderType type);
	Shader makeShader(std::string_view src, ShaderType type);
	Shader makeCompatShader(std::span<std::string_view> srcs, ShaderType type);
	Shader makeCompatShader(std::string_view src, ShaderType type);
	NativeShader defaultVShader();
	bool makeCommonProgram(CommonProgram);
	bool commonProgramIsCompiled(CommonProgram program) const;
	void uniformF(Program &program, int uniformLocation, float v1, float v2);
	void releaseShaderCompiler();
	void autoReleaseShaderCompiler();

	// resources

	Texture makeTexture(TextureConfig);
	Texture makeTexture(IG::Data::PixmapSource, const TextureSampler *compatSampler = {}, bool makeMipmaps = true);
	PixmapTexture makePixmapTexture(TextureConfig);
	PixmapTexture makePixmapTexture(IG::Data::PixmapSource, const TextureSampler *compatSampler = {}, bool makeMipmaps = true);
	PixmapBufferTexture makePixmapBufferTexture(TextureConfig config, TextureBufferMode mode = {}, bool singleBuffer = false);
	std::vector<TextureBufferModeDesc> textureBufferModes();
	TextureBufferMode makeValidTextureBufferMode(TextureBufferMode mode = {});
	TextureSampler makeTextureSampler(TextureSamplerConfig config);
	const TextureSampler &makeCommonTextureSampler(CommonTextureSampler sampler);
	const TextureSampler &make(CommonTextureSampler sampler) { return makeCommonTextureSampler(sampler); }
	const TextureSampler &commonTextureSampler(CommonTextureSampler sampler) const;
	const TextureSampler &get(CommonTextureSampler sampler) const { return commonTextureSampler(sampler); }

	// color space control

	bool supportsColorSpace() const;
	bool hasSrgbColorSpaceWriteControl() const;
	static ColorSpace supportedColorSpace(IG::PixelFormat, ColorSpace wantedColorSpace);
};

}
