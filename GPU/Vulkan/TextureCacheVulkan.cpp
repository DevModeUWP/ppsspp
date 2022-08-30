// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <algorithm>
#include <cstring>

#include "ext/xxhash.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Math/math_util.h"
#include "Common/Profiler/Profiler.h"
#include "Common/GPU/thin3d.h"
#include "Common/GPU/Vulkan/VulkanRenderManager.h"

#include "Common/Data/Convert/ColorConv.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Core/System.h"

#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanImage.h"
#include "Common/GPU/Vulkan/VulkanMemory.h"

#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/Common/TextureShaderCommon.h"
#include "GPU/Common/PostShader.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Vulkan/VulkanContext.h"
#include "GPU/Vulkan/TextureCacheVulkan.h"
#include "GPU/Vulkan/FramebufferManagerVulkan.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Vulkan/DrawEngineVulkan.h"

using namespace PPSSPP_VK;

#define TEXCACHE_MIN_SLAB_SIZE (8 * 1024 * 1024)
#define TEXCACHE_MAX_SLAB_SIZE (32 * 1024 * 1024)
#define TEXCACHE_SLAB_PRESSURE 4

const char *uploadShader = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

// 8x8 is the most common compute shader workgroup size, and works great on all major
// hardware vendors.
layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

uniform layout(binding = 0, rgba8) writeonly image2D img;

layout(std430, binding = 1) buffer Buf {
	uint data[];
} buf;

layout(push_constant) uniform Params {
	int width;
	int height;
} params;

uint readColoru(uvec2 p) {
	return buf.data[p.y * params.width + p.x];
}

vec4 readColorf(uvec2 p) {
	// Unpack the color (we could look it up in a CLUT here if we wanted...)
	// The imageStore repack is free.
	return unpackUnorm4x8(readColoru(p));
}

void writeColorf(ivec2 p, vec4 c) {
	imageStore(img, p, c);
}

%s

// Note that main runs once per INPUT pixel, unlike the old model.
void main() {
	uvec2 xy = gl_GlobalInvocationID.xy;
	// Kill off any out-of-image threads to avoid stray writes.
	// Should only happen on the tiniest mipmaps as PSP textures are power-of-2,
	// and we use a 8x8 workgroup size. Probably not really necessary.
	if (xy.x >= params.width || xy.y >= params.height)
		return;
	// applyScaling will write the upscaled pixels, using writeColorf above.
	// It's expected to write a square of scale*scale pixels, at the location xy*scale.
	applyScaling(xy);
}

)";

SamplerCache::~SamplerCache() {
	DeviceLost();
}

VkSampler SamplerCache::GetOrCreateSampler(const SamplerCacheKey &key) {
	VkSampler sampler = cache_.Get(key);
	if (sampler != VK_NULL_HANDLE)
		return sampler;

	VkSamplerCreateInfo samp = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samp.addressModeU = key.sClamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samp.addressModeV = key.tClamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
	// W addressing is irrelevant for 2d textures, but Mali recommends that all clamp modes are the same if possible so just copy from U.
	samp.addressModeW = key.texture3d ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : samp.addressModeU;
	samp.compareOp = VK_COMPARE_OP_ALWAYS;
	samp.flags = 0;
	samp.magFilter = key.magFilt ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	samp.minFilter = key.minFilt ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	samp.mipmapMode = key.mipFilt ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
	if (key.aniso) {
		// Docs say the min of this value and the supported max are used.
		samp.maxAnisotropy = 1 << g_Config.iAnisotropyLevel;
		samp.anisotropyEnable = true;
	} else {
		samp.maxAnisotropy = 1.0f;
		samp.anisotropyEnable = false;
	}
	if (key.maxLevel == 9 * 256) {
		// No max level needed.
		samp.maxLod = VK_LOD_CLAMP_NONE;
	} else {
		samp.maxLod = (float)(int32_t)key.maxLevel * (1.0f / 256.0f);
	}
	samp.minLod = (float)(int32_t)key.minLevel * (1.0f / 256.0f);
	samp.mipLodBias = (float)(int32_t)key.lodBias * (1.0f / 256.0f);

	VkResult res = vkCreateSampler(vulkan_->GetDevice(), &samp, nullptr, &sampler);
	_assert_(res == VK_SUCCESS);
	cache_.Insert(key, sampler);
	return sampler;
}

std::string SamplerCache::DebugGetSamplerString(std::string id, DebugShaderStringType stringType) {
	SamplerCacheKey key;
	key.FromString(id);
	return StringFromFormat("%s/%s mag:%s min:%s mip:%s maxLod:%f minLod:%f bias:%f",
		key.sClamp ? "Clamp" : "Wrap",
		key.tClamp ? "Clamp" : "Wrap",
		key.magFilt ? "Linear" : "Nearest",
		key.minFilt ? "Linear" : "Nearest",
		key.mipFilt ? "Linear" : "Nearest",
		key.maxLevel / 256.0f,
		key.minLevel / 256.0f,
		key.lodBias / 256.0f);
}

void SamplerCache::DeviceLost() {
	cache_.Iterate([&](const SamplerCacheKey &key, VkSampler sampler) {
		vulkan_->Delete().QueueDeleteSampler(sampler);
	});
	cache_.Clear();
	vulkan_ = nullptr;
}

void SamplerCache::DeviceRestore(VulkanContext *vulkan) {
	vulkan_ = vulkan;
}

std::vector<std::string> SamplerCache::DebugGetSamplerIDs() const {
	std::vector<std::string> ids;
	cache_.Iterate([&](const SamplerCacheKey &id, VkSampler sampler) {
		std::string idstr;
		id.ToString(&idstr);
		ids.push_back(idstr);
	});
	return ids;
}

TextureCacheVulkan::TextureCacheVulkan(Draw::DrawContext *draw, Draw2D *draw2D, VulkanContext *vulkan)
	: TextureCacheCommon(draw, draw2D),
		computeShaderManager_(vulkan),
		samplerCache_(vulkan) {
	DeviceRestore(draw);
}

TextureCacheVulkan::~TextureCacheVulkan() {
	DeviceLost();
}

void TextureCacheVulkan::SetFramebufferManager(FramebufferManagerVulkan *fbManager) {
	framebufferManager_ = fbManager;
}

void TextureCacheVulkan::DeviceLost() {
	textureShaderCache_->DeviceLost();

	VulkanContext *vulkan = draw_ ? (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT) : nullptr;

	Clear(true);

	samplerCache_.DeviceLost();

	if (samplerNearest_)
		vulkan->Delete().QueueDeleteSampler(samplerNearest_);

	if (uploadCS_ != VK_NULL_HANDLE)
		vulkan->Delete().QueueDeleteShaderModule(uploadCS_);

	computeShaderManager_.DeviceLost();

	nextTexture_ = nullptr;
	draw_ = nullptr;
}

void TextureCacheVulkan::DeviceRestore(Draw::DrawContext *draw) {
	VulkanContext *vulkan = (VulkanContext *)draw->GetNativeObject(Draw::NativeObject::CONTEXT);
	draw_ = draw;

	_assert_(!allocator_);

	samplerCache_.DeviceRestore(vulkan);
	textureShaderCache_->DeviceRestore(draw);

	VkSamplerCreateInfo samp{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samp.magFilter = VK_FILTER_NEAREST;
	samp.minFilter = VK_FILTER_NEAREST;
	samp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	VkResult res = vkCreateSampler(vulkan->GetDevice(), &samp, nullptr, &samplerNearest_);
	_assert_(res == VK_SUCCESS);

	CompileScalingShader();

	computeShaderManager_.DeviceRestore(vulkan);
}

void TextureCacheVulkan::NotifyConfigChanged() {
	TextureCacheCommon::NotifyConfigChanged();
	CompileScalingShader();
}

static std::string ReadShaderSrc(const Path &filename) {
	size_t sz = 0;
	char *data = (char *)VFSReadFile(filename.c_str(), &sz);
	if (!data)
		return std::string();

	std::string src(data, sz);
	delete[] data;
	return src;
}

void TextureCacheVulkan::CompileScalingShader() {
	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);

	if (!g_Config.bTexHardwareScaling || g_Config.sTextureShaderName != textureShader_) {
		if (uploadCS_ != VK_NULL_HANDLE)
			vulkan->Delete().QueueDeleteShaderModule(uploadCS_);
		textureShader_.clear();
		shaderScaleFactor_ = 0;  // no texture scaling shader
	} else if (uploadCS_) {
		// No need to recreate.
		return;
	}

	if (!g_Config.bTexHardwareScaling)
		return;

	ReloadAllPostShaderInfo(draw_);
	const TextureShaderInfo *shaderInfo = GetTextureShaderInfo(g_Config.sTextureShaderName);
	if (!shaderInfo || shaderInfo->computeShaderFile.empty())
		return;

	std::string shaderSource = ReadShaderSrc(shaderInfo->computeShaderFile);
	std::string fullUploadShader = StringFromFormat(uploadShader, shaderSource.c_str());

	std::string error;
	uploadCS_ = CompileShaderModule(vulkan, VK_SHADER_STAGE_COMPUTE_BIT, fullUploadShader.c_str(), &error);
	_dbg_assert_msg_(uploadCS_ != VK_NULL_HANDLE, "failed to compile upload shader");

	textureShader_ = g_Config.sTextureShaderName;
	shaderScaleFactor_ = shaderInfo->scaleFactor;
}

void TextureCacheVulkan::ReleaseTexture(TexCacheEntry *entry, bool delete_them) {
	delete entry->vkTex;
	entry->vkTex = nullptr;
}

VkFormat getClutDestFormatVulkan(GEPaletteFormat format) {
	switch (format) {
	case GE_CMODE_16BIT_ABGR4444:
		return VULKAN_4444_FORMAT;
	case GE_CMODE_16BIT_ABGR5551:
		return VULKAN_1555_FORMAT;
	case GE_CMODE_16BIT_BGR5650:
		return VULKAN_565_FORMAT;
	case GE_CMODE_32BIT_ABGR8888:
		return VULKAN_8888_FORMAT;
	}
	return VK_FORMAT_UNDEFINED;
}

static const VkFilter MagFiltVK[2] = {
	VK_FILTER_NEAREST,
	VK_FILTER_LINEAR
};

void TextureCacheVulkan::StartFrame() {
	TextureCacheCommon::StartFrame();

	InvalidateLastTexture();
	textureShaderCache_->Decimate();

	timesInvalidatedAllThisFrame_ = 0;
	texelsScaledThisFrame_ = 0;
	replacementTimeThisFrame_ = 0.0;

	if (clearCacheNextFrame_) {
		Clear(true);
		clearCacheNextFrame_ = false;
	} else {
		int slabPressureLimit = TEXCACHE_SLAB_PRESSURE;
		if (g_Config.iTexScalingLevel > 1) {
			// Since textures are 2D maybe we should square this, but might get too non-aggressive.
			slabPressureLimit *= g_Config.iTexScalingLevel;
		}
		// TODO: Use some indication from VMA.
		// Maybe see https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/staying_within_budget.html#staying_within_budget_querying_for_budget .
		Decimate(false);
	}

	computeShaderManager_.BeginFrame();
}

void TextureCacheVulkan::EndFrame() {
	computeShaderManager_.EndFrame();

	if (texelsScaledThisFrame_) {
		VERBOSE_LOG(G3D, "Scaled %i texels", texelsScaledThisFrame_);
	}
}

void TextureCacheVulkan::UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple) {
	const u32 clutBaseBytes = clutFormat == GE_CMODE_32BIT_ABGR8888 ? (clutBase * sizeof(u32)) : (clutBase * sizeof(u16));
	// Technically, these extra bytes weren't loaded, but hopefully it was loaded earlier.
	// If not, we're going to hash random data, which hopefully doesn't cause a performance issue.
	//
	// TODO: Actually, this seems like a hack.  The game can upload part of a CLUT and reference other data.
	// clutTotalBytes_ is the last amount uploaded.  We should hash clutMaxBytes_, but this will often hash
	// unrelated old entries for small palettes.
	// Adding clutBaseBytes may just be mitigating this for some usage patterns.
	const u32 clutExtendedBytes = std::min(clutTotalBytes_ + clutBaseBytes, clutMaxBytes_);

	if (replacer_.Enabled())
		clutHash_ = XXH32((const char *)clutBufRaw_, clutExtendedBytes, 0xC0108888);
	else
		clutHash_ = XXH3_64bits((const char *)clutBufRaw_, clutExtendedBytes) & 0xFFFFFFFF;
	clutBuf_ = clutBufRaw_;

	// Special optimization: fonts typically draw clut4 with just alpha values in a single color.
	clutAlphaLinear_ = false;
	clutAlphaLinearColor_ = 0;
	if (clutFormat == GE_CMODE_16BIT_ABGR4444 && clutIndexIsSimple) {
		const u16_le *clut = GetCurrentClut<u16_le>();
		clutAlphaLinear_ = true;
		clutAlphaLinearColor_ = clut[15] & 0x0FFF;
		for (int i = 0; i < 16; ++i) {
			u16 step = clutAlphaLinearColor_ | (i << 12);
			if (clut[i] != step) {
				clutAlphaLinear_ = false;
				break;
			}
		}
	}

	clutLastFormat_ = gstate.clutformat;
}

void TextureCacheVulkan::BindTexture(TexCacheEntry *entry) {
	if (!entry) {
		imageView_ = VK_NULL_HANDLE;
		curSampler_ = VK_NULL_HANDLE;
		return;
	}

	_dbg_assert_(entry->vkTex);
	entry->vkTex->Touch();

	int maxLevel = (entry->status & TexCacheEntry::STATUS_NO_MIPS) ? 0 : entry->maxLevel;
	SamplerCacheKey samplerKey = GetSamplingParams(maxLevel, entry);
	curSampler_ = samplerCache_.GetOrCreateSampler(samplerKey);
	imageView_ = entry->vkTex->GetImageView();
	drawEngine_->SetDepalTexture(VK_NULL_HANDLE, false);
	gstate_c.SetUseShaderDepal(false, false);
}

void TextureCacheVulkan::ApplySamplingParams(const SamplerCacheKey &key) {
	curSampler_ = samplerCache_.GetOrCreateSampler(key);
}

void TextureCacheVulkan::Unbind() {
	imageView_ = VK_NULL_HANDLE;
	curSampler_ = VK_NULL_HANDLE;
	InvalidateLastTexture();
}

void TextureCacheVulkan::BindAsClutTexture(Draw::Texture *tex, bool smooth) {
	VkImageView clutTexture = (VkImageView)draw_->GetNativeObject(Draw::NativeObject::TEXTURE_VIEW, tex);
	drawEngine_->SetDepalTexture(clutTexture, smooth);
}

static Draw::DataFormat FromVulkanFormat(VkFormat fmt) {
	switch (fmt) {
	case VULKAN_8888_FORMAT: default: return Draw::DataFormat::R8G8B8A8_UNORM;
	}
}

static VkFormat ToVulkanFormat(Draw::DataFormat fmt) {
	switch (fmt) {
	case Draw::DataFormat::R8G8B8A8_UNORM: default: return VULKAN_8888_FORMAT;
	}
}

void TextureCacheVulkan::BuildTexture(TexCacheEntry *const entry) {
	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);

	BuildTexturePlan plan;
	plan.hardwareScaling = g_Config.bTexHardwareScaling && uploadCS_ != VK_NULL_HANDLE;
	plan.slowScaler = !plan.hardwareScaling || vulkan->DevicePerfClass() == PerfClass::SLOW;
	if (!PrepareBuildTexture(plan, entry)) {
		// We're screwed?
		return;
	}

	VkFormat dstFmt = GetDestFormat(GETextureFormat(entry->format), gstate.getClutPaletteFormat());

	if (plan.scaleFactor > 1) {
		// Whether hardware or software scaling, this is the dest format.
		dstFmt = VULKAN_8888_FORMAT;
	}

	// We don't generate mipmaps for 512x512 textures because they're almost exclusively used for menu backgrounds
	// and similar, which don't really need it.
	if (g_Config.iTexFiltering == TEX_FILTER_AUTO_MAX_QUALITY && plan.w <= 256 && plan.h <= 256) {
		// Boost the number of mipmaps.
		if (plan.maxPossibleLevels > plan.levelsToCreate) {
			// We have to generate mips with a shader. This requires decoding to R8G8B8A8_UNORM format to avoid extra complications.
			dstFmt = VULKAN_8888_FORMAT;
		}
		plan.levelsToCreate = plan.maxPossibleLevels;
	}

	// Any texture scaling is gonna move away from the original 16-bit format, if any.
	VkFormat actualFmt = plan.scaleFactor > 1 ? VULKAN_8888_FORMAT : dstFmt;
	if (plan.replaceValid) {
		actualFmt = ToVulkanFormat(plan.replaced->Format(plan.baseLevelSrc));
	}

	bool computeUpload = false;
	VkCommandBuffer cmdInit = (VkCommandBuffer)draw_->GetNativeObject(Draw::NativeObject::INIT_COMMANDBUFFER);

	delete entry->vkTex;
	entry->vkTex = new VulkanTexture(vulkan);
	VulkanTexture *image = entry->vkTex;

	const VkComponentMapping *mapping;
	switch (actualFmt) {
	case VULKAN_4444_FORMAT: mapping = &VULKAN_4444_SWIZZLE; break;
	case VULKAN_1555_FORMAT: mapping = &VULKAN_1555_SWIZZLE; break;
	case VULKAN_565_FORMAT:  mapping = &VULKAN_565_SWIZZLE;  break;
	default:                 mapping = &VULKAN_8888_SWIZZLE; break;
	}

	VkImageLayout imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

	if (actualFmt == VULKAN_8888_FORMAT && plan.scaleFactor > 1 && plan.hardwareScaling) {
		if (uploadCS_ != VK_NULL_HANDLE) {
			computeUpload = true;
		} else {
			WARN_LOG(G3D, "Falling back to software scaling, hardware shader didn't compile");
		}
	}

	if (computeUpload) {
		usage |= VK_IMAGE_USAGE_STORAGE_BIT;
		imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	}

	char texName[128]{};
	snprintf(texName, sizeof(texName), "tex_%08x_%s", entry->addr, GeTextureFormatToString((GETextureFormat)entry->format, gstate.getClutPaletteFormat()));
	image->SetTag(texName);

	bool allocSuccess = image->CreateDirect(cmdInit, plan.createW, plan.createH, plan.depth, plan.levelsToCreate, actualFmt, imageLayout, usage, mapping);
	if (!allocSuccess && !lowMemoryMode_) {
		WARN_LOG_REPORT(G3D, "Texture cache ran out of GPU memory; switching to low memory mode");
		lowMemoryMode_ = true;
		decimationCounter_ = 0;
		Decimate();
		// TODO: We should stall the GPU here and wipe things out of memory.
		// As is, it will almost definitely fail the second time, but next frame it may recover.

		auto err = GetI18NCategory("Error");
		if (plan.scaleFactor > 1) {
			host->NotifyUserMessage(err->T("Warning: Video memory FULL, reducing upscaling and switching to slow caching mode"), 2.0f);
		} else {
			host->NotifyUserMessage(err->T("Warning: Video memory FULL, switching to slow caching mode"), 2.0f);
		}

		// Turn off texture replacement for this texture.
		plan.replaced = &replacer_.FindNone();

		plan.createW /= plan.scaleFactor;
		plan.createH /= plan.scaleFactor;
		plan.scaleFactor = 1;
		actualFmt = dstFmt;

		allocSuccess = image->CreateDirect(cmdInit, plan.createW, plan.createH, plan.depth, plan.levelsToCreate, actualFmt, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, mapping);
	}

	if (!allocSuccess) {
		ERROR_LOG(G3D, "Failed to create texture (%dx%d)", plan.w, plan.h);
		delete entry->vkTex;
		entry->vkTex = nullptr;
	}

	if (!entry->vkTex) {
		return;
	}

	ReplacedTextureDecodeInfo replacedInfo;
	bool willSaveTex = false;
	if (replacer_.Enabled() && !plan.replaceValid && plan.depth == 1) {
		// TODO: Do we handle the race where a replacement becomes valid AFTER this but before we save?
		replacedInfo.cachekey = entry->CacheKey();
		replacedInfo.hash = entry->fullhash;
		replacedInfo.addr = entry->addr;
		replacedInfo.isVideo = plan.isVideo;
		replacedInfo.isFinal = (entry->status & TexCacheEntry::STATUS_TO_SCALE) == 0;
		replacedInfo.scaleFactor = plan.scaleFactor;
		replacedInfo.fmt = FromVulkanFormat(actualFmt);
		willSaveTex = replacer_.WillSave(replacedInfo);
	}

	VK_PROFILE_BEGIN(vulkan, cmdInit, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		"Texture Upload (%08x) video=%d", entry->addr, plan.isVideo);

	// Upload the texture data. We simply reuse the same loop for 3D texture slices instead of mips, if we have those.
	int levels;
	if (plan.depth > 1) {
		levels = plan.depth;
	} else {
		levels = plan.levelsToLoad;
	}

	for (int i = 0; i < levels; i++) {
		int mipUnscaledWidth = gstate.getTextureWidth(i);
		int mipUnscaledHeight = gstate.getTextureHeight(i);

		int mipWidth;
		int mipHeight;
		plan.GetMipSize(i, &mipWidth, &mipHeight);

		int bpp = actualFmt == VULKAN_8888_FORMAT ? 4 : 2;  // output bpp
		int stride = (mipWidth * bpp + 15) & ~15;  // output stride
		int uploadSize = stride * mipHeight;

		uint32_t bufferOffset;
		VkBuffer texBuf;
		// NVIDIA reports a min alignment of 1 but that can't be healthy... let's align by 16 as a minimum.
		int pushAlignment = std::max(16, (int)vulkan->GetPhysicalDeviceProperties().properties.limits.optimalBufferCopyOffsetAlignment);
		void *data;
		std::vector<uint8_t> saveData;

		auto loadLevel = [&](int sz, int srcLevel, int lstride, int lfactor) {
			if (willSaveTex) {
				saveData.resize(sz);
				data = &saveData[0];
			} else {
				data = drawEngine_->GetPushBufferForTextureData()->PushAligned(sz, &bufferOffset, &texBuf, pushAlignment);
			}
			LoadTextureLevel(*entry, (uint8_t *)data, lstride, srcLevel, lfactor, dstFmt);
			if (willSaveTex)
				bufferOffset = drawEngine_->GetPushBufferForTextureData()->PushAligned(&saveData[0], sz, pushAlignment, &texBuf);
		};

		bool dataScaled = true;
		if (plan.replaceValid) {
			// Directly load the replaced image.
			data = drawEngine_->GetPushBufferForTextureData()->PushAligned(uploadSize, &bufferOffset, &texBuf, pushAlignment);
			double replaceStart = time_now_d();
			plan.replaced->Load(plan.baseLevelSrc + i, data, stride);  // if it fails, it'll just be garbage data... OK for now.
			replacementTimeThisFrame_ += time_now_d() - replaceStart;
			VK_PROFILE_BEGIN(vulkan, cmdInit, VK_PIPELINE_STAGE_TRANSFER_BIT,
				"Copy Upload (replaced): %dx%d", mipWidth, mipHeight);
			entry->vkTex->UploadMip(cmdInit, i, mipWidth, mipHeight, 0, texBuf, bufferOffset, stride / bpp);
			VK_PROFILE_END(vulkan, cmdInit, VK_PIPELINE_STAGE_TRANSFER_BIT);
		} else {
			if (plan.depth != 1) {
				// 3D texturing.
				loadLevel(uploadSize, i, stride, plan.scaleFactor);
				entry->vkTex->UploadMip(cmdInit, 0, mipWidth, mipHeight, i, texBuf, bufferOffset, stride / bpp);
			} else if (computeUpload) {
				int srcBpp = dstFmt == VULKAN_8888_FORMAT ? 4 : 2;
				int srcStride = mipUnscaledWidth * srcBpp;
				int srcSize = srcStride * mipUnscaledHeight;
				loadLevel(srcSize, i == 0 ? plan.baseLevelSrc : i, srcStride, 1);
				dataScaled = false;

				// This format can be used with storage images.
				VkImageView view = entry->vkTex->CreateViewForMip(i);
				VkDescriptorSet descSet = computeShaderManager_.GetDescriptorSet(view, texBuf, bufferOffset, srcSize);
				struct Params { int x; int y; } params{ mipUnscaledWidth, mipUnscaledHeight };
				VK_PROFILE_BEGIN(vulkan, cmdInit, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					"Compute Upload: %dx%d->%dx%d", mipUnscaledWidth, mipUnscaledHeight, mipWidth, mipHeight);
				vkCmdBindPipeline(cmdInit, VK_PIPELINE_BIND_POINT_COMPUTE, computeShaderManager_.GetPipeline(uploadCS_));
				vkCmdBindDescriptorSets(cmdInit, VK_PIPELINE_BIND_POINT_COMPUTE, computeShaderManager_.GetPipelineLayout(), 0, 1, &descSet, 0, nullptr);
				vkCmdPushConstants(cmdInit, computeShaderManager_.GetPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
				vkCmdDispatch(cmdInit, (mipUnscaledWidth + 7) / 8, (mipUnscaledHeight + 7) / 8, 1);
				VK_PROFILE_END(vulkan, cmdInit, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				vulkan->Delete().QueueDeleteImageView(view);
			} else {
				loadLevel(uploadSize, i == 0 ? plan.baseLevelSrc : i, stride, plan.scaleFactor);
				VK_PROFILE_BEGIN(vulkan, cmdInit, VK_PIPELINE_STAGE_TRANSFER_BIT,
					"Copy Upload: %dx%d", mipWidth, mipHeight);
				entry->vkTex->UploadMip(cmdInit, i, mipWidth, mipHeight, 0, texBuf, bufferOffset, stride / bpp);
				VK_PROFILE_END(vulkan, cmdInit, VK_PIPELINE_STAGE_TRANSFER_BIT);
			}
			if (replacer_.Enabled()) {
				// When hardware texture scaling is enabled, this saves the original.
				int w = dataScaled ? mipWidth : mipUnscaledWidth;
				int h = dataScaled ? mipHeight : mipUnscaledHeight;
				// At this point, data should be saveData, and not slow.
				replacer_.NotifyTextureDecoded(replacedInfo, data, stride, plan.baseLevelSrc + i, w, h);
			}
		}
	}

	VkImageLayout layout = computeUpload ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	VkPipelineStageFlags prevStage = computeUpload ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT;

	// Generate any additional mipmap levels.
	// This will transition the whole stack to GENERAL if it wasn't already.
	if (plan.levelsToLoad < plan.levelsToCreate) {
		VK_PROFILE_BEGIN(vulkan, cmdInit, VK_PIPELINE_STAGE_TRANSFER_BIT, "Mipgen up to level %d", plan.levelsToCreate);
		entry->vkTex->GenerateMips(cmdInit, plan.levelsToLoad, computeUpload);
		layout = VK_IMAGE_LAYOUT_GENERAL;
		prevStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		VK_PROFILE_END(vulkan, cmdInit, VK_PIPELINE_STAGE_TRANSFER_BIT);
	}

	entry->vkTex->EndCreate(cmdInit, false, prevStage, layout);
	VK_PROFILE_END(vulkan, cmdInit, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

	// Signal that we support depth textures so use it as one.
	if (plan.depth > 1) {
		entry->status |= TexCacheEntry::STATUS_3D;
	}

	if (plan.replaceValid) {
		entry->SetAlphaStatus(TexCacheEntry::TexStatus(plan.replaced->AlphaStatus()));
	}
}

VkFormat TextureCacheVulkan::GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const {
	if (!gstate_c.Supports(GPU_SUPPORTS_16BIT_FORMATS)) {
		return VK_FORMAT_R8G8B8A8_UNORM;
	}
	switch (format) {
	case GE_TFMT_CLUT4:
	case GE_TFMT_CLUT8:
	case GE_TFMT_CLUT16:
	case GE_TFMT_CLUT32:
		return getClutDestFormatVulkan(clutFormat);
	case GE_TFMT_4444:
		return VULKAN_4444_FORMAT;
	case GE_TFMT_5551:
		return VULKAN_1555_FORMAT;
	case GE_TFMT_5650:
		return VULKAN_565_FORMAT;
	case GE_TFMT_8888:
	case GE_TFMT_DXT1:
	case GE_TFMT_DXT3:
	case GE_TFMT_DXT5:
	default:
		return VULKAN_8888_FORMAT;
	}
}

CheckAlphaResult TextureCacheVulkan::CheckAlpha(const u32 *pixelData, VkFormat dstFmt, int w) {
	switch (dstFmt) {
	case VULKAN_4444_FORMAT:
		return CheckAlpha16((const u16 *)pixelData, w, 0xF000);
	case VULKAN_1555_FORMAT:
		return CheckAlpha16((const u16 *)pixelData, w, 0x8000);
	case VULKAN_565_FORMAT:
		// Never has any alpha.
		return CHECKALPHA_FULL;
	default:
		return CheckAlpha32(pixelData, w, 0xFF000000);
	}
}

void TextureCacheVulkan::LoadTextureLevel(TexCacheEntry &entry, uint8_t *writePtr, int rowPitch, int level, int scaleFactor, VkFormat dstFmt) {
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	GETextureFormat tfmt = (GETextureFormat)entry.format;
	GEPaletteFormat clutformat = gstate.getClutPaletteFormat();
	u32 texaddr = gstate.getTextureAddress(level);

	_assert_msg_(texaddr != 0, "Can't load a texture from address null")

	int bufw = GetTextureBufw(level, texaddr, tfmt);
	int bpp = dstFmt == VULKAN_8888_FORMAT ? 4 : 2;

	u32 *pixelData;
	int decPitch;

	bool expand32 = !gstate_c.Supports(GPU_SUPPORTS_16BIT_FORMATS) || scaleFactor > 1 || dstFmt == VULKAN_8888_FORMAT;

	if (scaleFactor > 1) {
		tmpTexBufRearrange_.resize(std::max(bufw, w) * h);
		pixelData = tmpTexBufRearrange_.data();
		// We want to end up with a neatly packed texture for scaling.
		decPitch = w * bpp;
	} else {
		pixelData = (u32 *)writePtr;
		decPitch = rowPitch;
	}

	CheckAlphaResult alphaResult = DecodeTextureLevel((u8 *)pixelData, decPitch, tfmt, clutformat, texaddr, level, bufw, false, expand32);
	entry.SetAlphaStatus(alphaResult, level);

	if (scaleFactor > 1) {
		u32 fmt = dstFmt;
		// CPU scaling reads from the destination buffer so we want cached RAM.
		uint8_t *rearrange = (uint8_t *)AllocateAlignedMemory(w * scaleFactor * h * scaleFactor * 4, 16);
		scaler_.ScaleAlways((u32 *)rearrange, pixelData, w, h, scaleFactor);
		pixelData = (u32 *)writePtr;

		// We always end up at 8888.  Other parts assume this.
		_assert_(dstFmt == VULKAN_8888_FORMAT);
		bpp = sizeof(u32);
		decPitch = w * bpp;

		if (decPitch != rowPitch) {
			for (int y = 0; y < h; ++y) {
				memcpy(writePtr + rowPitch * y, rearrange + decPitch * y, w * bpp);
			}
			decPitch = rowPitch;
		} else {
			memcpy(writePtr, rearrange, w * h * 4);
		}
		FreeAlignedMemory(rearrange);
	}
}

void TextureCacheVulkan::BoundFramebufferTexture() {
	imageView_ = (VkImageView)draw_->GetNativeObject(Draw::NativeObject::BOUND_TEXTURE0_IMAGEVIEW);
}

bool TextureCacheVulkan::GetCurrentTextureDebug(GPUDebugBuffer &buffer, int level) {
	SetTexture();
	if (!nextTexture_) {
		if (nextFramebufferTexture_) {
			VirtualFramebuffer *vfb = nextFramebufferTexture_;
			buffer.Allocate(vfb->bufferWidth, vfb->bufferHeight, GPU_DBG_FORMAT_8888, false);
			bool retval = draw_->CopyFramebufferToMemorySync(vfb->fbo, Draw::FB_COLOR_BIT, 0, 0, vfb->bufferWidth, vfb->bufferHeight, Draw::DataFormat::R8G8B8A8_UNORM, buffer.GetData(), vfb->bufferWidth, "GetCurrentTextureDebug");
			// Vulkan requires us to re-apply all dynamic state for each command buffer, and the above will cause us to start a new cmdbuf.
			// So let's dirty the things that are involved in Vulkan dynamic state. Readbacks are not frequent so this won't hurt other backends.
			gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE);
			// We may have blitted to a temp FBO.
			framebufferManager_->RebindFramebuffer("RebindFramebuffer - GetCurrentTextureDebug");
			if (!retval)
				ERROR_LOG(G3D, "Failed to get debug texture: copy to memory failed");
			return retval;
		} else {
			return false;
		}
	}

	// Apply texture may need to rebuild the texture if we're about to render, or bind a framebuffer.
	TexCacheEntry *entry = nextTexture_;
	ApplyTexture();

	if (!entry->vkTex)
		return false;

	VulkanTexture *texture = entry->vkTex;
	VulkanRenderManager *renderManager = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);

	GPUDebugBufferFormat bufferFormat;
	Draw::DataFormat drawFormat;
	switch (texture->GetFormat()) {
	case VULKAN_565_FORMAT:
		bufferFormat = GPU_DBG_FORMAT_565;
		drawFormat = Draw::DataFormat::B5G6R5_UNORM_PACK16;
		break;
	case VULKAN_1555_FORMAT:
		bufferFormat = GPU_DBG_FORMAT_5551;
		drawFormat = Draw::DataFormat::B5G5R5A1_UNORM_PACK16;
		break;
	case VULKAN_4444_FORMAT:
		bufferFormat = GPU_DBG_FORMAT_4444;
		drawFormat = Draw::DataFormat::B4G4R4A4_UNORM_PACK16;
		break;
	case VULKAN_8888_FORMAT:
	default:
		bufferFormat = GPU_DBG_FORMAT_8888;
		drawFormat = Draw::DataFormat::R8G8B8A8_UNORM;
		break;
	}

	int w = texture->GetWidth();
	int h = texture->GetHeight();
	if (level > 0) {
		// In the future, maybe this could do something for 3D textures...
		if (level >= texture->GetNumMips())
			return false;
		w >>= level;
		h >>= level;
	}
	buffer.Allocate(w, h, bufferFormat);

	renderManager->CopyImageToMemorySync(texture->GetImage(), level, 0, 0, w, h, drawFormat, (uint8_t *)buffer.GetData(), w, "GetCurrentTextureDebug");

	// Vulkan requires us to re-apply all dynamic state for each command buffer, and the above will cause us to start a new cmdbuf.
	// So let's dirty the things that are involved in Vulkan dynamic state. Readbacks are not frequent so this won't hurt other backends.
	gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE);
	framebufferManager_->RebindFramebuffer("RebindFramebuffer - GetCurrentTextureDebug");
	return true;
}

void TextureCacheVulkan::GetStats(char *ptr, size_t size) {
	snprintf(ptr, size, "N/A");
}

std::vector<std::string> TextureCacheVulkan::DebugGetSamplerIDs() const {
	return samplerCache_.DebugGetSamplerIDs();
}

std::string TextureCacheVulkan::DebugGetSamplerString(std::string id, DebugShaderStringType stringType) {
	return samplerCache_.DebugGetSamplerString(id, stringType);
}
