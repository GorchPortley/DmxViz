#include "render/ShaderProgram.h"

#include "core/Log.h"
#include "render/EmbeddedShaders.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string_view>

namespace dmxviz::render {
namespace {

int findShaderFile(std::string_view name) {
    for (std::size_t i = 0; i < std::size(embedded::kShaderFiles); ++i) {
        if (name == embedded::kShaderFiles[i].name) return static_cast<int>(i);
    }
    return -1;
}

std::string_view trimLeft(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    return s;
}

// Copies `fileIndex` into `out`, replacing `#include "x"` lines with the
// contents of x (each file at most once). `#line <n> <file>` directives keep
// the line numbers in GL compiler messages pointing at the original files;
// the file number is the index printed by logShaderFileIndex().
void appendResolved(std::string& out, int fileIndex, std::vector<int>& included) {
    included.push_back(fileIndex);
    std::string_view src = embedded::kShaderFiles[fileIndex].source;
    out += "#line 1 " + std::to_string(fileIndex) + "\n";
    int lineNo = 0;
    while (!src.empty()) {
        const std::size_t end = src.find('\n');
        const std::string_view line = src.substr(0, end);
        src = end == std::string_view::npos ? std::string_view{} : src.substr(end + 1);
        ++lineNo;

        const std::string_view t = trimLeft(line);
        if (t.starts_with("#version")) {
            out += '\n';  // the version line is emitted once, at the very top
        } else if (t.starts_with("#include")) {
            const std::size_t q0 = t.find('"');
            const std::size_t q1 = q0 == std::string_view::npos ? q0 : t.find('"', q0 + 1);
            const int inc = q1 == std::string_view::npos ? -1 : findShaderFile(t.substr(q0 + 1, q1 - q0 - 1));
            if (inc < 0) {
                log::error("render", "{}:{}: cannot resolve {}", embedded::kShaderFiles[fileIndex].name, lineNo, t);
                out += '\n';
            } else if (std::find(included.begin(), included.end(), inc) != included.end()) {
                out += '\n';
            } else {
                appendResolved(out, inc, included);
                out += "#line " + std::to_string(lineNo + 1) + " " + std::to_string(fileIndex) + "\n";
            }
        } else {
            out.append(line);
            out += '\n';
        }
    }
}

}  // namespace

std::string shaderSource(const char* file, sg_shader_stage stage) {
    const int index = findShaderFile(file);
    if (index < 0) {
        log::error("render", "shader file {} is not embedded", file);
        return {};
    }
    std::string out = "#version 430 core\n";
    out += stage == SG_SHADERSTAGE_VERTEX ? "#define VERTEX_SHADER 1\n" : "#define FRAGMENT_SHADER 1\n";
    std::vector<int> included;
    appendResolved(out, index, included);
    return out;
}

ShaderProgram& ShaderProgram::uniforms(int slot, std::size_t size, std::initializer_list<UniformMember> members) {
    blocks_.push_back({slot, size, std::vector<UniformMember>(members)});
    return *this;
}

ShaderProgram& ShaderProgram::storageBuffer(int viewSlot, int glslBinding) {
    storage_.push_back({viewSlot, glslBinding});
    return *this;
}

ShaderProgram& ShaderProgram::texture(int viewSlot, int samplerSlot, const char* glslName, sg_image_type type,
                                      sg_image_sample_type sampleType, sg_sampler_type samplerType) {
    textures_.push_back({viewSlot, samplerSlot, glslName, type, sampleType, samplerType, SG_SHADERSTAGE_FRAGMENT});
    return *this;
}

ShaderProgram& ShaderProgram::inVertexStage() {
    if (!textures_.empty()) textures_.back().stage = SG_SHADERSTAGE_VERTEX;
    return *this;
}

ShaderProgram& ShaderProgram::attribute(int index, const char* glslName) {
    attrs_.push_back({index, glslName});
    return *this;
}

sg_shader ShaderProgram::build() const {
    const std::string vs = shaderSource(file_, SG_SHADERSTAGE_VERTEX);
    const std::string fs = shaderSource(file_, SG_SHADERSTAGE_FRAGMENT);

    sg_shader_desc d{};
    d.label = file_;
    d.vertex_func.source = vs.c_str();
    d.fragment_func.source = fs.c_str();

    for (const Attr& a : attrs_) d.attrs[a.index].glsl_name = a.name;

    for (const Block& b : blocks_) {
        sg_shader_uniform_block& ub = d.uniform_blocks[b.slot];
        // GL uniforms are program-wide; the stage only matters for other backends.
        ub.stage = SG_SHADERSTAGE_FRAGMENT;
        ub.size = static_cast<std::uint32_t>(b.size);
        ub.layout = SG_UNIFORMLAYOUT_STD140;
        int i = 0;
        for (const UniformMember& m : b.members) {
            ub.glsl_uniforms[i].glsl_name = m.glslName;
            ub.glsl_uniforms[i].type = m.type;
            ub.glsl_uniforms[i].array_count = static_cast<std::uint16_t>(m.arrayCount > 0 ? m.arrayCount : 1);
            ++i;
        }
    }

    for (const Storage& s : storage_) {
        sg_shader_storage_buffer_view& v = d.views[s.viewSlot].storage_buffer;
        v.stage = SG_SHADERSTAGE_FRAGMENT;  // GL binds storage buffers per program as well
        v.readonly = true;
        v.glsl_binding_n = static_cast<std::uint8_t>(s.binding);
    }

    int pair = 0;
    for (const Texture& t : textures_) {
        sg_shader_texture_view& v = d.views[t.viewSlot].texture;
        v.stage = t.stage;
        v.image_type = t.type;
        v.sample_type = t.sampleType;
        sg_shader_sampler& smp = d.samplers[t.samplerSlot];
        smp.stage = t.stage;
        smp.sampler_type = t.samplerType;
        sg_shader_texture_sampler_pair& p = d.texture_sampler_pairs[pair++];
        p.stage = t.stage;
        p.view_slot = static_cast<std::uint8_t>(t.viewSlot);
        p.sampler_slot = static_cast<std::uint8_t>(t.samplerSlot);
        p.glsl_name = t.name;
    }

    const sg_shader shd = sg_make_shader(&d);
    if (sg_query_shader_state(shd) != SG_RESOURCESTATE_VALID) {
        std::string files;
        for (std::size_t i = 0; i < std::size(embedded::kShaderFiles); ++i)
            files += std::to_string(i) + "=" + embedded::kShaderFiles[i].name + " ";
        log::error("render", "failed to build shader {} (GL file numbers: {})", file_, files);
    }
    return shd;
}

}  // namespace dmxviz::render
