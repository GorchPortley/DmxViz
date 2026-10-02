#pragma once
// Turns the embedded GLSL files (shaders/*.glsl) into sokol shader objects.
//
// Each .glsl file holds both stages of one program, separated by
// `#ifdef VERTEX_SHADER` / `#ifdef FRAGMENT_SHADER`; `#include "file.glsl"`
// pulls in shared code. ShaderProgram describes the program's resource
// interface (uniforms, textures, storage buffers) the way sokol needs it for
// the OpenGL backend, where everything is bound by GLSL name or binding index.

#include "sokol_gfx.h"

#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

namespace dmxviz::render {

// Full source of `file` for one stage: version line, stage define and all
// includes resolved (with #line directives so GL error lines stay meaningful).
std::string shaderSource(const char* file, sg_shader_stage stage);

struct UniformMember {
    const char* glslName;
    sg_uniform_type type;
    int arrayCount = 0;
};

class ShaderProgram {
public:
    explicit ShaderProgram(const char* file) : file_(file) {}

    // One uniform block (sg_apply_uniforms(slot, ...)). Members must be FLOAT4 or
    // MAT4 so the C++ struct and the std140 layout agree; `size` is checked.
    ShaderProgram& uniforms(int slot, std::size_t size, std::initializer_list<UniformMember> members);
    // Read-only storage buffer bound at sg_bindings.views[viewSlot], GLSL `layout(binding = glslBinding)`.
    ShaderProgram& storageBuffer(int viewSlot, int glslBinding);
    // A texture at sg_bindings.views[viewSlot] sampled through sg_bindings.samplers[samplerSlot];
    // GLSL declares it as a combined sampler named glslName.
    ShaderProgram& texture(int viewSlot, int samplerSlot, const char* glslName,
                           sg_image_type type = SG_IMAGETYPE_2D,
                           sg_image_sample_type sampleType = SG_IMAGESAMPLETYPE_FLOAT,
                           sg_sampler_type samplerType = SG_SAMPLERTYPE_FILTERING);
    // Shorthand for depth/distance (R32F) textures read with texelFetch.
    ShaderProgram& unfilterableTexture(int viewSlot, int samplerSlot, const char* glslName) {
        return texture(viewSlot, samplerSlot, glslName, SG_IMAGETYPE_2D, SG_IMAGESAMPLETYPE_UNFILTERABLE_FLOAT,
                       SG_SAMPLERTYPE_NONFILTERING);
    }
    // The most recently added texture is read in the vertex shader instead of the fragment shader.
    ShaderProgram& inVertexStage();
    ShaderProgram& attribute(int index, const char* glslName);

    // Compiles and links; logs and returns an invalid/failed handle on error.
    sg_shader build() const;

private:
    struct Block {
        int slot;
        std::size_t size;
        std::vector<UniformMember> members;
    };
    struct Texture {
        int viewSlot, samplerSlot;
        const char* name;
        sg_image_type type;
        sg_image_sample_type sampleType;
        sg_sampler_type samplerType;
        sg_shader_stage stage;
    };
    struct Storage {
        int viewSlot, binding;
    };
    struct Attr {
        int index;
        const char* name;
    };

    const char* file_;
    std::vector<Block> blocks_;
    std::vector<Texture> textures_;
    std::vector<Storage> storage_;
    std::vector<Attr> attrs_;
};

}  // namespace dmxviz::render
