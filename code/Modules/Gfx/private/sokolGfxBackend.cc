//------------------------------------------------------------------------------
//  sokolGfxBackend.cc
//------------------------------------------------------------------------------
#include "Pre.h"
#include "Core/Core.h"
#include "Core/Assertion.h"
#include "Core/Memory/Memory.h"
#define SOKOL_IMPL
#define SOKOL_ASSERT(c) o_assert_dbg(c)
#define SOKOL_MALLOC(s) Oryol::Memory::Alloc(s)
#define SOKOL_FREE(p) Oryol::Memory::Free(p)
#define SOKOL_LOG(m) Oryol::Log::Info("%s\n",m)
#define SOKOL_UNREACHABLE o_assert_dbg(false)
#if ORYOL_OPENGL
    #if ORYOL_RASPBERRYPI
    #define GL_GLEXT_PROTOTYPES
    #include "GLES2/gl2.h"
    #include "GLES2/gl2ext.h"
    #elif ORYOL_WINDOWS || ORYOL_LINUX || ORYOL_MACOS
    #include "Gfx/private/flextGL.h"
    #elif ORYOL_IOS
    #include <OpenGLES/ES3/gl.h>
    #include <OpenGLES/ES3/glext.h>
    #elif ORYOL_EMSCRIPTEN
        #if ORYOL_OPENGLES2
        #define GL_GLEXT_PROTOTYPES
        #include <GL/gl.h>
        #include <GL/glext.h>
        #else
        #include <GLES3/gl3.h>
        #endif
    #elif ORYOL_ANDROID
    #define GL_GLEXT_PROTOTYPES
    #include <GLES3/gl3.h>
    #include <GLES3/gl3ext.h>
    #else
    #error "Missing platform for GL header include!"
    #endif
#endif
#if ORYOL_OPENGLES2
#define SOKKOL_GLES2
#elif ORYOL_OPENGL3
#define SOKOL_GLES3
#elif ORYOL_OPENGL_CORE_PROFILE
#define SOKOL_GLCORE33
#elif ORYOL_D3D11
#define SOKOL_D3D11
#elif ORYOL_METAL
#if ORYOL_MACOS
#define SOKOL_METAL_MACOS
#else
#define SOKOL_METAL_IOS
#endif
#endif
#include "sokolGfxBackend.h"

namespace Oryol {
namespace _priv {

//------------------------------------------------------------------------------
static Id makeId(GfxResourceType::Code type, uint32_t sgId) {
    // convert a Sokol resource id into a Oryol Id
    Id::SlotIndexT slotIndex = sgId & 0xFFFF;
    Id::UniqueStampT unique = (sgId >> 16) & 0xFFFF;
    Id id(unique, slotIndex, type);
    return id;
}

//------------------------------------------------------------------------------
static sg_shader makeShaderId(const Id& id) {
    // convert an Oryol Id into a sokol sg_shader
    o_assert_dbg(id.Type == GfxResourceType::Shader);
    uint32_t sgId = (id.UniqueStamp<<16)|id.SlotIndex;
    return sg_shader{sgId};
}

//------------------------------------------------------------------------------
static sg_pipeline makePipelineId(const Id& id) {
    // convert an Oryol Id into a sokol sg_pipeline
    o_assert_dbg(id.Type == GfxResourceType::Pipeline);
    uint32_t sgId = (id.UniqueStamp<<16)|id.SlotIndex;
    return sg_pipeline{sgId};
}

//------------------------------------------------------------------------------
static sg_buffer makeBufferId(const Id& id) {
    // convert an Oryol Id into a sokol sg_buffer
    o_assert_dbg(id.Type == GfxResourceType::Buffer);
    uint32_t sgId = (id.UniqueStamp<<16)|id.SlotIndex;
    return sg_buffer{sgId};
}

//------------------------------------------------------------------------------
static sg_image makeImageId(const Id& id) {
    // convert an Oryol Id into a sokol sg_image
    o_assert_dbg(id.Type == GfxResourceType::Texture);
    uint32_t sgId = (id.UniqueStamp<<16)|id.SlotIndex;
    return sg_image{sgId};
}

//------------------------------------------------------------------------------
static void convertPassAction(const PassAction& src, sg_pass_action& dst) {
    o_assert_dbg(GfxConfig::MaxNumColorAttachments <= SG_MAX_COLOR_ATTACHMENTS);
    for (int i = 0; i < GfxConfig::MaxNumColorAttachments; i++) {
        for (int c = 0; c < 4; c++) {
            dst.colors[i].val[c] = src.Color[i][c];
        }
        if (src.Flags & (PassAction::ClearC0<<i)) {
            dst.colors[i].action = SG_ACTION_CLEAR;
        }
        else if (src.Flags & (PassAction::LoadC0<<i)) {
            dst.colors[i].action = SG_ACTION_LOAD;
        }
        else {
            dst.colors[i].action = SG_ACTION_DONTCARE;
        }
    }
    dst.depth.val = src.Depth;
    dst.stencil.val = src.Stencil;
    if (src.Flags & PassAction::ClearDS) {
        dst.depth.action = SG_ACTION_CLEAR;
    }
    else if (src.Flags & PassAction::LoadDS) {
        dst.depth.action = SG_ACTION_LOAD;
    }
    else {
        dst.depth.action = SG_ACTION_DONTCARE;
    }
}

//------------------------------------------------------------------------------
static sg_buffer_type convertBufferType(BufferType::Code t) {
    switch (t) {
        case BufferType::VertexBuffer:  return SG_BUFFERTYPE_VERTEXBUFFER;
        case BufferType::IndexBuffer:   return SG_BUFFERTYPE_INDEXBUFFER;
        default: return _SG_BUFFERTYPE_DEFAULT;
    }
}

//------------------------------------------------------------------------------
static sg_usage convertUsage(Usage::Code u) {
    switch (u) {
        case Usage::Immutable:  return SG_USAGE_IMMUTABLE;
        case Usage::Dynamic:    return SG_USAGE_DYNAMIC;
        case Usage::Stream:     return SG_USAGE_STREAM;
        default: return _SG_USAGE_DEFAULT;
    }
}

//------------------------------------------------------------------------------
static sg_primitive_type convertPrimitiveType(PrimitiveType::Code t) {
    switch (t) {
        case PrimitiveType::Points:         return SG_PRIMITIVETYPE_POINTS;
        case PrimitiveType::Lines:          return SG_PRIMITIVETYPE_LINES;
        case PrimitiveType::LineStrip:      return SG_PRIMITIVETYPE_LINE_STRIP;
        case PrimitiveType::Triangles:      return SG_PRIMITIVETYPE_TRIANGLES;
        case PrimitiveType::TriangleStrip:  return SG_PRIMITIVETYPE_TRIANGLE_STRIP;
        default: return _SG_PRIMITIVETYPE_DEFAULT;
    }{}
}

//------------------------------------------------------------------------------
static sg_index_type convertIndexType(IndexType::Code t) {
    switch (t) {
        case IndexType::None:               return SG_INDEXTYPE_NONE;
        case IndexType::UInt16:             return SG_INDEXTYPE_UINT16;
        case IndexType::UInt32:             return SG_INDEXTYPE_UINT32;
        default: return _SG_INDEXTYPE_DEFAULT;  
    }
}

//------------------------------------------------------------------------------
static sg_stencil_op convertStencilOp(StencilOp::Code op) {
    switch (op) {
        case StencilOp::Keep:       return SG_STENCILOP_KEEP;
        case StencilOp::Zero:       return SG_STENCILOP_ZERO;
        case StencilOp::Replace:    return SG_STENCILOP_REPLACE;
        case StencilOp::IncrClamp:  return SG_STENCILOP_INCR_CLAMP;
        case StencilOp::DecrClamp:  return SG_STENCILOP_DECR_CLAMP;
        case StencilOp::Invert:     return SG_STENCILOP_INVERT;
        case StencilOp::IncrWrap:   return SG_STENCILOP_INCR_WRAP;
        case StencilOp::DecrWrap:   return SG_STENCILOP_DECR_WRAP;
        default: return _SG_STENCILOP_DEFAULT;
    }
}

//------------------------------------------------------------------------------
static sg_compare_func convertCompareFunc(CompareFunc::Code f) {
    switch (f) {
        case CompareFunc::Never:        return SG_COMPAREFUNC_NEVER;
        case CompareFunc::Less:         return SG_COMPAREFUNC_LESS;
        case CompareFunc::Equal:        return SG_COMPAREFUNC_EQUAL;
        case CompareFunc::LessEqual:    return SG_COMPAREFUNC_LESS_EQUAL;
        case CompareFunc::Greater:      return SG_COMPAREFUNC_GREATER;
        case CompareFunc::NotEqual:     return SG_COMPAREFUNC_NOT_EQUAL;
        case CompareFunc::GreaterEqual: return SG_COMPAREFUNC_GREATER_EQUAL;
        case CompareFunc::Always:       return SG_COMPAREFUNC_ALWAYS;
        default: return _SG_COMPAREFUNC_DEFAULT;
    }
}

//------------------------------------------------------------------------------
static void convertDepthStencilState(const PipelineDesc& src, sg_pipeline_desc& dst) {
    dst.depth_stencil.stencil_front.fail_op = convertStencilOp(src.StencilFrontFailOp);
    dst.depth_stencil.stencil_front.depth_fail_op = convertStencilOp(src.StencilFrontDepthFailOp);
    dst.depth_stencil.stencil_front.pass_op = convertStencilOp(src.StencilFrontPassOp);
    dst.depth_stencil.stencil_front.compare_func = convertCompareFunc(src.StencilFrontCmpFunc);
    dst.depth_stencil.stencil_back.fail_op = convertStencilOp(src.StencilBackFailOp);
    dst.depth_stencil.stencil_back.depth_fail_op = convertStencilOp(src.StencilBackDepthFailOp);
    dst.depth_stencil.stencil_back.pass_op = convertStencilOp(src.StencilBackPassOp);
    dst.depth_stencil.stencil_back.compare_func = convertCompareFunc(src.StencilBackCmpFunc);
    dst.depth_stencil.depth_compare_func = convertCompareFunc(src.DepthCmpFunc);
    dst.depth_stencil.depth_write_enabled = src.DepthWriteEnabled;
    dst.depth_stencil.stencil_enabled = src.StencilEnabled;
    dst.depth_stencil.stencil_read_mask = src.StencilReadMask;
    dst.depth_stencil.stencil_write_mask = src.StencilWriteMask;
    dst.depth_stencil.stencil_ref = src.StencilRef;
}

//------------------------------------------------------------------------------
static sg_blend_factor convertBlendFactor(BlendFactor::Code f) {
    switch (f) {
        case BlendFactor::Zero:                 return SG_BLENDFACTOR_ZERO;
        case BlendFactor::One:                  return SG_BLENDFACTOR_ONE;
        case BlendFactor::SrcColor:             return SG_BLENDFACTOR_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor:     return SG_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::SrcAlpha:             return SG_BLENDFACTOR_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:     return SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstColor:             return SG_BLENDFACTOR_DST_COLOR;
        case BlendFactor::OneMinusDstColor:     return SG_BLENDFACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::DstAlpha:             return SG_BLENDFACTOR_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha:     return SG_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
        case BlendFactor::SrcAlphaSaturated:    return SG_BLENDFACTOR_SRC_ALPHA_SATURATED;
        case BlendFactor::BlendColor:           return SG_BLENDFACTOR_BLEND_COLOR;
        case BlendFactor::OneMinusBlendColor:   return SG_BLENDFACTOR_ONE_MINUS_BLEND_COLOR;
        case BlendFactor::BlendAlpha:           return SG_BLENDFACTOR_BLEND_ALPHA;
        case BlendFactor::OneMinusBlendAlpha:   return SG_BLENDFACTOR_ONE_MINUS_BLEND_ALPHA;
        default: return _SG_BLENDFACTOR_DEFAULT;
    }
}

//------------------------------------------------------------------------------
static sg_blend_op convertBlendOp(BlendOperation::Code op) {
    switch (op) {
        case BlendOperation::Add:               return SG_BLENDOP_ADD;
        case BlendOperation::Subtract:          return SG_BLENDOP_SUBTRACT;
        case BlendOperation::ReverseSubtract:   return SG_BLENDOP_REVERSE_SUBTRACT;
        default: return _SG_BLENDOP_DEFAULT;
    }
}

//------------------------------------------------------------------------------
static uint8_t convertColorMask(PixelChannel::Mask src) {
    if (src == 0) {
        return SG_COLORMASK_NONE;
    }
    else {
        uint8_t dst = 0;
        if (src & PixelChannel::Red)    { dst |= SG_COLORMASK_R; }
        if (src & PixelChannel::Green)  { dst |= SG_COLORMASK_G; }
        if (src & PixelChannel::Blue)   { dst |= SG_COLORMASK_B; }
        if (src & PixelChannel::Alpha)  { dst |= SG_COLORMASK_A; }
        return dst;
    }
}

//------------------------------------------------------------------------------
static sg_pixel_format convertPixelFormat(PixelFormat::Code fmt) {
    switch (fmt) {
        case PixelFormat::RGBA8:        return SG_PIXELFORMAT_RGBA8;
        case PixelFormat::RGB8:         return SG_PIXELFORMAT_RGB8;
        case PixelFormat::RGBA4:        return SG_PIXELFORMAT_RGBA4;
        case PixelFormat::R5G6B5:       return SG_PIXELFORMAT_R5G6B5;
        case PixelFormat::R5G5B5A1:     return SG_PIXELFORMAT_R5G5B5A1;
        case PixelFormat::R10G10B10A2:  return SG_PIXELFORMAT_R10G10B10A2;
        case PixelFormat::RGBA32F:      return SG_PIXELFORMAT_RGBA32F;
        case PixelFormat::RGBA16F:      return SG_PIXELFORMAT_RGBA16F;
        case PixelFormat::R32F:         return SG_PIXELFORMAT_R32F;
        case PixelFormat::L8:           return SG_PIXELFORMAT_L8;
        case PixelFormat::DXT1:         return SG_PIXELFORMAT_DXT1;
        case PixelFormat::DXT3:         return SG_PIXELFORMAT_DXT3;
        case PixelFormat::DXT5:         return SG_PIXELFORMAT_DXT5;
        case PixelFormat::DEPTH:        return SG_PIXELFORMAT_DEPTH;
        case PixelFormat::DEPTHSTENCIL: return SG_PIXELFORMAT_DEPTHSTENCIL;
        case PixelFormat::PVRTC2_RGB:   return SG_PIXELFORMAT_PVRTC2_RGB;
        case PixelFormat::PVRTC4_RGB:   return SG_PIXELFORMAT_PVRTC4_RGB;
        case PixelFormat::PVRTC2_RGBA:  return SG_PIXELFORMAT_PVRTC2_RGBA;
        case PixelFormat::PVRTC4_RGBA:  return SG_PIXELFORMAT_PVRTC4_RGBA;
        case PixelFormat::ETC2_RGB8:    return SG_PIXELFORMAT_ETC2_RGB8;
        case PixelFormat::ETC2_SRGB8:   return SG_PIXELFORMAT_ETC2_SRGB8;
        default: return _SG_PIXELFORMAT_DEFAULT;
    }
}

//------------------------------------------------------------------------------
static void convertBlendState(const PipelineDesc& src, sg_pipeline_desc& dst) {
    dst.blend.enabled = src.BlendEnabled;
    dst.blend.src_factor_rgb = convertBlendFactor(src.BlendSrcFactorRGB);
    dst.blend.dst_factor_rgb = convertBlendFactor(src.BlendDstFactorRGB);
    dst.blend.op_rgb = convertBlendOp(src.BlendOpRGB);
    dst.blend.src_factor_alpha = convertBlendFactor(src.BlendSrcFactorAlpha);
    dst.blend.dst_factor_alpha = convertBlendFactor(src.BlendDstFactorAlpha);
    dst.blend.op_alpha = convertBlendOp(src.BlendOpAlpha);
    dst.blend.color_write_mask = convertColorMask(src.ColorWriteMask);
    dst.blend.color_attachment_count = src.MRTCount;
    dst.blend.color_format = convertPixelFormat(src.ColorFormat);
    dst.blend.depth_format = convertPixelFormat(src.DepthFormat);
    dst.blend.blend_color[0] = src.BlendColor.r;
    dst.blend.blend_color[1] = src.BlendColor.g;
    dst.blend.blend_color[2] = src.BlendColor.b;
    dst.blend.blend_color[3] = src.BlendColor.a;
}

//------------------------------------------------------------------------------
static sg_cull_mode convertCullMode(bool enabled, Face::Code face) {
    if (enabled) {
        if (face == Face::Front) { 
            return SG_CULLMODE_FRONT; 
        }
        else { 
            return SG_CULLMODE_BACK;
        }
    }
    else {
        return SG_CULLMODE_NONE;
    }
}

//------------------------------------------------------------------------------
static void convertRasterizerState(const PipelineDesc& src, sg_pipeline_desc& dst) {
    dst.rasterizer.alpha_to_coverage_enabled = src.AlphaToCoverageEnabled;
    dst.rasterizer.cull_mode = convertCullMode(src.CullFaceEnabled, src.CullFace);
    dst.rasterizer.face_winding = _SG_FACEWINDING_DEFAULT;
    dst.rasterizer.sample_count = src.SampleCount;
    dst.rasterizer.depth_bias = 0.0f;
    dst.rasterizer.depth_bias_slope_scale = 0.0f;
    dst.rasterizer.depth_bias_clamp = 0.0f;
}

//------------------------------------------------------------------------------
static sg_vertex_step convertStepFunc(VertexStepFunction::Code f) {
    switch (f) {
        case VertexStepFunction::PerVertex:     return SG_VERTEXSTEP_PER_VERTEX;
        case VertexStepFunction::PerInstance:   return SG_VERTEXSTEP_PER_INSTANCE;
        default: return _SG_VERTEXSTEP_DEFAULT;
    }
}

//------------------------------------------------------------------------------
static sg_vertex_format convertVertexFormat(VertexFormat::Code fmt) {
    switch (fmt) {
        case VertexFormat::Float:       return SG_VERTEXFORMAT_FLOAT;
        case VertexFormat::Float2:      return SG_VERTEXFORMAT_FLOAT2;
        case VertexFormat::Float3:      return SG_VERTEXFORMAT_FLOAT3;
        case VertexFormat::Float4:      return SG_VERTEXFORMAT_FLOAT4;
        case VertexFormat::Byte4:       return SG_VERTEXFORMAT_BYTE4;
        case VertexFormat::Byte4N:      return SG_VERTEXFORMAT_BYTE4N;
        case VertexFormat::UByte4:      return SG_VERTEXFORMAT_UBYTE4;
        case VertexFormat::UByte4N:     return SG_VERTEXFORMAT_UBYTE4N;
        case VertexFormat::Short2:      return SG_VERTEXFORMAT_SHORT2;
        case VertexFormat::Short2N:     return SG_VERTEXFORMAT_SHORT2N;
        case VertexFormat::Short4:      return SG_VERTEXFORMAT_SHORT4;
        case VertexFormat::Short4N:     return SG_VERTEXFORMAT_SHORT4N;
        case VertexFormat::UInt10_2N:   return SG_VERTEXFORMAT_UINT10_N2;
        default: return SG_VERTEXFORMAT_INVALID;
    }
}

//------------------------------------------------------------------------------
static void convertVertexLayouts(const PipelineDesc& src, sg_pipeline_desc& dst) {
    o_assert_dbg(GfxConfig::MaxNumVertexBuffers <= SG_MAX_SHADERSTAGE_BUFFERS);
    for (int layoutIndex = 0; layoutIndex < GfxConfig::MaxNumVertexBuffers; layoutIndex++) {
        const auto& srcLayout = src.Layouts[layoutIndex];
        auto& dstLayout = dst.vertex_layouts[layoutIndex];
        if (!srcLayout.Empty()) {
            dstLayout.stride = srcLayout.ByteSize();
            dstLayout.step_func = convertStepFunc(srcLayout.StepFunction);
            dstLayout.step_rate = srcLayout.StepRate;
            for (int compIndex = 0; compIndex < srcLayout.NumComponents(); compIndex++) {
                const auto& srcComp = srcLayout.ComponentAt(compIndex);
                auto& dstComp = dstLayout.attrs[compIndex];
                if (srcComp.Name.IsValid()) {
                    dstComp.name = srcComp.Name.AsCStr();
                }
                // FIXME: sem_name, sem_index (D3D11!)
                dstComp.offset = srcComp.Offset;
                dstComp.format = convertVertexFormat(srcComp.Format);
            }
        }
    }
}

//------------------------------------------------------------------------------
static sg_image_type convertTextureType(TextureType::Code t) {
    switch (t) {
        case TextureType::Texture2D:    return SG_IMAGETYPE_2D;
        case TextureType::TextureCube:  return SG_IMAGETYPE_CUBE;
        case TextureType::Texture3D:    return SG_IMAGETYPE_3D;
        case TextureType::TextureArray: return SG_IMAGETYPE_ARRAY;
        default: return _SG_IMAGETYPE_DEFAULT;
    }
}

//------------------------------------------------------------------------------
static sg_filter convertFilter(TextureFilterMode::Code f) {
    switch (f) {
        case TextureFilterMode::Nearest: return SG_FILTER_NEAREST;
        case TextureFilterMode::Linear:  return SG_FILTER_LINEAR;
        case TextureFilterMode::NearestMipmapNearest: return SG_FILTER_NEAREST_MIPMAP_NEAREST;
        case TextureFilterMode::NearestMipmapLinear:  return SG_FILTER_NEAREST_MIPMAP_LINEAR;
        case TextureFilterMode::LinearMipmapNearest:  return SG_FILTER_LINEAR_MIPMAP_NEAREST;
        case TextureFilterMode::LinearMipmapLinear:   return SG_FILTER_LINEAR_MIPMAP_LINEAR;
        default: return _SG_FILTER_DEFAULT;
    }
}

//------------------------------------------------------------------------------
static sg_wrap convertWrap(TextureWrapMode::Code w) {
    switch (w) {
        case TextureWrapMode::ClampToEdge:      return SG_WRAP_CLAMP_TO_EDGE;
        case TextureWrapMode::Repeat:           return SG_WRAP_REPEAT;
        case TextureWrapMode::MirroredRepeat:   return SG_WRAP_MIRRORED_REPEAT;
        default: return _SG_WRAP_DEFAULT;
    }
}

//------------------------------------------------------------------------------
sokolGfxBackend::~sokolGfxBackend() {
    o_assert(!this->isValid);
}

//------------------------------------------------------------------------------
void
sokolGfxBackend::Setup(const GfxSetup& setup, const gfxPointers& ptrs) {
    o_assert(!this->isValid);
    this->displayManager.SetupDisplay(setup, ptrs);

    // setup sokol-gfx
    sg_desc sgDesc = { };
    sgDesc.buffer_pool_size = setup.ResourcePoolSize[GfxResourceType::Buffer];
    sgDesc.image_pool_size = setup.ResourcePoolSize[GfxResourceType::Texture];
    sgDesc.shader_pool_size = setup.ResourcePoolSize[GfxResourceType::Shader];
    sgDesc.pipeline_pool_size = setup.ResourcePoolSize[GfxResourceType::Pipeline];
    sgDesc.pass_pool_size = setup.ResourcePoolSize[GfxResourceType::RenderPass];
    #if ORYOL_EMSCRIPTEN
    sgDesc.gl_force_gles2 = this->displayManager.force_gles2;
    #elif ORYOL_METAL
    sgDesc.mtl_device = XXX; // FIXME;
    sgDesc.mtl_renderpass_descriptor_cb = XXX; // FIXME
    sgDesc.mtl_drawable_cb = XXX; // FIXME
    #elif ORYOL_D3D11
    sgDesc.d3d11_device = XXX; // FIXME
    sgDesc.d3d11_device_context = XXX; // FIXME
    sgDesc.d3d11_render_target_view_cb = XXX; // FIXME
    sgDesc.d3d11_depth_stencil_view_cb = XXX; // FIXME
    #endif
    sg_setup(&sgDesc);

    this->registry.Setup(setup.ResourceRegistryCapacity);
    this->labelStack.Setup(setup.ResourceLabelStackCapacity);
    this->isValid = true;
}

//------------------------------------------------------------------------------
void
sokolGfxBackend::Discard() {
    o_assert(this->isValid);
    this->registry.Discard();
    this->labelStack.Discard();
    sg_shutdown();
    this->displayManager.DiscardDisplay();
    this->isValid = false;
}

//------------------------------------------------------------------------------
bool
sokolGfxBackend::IsValid() {
    return this->isValid;
}

//------------------------------------------------------------------------------
bool
sokolGfxBackend::QuitRequested() {
    return this->displayManager.QuitRequested();
}

//------------------------------------------------------------------------------
bool
sokolGfxBackend::QueryFeature(GfxFeature::Code feature) {
    o_assert_dbg(this->isValid);
    // FIXME
    o_error("sokolGfxBackend::QueryFeature FIXME!\n");
    return false;
}

//------------------------------------------------------------------------------
GfxEvent::HandlerId
sokolGfxBackend::Subscribe(GfxEvent::Handler handler) {
    o_assert_dbg(this->isValid);
    return this->displayManager.Subscribe(handler);
}

//------------------------------------------------------------------------------
void
sokolGfxBackend::Unsubscribe(GfxEvent::HandlerId id) {
    o_assert_dbg(this->isValid);
    this->displayManager.Unsubscribe(id);
}

//------------------------------------------------------------------------------
ResourceLabel
sokolGfxBackend::PushResourceLabel() {
    o_assert_dbg(this->isValid);
    return this->labelStack.PushLabel();
}

//------------------------------------------------------------------------------
void
sokolGfxBackend::PushResourceLabel(ResourceLabel label) {
    o_assert_dbg(this->isValid);
    return this->labelStack.PushLabel(label);
}

//------------------------------------------------------------------------------
ResourceLabel
sokolGfxBackend::PopResourceLabel() {
    o_assert_dbg(this->isValid);
    return this->labelStack.PopLabel();
}

//------------------------------------------------------------------------------
Id
sokolGfxBackend::CreateBuffer(const BufferDesc& desc, const void* data, int dataSize) {
    o_assert_dbg(this->isValid);
    o_assert_dbg((data == nullptr) || (desc.Size+desc.Offset) <= dataSize);
    sg_buffer_desc sgDesc = { };
    sgDesc.size = desc.Size;
    sgDesc.type = convertBufferType(desc.Type);
    sgDesc.usage = convertUsage(desc.Usage);
    sgDesc.content = (uint8_t*)data + desc.Offset;
    o_assert_dbg(GfxConfig::MaxInflightFrames <= SG_NUM_INFLIGHT_FRAMES);
    #if ORYOL_OPENGL
    for (int i = 0; i < GfxConfig::MaxInflightFrames; i++) {
        sgDesc.gl_buffers[i] = (uint32_t) desc.NativeBuffers[i];
    }
    #elif ORYOL_METAL
    for (int i = 0; i < GfxConfig::MaxInflightFrames; i++) {
        sgDesc.mtl_buffers[i] = (const void*) desc.NativeBuffers[i];
    }
    #elif ORYOL_D3D11
    sgDesc.d3d11_buffer = (const void*) desc.NativeBuffers[0]
    #endif
    return makeId(GfxResourceType::Buffer, sg_make_buffer(&sgDesc).id);
}

//------------------------------------------------------------------------------
Id
sokolGfxBackend::CreateTexture(const TextureDesc& desc, const void* data, int size) {
    o_assert_dbg(this->isValid);
    sg_image_desc sgDesc = { };
    sgDesc.type = convertTextureType(desc.Type);
    sgDesc.render_target = desc.RenderTarget;
    sgDesc.width = desc.Width;
    sgDesc.height = desc.Height;
    sgDesc.depth = desc.Depth;
    sgDesc.num_mipmaps = desc.NumMipMaps;
    sgDesc.usage = convertUsage(desc.Usage);
    sgDesc.pixel_format = convertPixelFormat(desc.Format);
    sgDesc.sample_count = desc.SampleCount;
    sgDesc.min_filter = convertFilter(desc.MinFilter);
    sgDesc.mag_filter = convertFilter(desc.MagFilter);
    sgDesc.wrap_u = convertWrap(desc.WrapU);
    sgDesc.wrap_v = convertWrap(desc.WrapV);
    sgDesc.wrap_w = convertWrap(desc.WrapW);
    o_assert_dbg(GfxConfig::MaxNumTextureMipMaps <= SG_MAX_MIPMAPS);
    o_assert_dbg(GfxConfig::MaxNumTextureFaces <= SG_CUBEFACE_NUM);
    for (int f = 0; f < GfxConfig::MaxNumTextureFaces; f++) {
        for (int m = 0; m < GfxConfig::MaxNumTextureMipMaps; m++) {
            if (desc.ImageData.Sizes[f][m] > 0) {
                sgDesc.content.subimage[f][m].ptr = (uint8_t*)data + desc.ImageData.Offsets[f][m];
                sgDesc.content.subimage[f][m].size = desc.ImageData.Sizes[f][m];
            }
        }
    }
    o_assert_dbg(GfxConfig::MaxInflightFrames <= SG_NUM_INFLIGHT_FRAMES);
    #if ORYOL_OPENGL
    for (int i = 0; i < GfxConfig::MaxInflightFrames; i++) {
        sgDesc.gl_textures[i] = (uint32_t) desc.NativeTextures[i];
    }
    #elif ORYOL_METAL
    for (int i = 0; i < GfxConfig::MaxInflightFrames; i++) {
        sgDesc.mtl_buffers[i] = (const void*) desc.NativeTextures[i];
    }
    #elif ORYOL_D3D11
    sgDesc.d3d11_buffer = (const void*) desc.NativeTextures[0]
    #endif
    return makeId(GfxResourceType::Texture, sg_make_image(&sgDesc).id);
}

//------------------------------------------------------------------------------
Id
sokolGfxBackend::CreateShader(const ShaderDesc& desc) {
    o_assert_dbg(this->isValid);
    sg_shader_desc sgDesc = { };

    // select the shader language dialect
    ShaderLang::Code slang = ShaderLang::Invalid;
    #if ORYOL_OPENGL_CORE_PROFILE
    slang = ShaderLang::GLSL330;
    #elif ORYOL_OPENGL_GLES2
    slang = ShaderLang::GLSL100;
    #elif ORYOL_OPENGL_GLES3
    if (this->displayManager.force_gles2) {
        slang = ShaderLang::GLSL100;
    }
    else {
        slang = ShaderLang::GLSLES3;
    }
    #elif ORYOL_METAL
    slang = ShaderLang::Metal;
    #elif ORYOL_D3D11
    slang = ShaderLang::HLSL5;
    #else
    #error("Unknown Platform")
    #endif

    // set source- or byte-code, and optional entry function
    #if ORYOL_OPENGL
    sgDesc.vs.source = desc.VertexShaderSource(slang).AsCStr();
    sgDesc.fs.source = desc.FragmentShaderSource(slang).AsCStr();
    #elif ORYOL_METAL || ORYOL_D3D11
    const void* byteCodePtr; uint32_t byteCodeSize;
    desc.VertexShaderByteCode(slang, byteCodePtr, byteCodeSize);
    sgDesc.vs.byte_code = byteCodePtr;
    sgDesc.vs.byte_code_size = byteCodeSize;
    desc.FragmentShaderByteCode(slang, byteCodePtr, byteCodeSize);
    sgDesc.fs.byte_code = byteCodePtr;
    sgDesc.fs.byte_code_size = byteCodeSize;
    #endif
    if (desc.VertexShaderFunc(slang).IsValid()) {
        sgDesc.vs.entry = desc.VertexShaderFunc(slang).AsCStr();
    }
    if (desc.FragmentShaderFunc(slang).IsValid()) {
        sgDesc.fs.entry = desc.FragmentShaderFunc(slang).AsCStr();
    }

    // uniform block declarations
    int vsUbIndex = 0, fsUbIndex = 0;
    for (int i = 0; i < desc.NumUniformBlocks(); i++) {
        sg_shader_uniform_block_desc* ubDesc;
        if (desc.UniformBlockBindStage(i) == ShaderStage::VS) {
            o_assert_dbg(vsUbIndex < SG_MAX_SHADERSTAGE_UBS);
            ubDesc = &sgDesc.vs.uniform_blocks[vsUbIndex++];
        }
        else {
            o_assert_dbg(vsUbIndex < SG_MAX_SHADERSTAGE_UBS);
            ubDesc = &sgDesc.fs.uniform_blocks[fsUbIndex++];
        }
        ubDesc->size = desc.UniformBlockByteSize(i);
        // size must be a multiple of 16 (sizeof(vec4))
        o_assert_dbg((ubDesc->size & 15) == 0);
        ubDesc->uniforms[0].name = desc.UniformBlockType(i).AsCStr();
        ubDesc->uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
        ubDesc->uniforms[0].array_count = ubDesc->size / 16;
    }

    // texture declarations
    int vsImgIndex = 0, fsImgIndex = 0;
    for (int i = 0; i < desc.NumTextures(); i++) {
        sg_shader_image_desc* imgDesc;
        if (desc.TexBindStage(i) == ShaderStage::VS) {
            o_assert_dbg(vsImgIndex < SG_MAX_SHADERSTAGE_IMAGES);
            imgDesc = &sgDesc.vs.images[vsImgIndex++];
        }
        else {
            o_assert_dbg(fsImgIndex < SG_MAX_SHADERSTAGE_IMAGES);
            imgDesc = &sgDesc.fs.images[fsImgIndex++];
        }
        imgDesc->type = convertTextureType(desc.TexType(i));
        imgDesc->name = desc.TexName(i).AsCStr();
    }

    return makeId(GfxResourceType::Shader, sg_make_shader(&sgDesc).id);
}

//------------------------------------------------------------------------------
Id
sokolGfxBackend::CreatePipeline(const PipelineDesc& desc) {
    o_assert_dbg(this->isValid);
    sg_pipeline_desc sgDesc = { };
    sgDesc.shader = makeShaderId(desc.Shader);
    sgDesc.primitive_type = convertPrimitiveType(desc.PrimType);
    sgDesc.index_type = convertIndexType(desc.IndexType);
    convertVertexLayouts(desc, sgDesc);
    convertDepthStencilState(desc, sgDesc);
    convertBlendState(desc, sgDesc);
    convertRasterizerState(desc, sgDesc);
    return makeId(GfxResourceType::Pipeline, sg_make_pipeline(&sgDesc).id);
}

//------------------------------------------------------------------------------
Id
sokolGfxBackend::CreatePass(const PassDesc& desc) {
    o_assert_dbg(this->isValid);
    // FIXME
    return Id::InvalidId();
}

//------------------------------------------------------------------------------
Id
sokolGfxBackend::LookupResource(const Locator& loc) {
    o_assert_dbg(this->isValid);
    return this->registry.Lookup(loc);
}

//------------------------------------------------------------------------------
void
sokolGfxBackend::AddResource(const Locator& loc, const Id& id) {
    o_assert_dbg(this->isValid);
    this->registry.Add(loc, id, this->labelStack.PeekLabel());
}

//------------------------------------------------------------------------------
void
sokolGfxBackend::DestroyResources(ResourceLabel label) {
    o_assert_dbg(this->isValid);
    // FIXME
}

//------------------------------------------------------------------------------
void
sokolGfxBackend::UpdateBuffer(const Id& id, const void* data, int numBytes) {
    o_assert_dbg(this->isValid);
    sg_update_buffer(makeBufferId(id), data, numBytes);
}

//------------------------------------------------------------------------------
void
sokolGfxBackend::UpdateTexture(const Id& id, const void* data, const ImageDataAttrs& attrs) {
    o_assert_dbg(this->isValid);
    // FIXME
}

//------------------------------------------------------------------------------
void
sokolGfxBackend::BeginPass(Id passId, const PassAction* action) {
    o_assert_dbg(this->isValid);
    o_assert_dbg(action);
    if (passId.IsValid()) {
        // offscreen framebuffer
    }
    else {
        // default framebuffer
        sg_pass_action sgAction = { };
        convertPassAction(*action, sgAction);
        const DisplayAttrs& attrs = this->displayManager.GetDisplayAttrs();
        sg_begin_default_pass(&sgAction, attrs.FramebufferWidth, attrs.FramebufferHeight);
    }
}

//------------------------------------------------------------------------------
void
sokolGfxBackend::EndPass() {
    o_assert_dbg(this->isValid);
    sg_end_pass();
}

//------------------------------------------------------------------------------
void
sokolGfxBackend::ApplyViewPort(int x, int y, int w, int h, bool originTopLeft) {
    o_assert_dbg(this->isValid);
    sg_apply_viewport(x, y, w, h, originTopLeft);
}

//------------------------------------------------------------------------------
void
sokolGfxBackend::ApplyScissorRect(int x, int y, int w, int h, bool originTopLeft) {
    o_assert_dbg(this->isValid);
    sg_apply_scissor_rect(x, y, w, h, originTopLeft);
}

//------------------------------------------------------------------------------
void sokolGfxBackend::ApplyDrawState(const DrawState& drawState) {
    o_assert_dbg(this->isValid);
    o_assert_dbg(SG_MAX_SHADERSTAGE_BUFFERS >= GfxConfig::MaxNumVertexBuffers);
    o_assert_dbg(SG_MAX_SHADERSTAGE_IMAGES >= GfxConfig::MaxNumVertexTextures);
    o_assert_dbg(SG_MAX_SHADERSTAGE_IMAGES >= GfxConfig::MaxNumFragmentTextures);
    sg_draw_state sgDrawState = { };
    sgDrawState.pipeline = makePipelineId(drawState.Pipeline);
    for (int i = 0; i < GfxConfig::MaxNumVertexBuffers; i++) {
        if (drawState.VertexBuffers[i].IsValid()) {
            sgDrawState.vertex_buffers[i] = makeBufferId(drawState.VertexBuffers[i]);
        }
        else {
            break;
        }
    }
    if (drawState.IndexBuffer.IsValid()) {
        sgDrawState.index_buffer = makeBufferId(drawState.IndexBuffer);
    }
    for (int i = 0; i < GfxConfig::MaxNumVertexTextures; i++) {
        if (drawState.VSTexture[i].IsValid()) {
            sgDrawState.vs_images[i] = makeImageId(drawState.VSTexture[i]);
        }
        else {
            break;
        }
    }
    for (int i = 0; i < GfxConfig::MaxNumFragmentTextures; i++) {
        if (drawState.FSTexture[i].IsValid()) {
            sgDrawState.fs_images[i] = makeImageId(drawState.FSTexture[i]);
        }
        else {
            break;
        }
    }
    sg_apply_draw_state(&sgDrawState);
}

//------------------------------------------------------------------------------
void
sokolGfxBackend::ApplyUniformBlock(ShaderStage::Code stage, int ubIndex, const void* data, int numBytes) {
    o_assert_dbg(this->isValid);
    sg_shader_stage sgStage = (stage==ShaderStage::VS) ? SG_SHADERSTAGE_VS : SG_SHADERSTAGE_FS;
    sg_apply_uniform_block(sgStage, ubIndex, data, numBytes);
}

//------------------------------------------------------------------------------
void
sokolGfxBackend::Draw(int baseElement, int numElements, int numInstances) {
    o_assert_dbg(this->isValid);
    sg_draw(baseElement, numElements, numInstances);
}

//------------------------------------------------------------------------------
void
sokolGfxBackend::CommitFrame() {
    o_assert_dbg(this->isValid);
    sg_commit();
    this->displayManager.Present();
}

//------------------------------------------------------------------------------
void
sokolGfxBackend::ResetStateCache() {
    o_assert_dbg(this->isValid);
    sg_reset_state_cache();
}

//------------------------------------------------------------------------------
void
sokolGfxBackend::ProcessSystemEvents() {
    o_assert_dbg(this->isValid);
    this->displayManager.ProcessSystemEvents();
}

} // namespace _priv
} // namespace Oryol