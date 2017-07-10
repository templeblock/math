/*
Copyright (C) 2017 Topological Manifold

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "com/error.h"
#include "gl_func/gl_functions.h"

#include <SFML/Graphics/Image.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <type_traits>
#include <vector>

constexpr int MAJOR_GL_VERSION = 4;
constexpr int MINOR_GL_VERSION = 5;
constexpr int ANTIALIASING_LEVEL = 4;
constexpr int DEPTH_BITS = 24;
constexpr int STENCIL_BITS = 8;
constexpr int RED_BITS = 8;
constexpr int GREEN_BITS = 8;
constexpr int BLUE_BITS = 8;
constexpr int ALPHA_BITS = 8;
inline std::vector<std::string> required_extensions()
{
        return std::vector<std::string>({"GL_ARB_bindless_texture", "GL_ARB_compute_variable_group_size"});
}

class Shader
{
        GLuint m_shader = 0;

protected:
        Shader(GLenum type, const std::string& shader_text)
        {
                m_shader = glCreateShader(type);
                try
                {
                        std::string source;
                        source = "#version " + std::to_string(MAJOR_GL_VERSION) + std::to_string(MINOR_GL_VERSION) +
                                 (MINOR_GL_VERSION < 10 ? "0" : "") + " core\n";
                        for (const std::string& ext : required_extensions())
                        {
                                source += "#extension " + ext + " : require\n";
                        }
                        source += "\n";
                        source += shader_text;
                        const char* const source_ptr = source.c_str();

                        glShaderSource(m_shader, 1, &source_ptr, nullptr);
                        glCompileShader(m_shader);

                        GLint status;
                        glGetShaderiv(m_shader, GL_COMPILE_STATUS, &status);
                        if (status != GL_TRUE)
                        {
                                GLint length;
                                glGetShaderiv(m_shader, GL_INFO_LOG_LENGTH, &length);
                                if (length > 1)
                                {
                                        std::vector<GLchar> buffer(length);
                                        glGetShaderInfoLog(m_shader, length, nullptr, buffer.data());
                                        error_source(std::string("CompileShader\n\n") + buffer.data(), source);
                                }
                                else
                                {
                                        error_source("CompileShader\n\nUnknown error", source);
                                }
                        }
                }
                catch (...)
                {
                        glDeleteShader(m_shader);
                        throw;
                }
        }

        ~Shader()
        {
                glDeleteShader(m_shader);
        }

        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;

        Shader(Shader&& from) noexcept
        {
                *this = std::move(from);
        }
        Shader& operator=(Shader&& from) noexcept
        {
                if (this == &from)
                {
                        return *this;
                }
                glDeleteShader(m_shader);
                m_shader = from.m_shader;
                from.m_shader = 0;
                return *this;
        }

public:
        void attach_to_program(GLuint program) const noexcept
        {
                glAttachShader(program, m_shader);
        }
        void detach_from_program(GLuint program) const noexcept
        {
                glDetachShader(program, m_shader);
        }
};

class Program
{
        class AttachShader final
        {
                GLuint m_program;
                const Shader* m_shader;

        public:
                AttachShader(GLuint program, const Shader& shader) : m_program(program), m_shader(&shader)
                {
                        m_shader->attach_to_program(m_program);
                }
                ~AttachShader()
                {
                        if (m_shader && m_program)
                        {
                                m_shader->detach_from_program(m_program);
                        }
                }

                AttachShader(const AttachShader&) = delete;
                AttachShader& operator=(const AttachShader&) = delete;

                AttachShader(AttachShader&& from) noexcept
                {
                        *this = std::move(from);
                }
                AttachShader& operator=(AttachShader&& from) noexcept
                {
                        if (this == &from)
                        {
                                return *this;
                        }
                        m_program = from.m_program;
                        m_shader = from.m_shader;
                        from.m_program = 0;
                        from.m_shader = nullptr;
                        return *this;
                }
        };

        //
        GLuint m_program = 0;

        GLint get_uniform_location(const char* name) const
        {
                GLint loc = glGetUniformLocation(m_program, name);
                if (loc < 0)
                {
                        error(std::string("glGetUniformLocation error: ") + name);
                }
                return loc;
        }

protected:
        Program() = delete; // Программа без шейдеров не должна делаться

        template <typename... S>
        Program(const S&... shader)
        {
                m_program = glCreateProgram();
                try
                {
                        std::vector<AttachShader> attaches;

                        (attaches.emplace_back(m_program, shader), ...);

                        glLinkProgram(m_program);

                        GLint status;
                        glGetProgramiv(m_program, GL_LINK_STATUS, &status);
                        if (status != GL_TRUE)
                        {
                                GLint length;
                                glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &length);
                                if (length > 1)
                                {
                                        std::vector<GLchar> buffer(length);
                                        glGetProgramInfoLog(m_program, length, nullptr, buffer.data());
                                        error(std::string("LinkProgram Error: ") + buffer.data());
                                }
                                else
                                {
                                        error("LinkProgram Error");
                                }
                        }
                }
                catch (...)
                {
                        glDeleteProgram(m_program);
                        throw;
                }
        }

        Program(const Program&) = delete;
        Program& operator=(const Program&) = delete;
        Program(Program&& from) noexcept
        {
                *this = std::move(from);
        }
        Program& operator=(Program&& from) noexcept
        {
                if (this == &from)
                {
                        return *this;
                }
                glDeleteProgram(m_program);
                m_program = from.m_program;
                from.m_program = 0;
                return *this;
        }

        ~Program()
        {
                glDeleteProgram(m_program);
        }

        void use() const noexcept
        {
                glUseProgram(m_program);
        }

public:
        void set_uniform(const char* var_name, const glm::vec2& var) const
        {
                glProgramUniform2fv(m_program, get_uniform_location(var_name), 1, glm::value_ptr(var));
        }
        void set_uniform(const char* var_name, const glm::vec3& var) const
        {
                glProgramUniform3fv(m_program, get_uniform_location(var_name), 1, glm::value_ptr(var));
        }
        void set_uniform(const char* var_name, const glm::vec4& var) const
        {
                glProgramUniform4fv(m_program, get_uniform_location(var_name), 1, glm::value_ptr(var));
        }

        void set_uniform(const char* var_name, int var) const
        {
                glProgramUniform1i(m_program, get_uniform_location(var_name), var);
        }
        void set_uniform_unsigned(const char* var_name, unsigned var) const
        {
                glProgramUniform1ui(m_program, get_uniform_location(var_name), var);
        }
        void set_uniform(const char* var_name, float var) const
        {
                glProgramUniform1f(m_program, get_uniform_location(var_name), var);
        }
        void set_uniform(const char* var_name, double var) const
        {
                glProgramUniform1d(m_program, get_uniform_location(var_name), var);
        }

        void set_uniform(GLint loc, int var) const
        {
                glProgramUniform1i(m_program, loc, var);
        }
        void set_uniform_unsigned(GLint loc, unsigned var) const
        {
                glProgramUniform1ui(m_program, loc, var);
        }
        void set_uniform(GLint loc, float var) const
        {
                glProgramUniform1f(m_program, loc, var);
        }
        void set_uniform(GLint loc, double var) const
        {
                glProgramUniform1d(m_program, loc, var);
        }
        void set_uniform_handle(GLint loc, GLuint64 var) const
        {
                glProgramUniformHandleui64ARB(m_program, loc, var);
        }
        void set_uniform_handles(GLint loc, const std::vector<GLuint64>& var) const
        {
                glProgramUniformHandleui64vARB(m_program, loc, var.size(), var.data());
        }

        void set_uniform(const char* var_name, const glm::mat2x2& var) const
        {
                glProgramUniformMatrix2fv(m_program, get_uniform_location(var_name), 1, GL_FALSE, glm::value_ptr(var));
        }
        void set_uniform(const char* var_name, const glm::mat3x3& var) const
        {
                glProgramUniformMatrix3fv(m_program, get_uniform_location(var_name), 1, GL_FALSE, glm::value_ptr(var));
        }
        void set_uniform(const char* var_name, const glm::mat4x4& var) const
        {
                glProgramUniformMatrix4fv(m_program, get_uniform_location(var_name), 1, GL_FALSE, glm::value_ptr(var));
        }

        void set_uniform(const char* var_name, const std::vector<int>& var) const
        {
                glProgramUniform1iv(m_program, get_uniform_location(var_name), var.size(), var.data());
        }
        void set_uniform(const char* var_name, const std::vector<unsigned>& var) const
        {
                glProgramUniform1uiv(m_program, get_uniform_location(var_name), var.size(), var.data());
        }

        void set_uniform_handle(const char* var_name, GLuint64 var) const
        {
                glProgramUniformHandleui64ARB(m_program, get_uniform_location(var_name), var);
        }
        void set_uniform_handles(const char* var_name, const std::vector<GLuint64>& var) const
        {
                glProgramUniformHandleui64vARB(m_program, get_uniform_location(var_name), var.size(), var.data());
        }
};

class VertexShader final : public Shader
{
public:
        VertexShader(const std::string& shader_text) : Shader(GL_VERTEX_SHADER, shader_text)
        {
        }
};

class TessControlShader final : public Shader
{
public:
        TessControlShader(const std::string& shader_text) : Shader(GL_TESS_CONTROL_SHADER, shader_text)
        {
        }
};

class TessEvaluationShader final : public Shader
{
public:
        TessEvaluationShader(const std::string& shader_text) : Shader(GL_TESS_EVALUATION_SHADER, shader_text)
        {
        }
};

class GeometryShader final : public Shader
{
public:
        GeometryShader(const std::string& shader_text) : Shader(GL_GEOMETRY_SHADER, shader_text)
        {
        }
};

class FragmentShader final : public Shader
{
public:
        FragmentShader(const std::string& shader_text) : Shader(GL_FRAGMENT_SHADER, shader_text)
        {
        }
};

class ComputeShader final : public Shader
{
public:
        ComputeShader(const std::string& shader_text) : Shader(GL_COMPUTE_SHADER, shader_text)
        {
        }
};

class GraphicsProgram final : public Program
{
public:
        template <typename... S>
        GraphicsProgram(const S&... s) : Program(s...)
        {
                static_assert(((std::is_same<VertexShader, S>::value || std::is_same<TessControlShader, S>::value ||
                                std::is_same<TessEvaluationShader, S>::value || std::is_same<GeometryShader, S>::value ||
                                std::is_same<FragmentShader, S>::value) &&
                               ...),
                              "GraphicsProgram accepts only vertex, tesselation, geometry and fragment shaders");
        }

        void draw_arrays(GLenum mode, GLint first, GLsizei count) const noexcept
        {
                Program::use();
                glDrawArrays(mode, first, count);
        }
};

class ComputeProgram final : public Program
{
public:
        template <typename... S>
        ComputeProgram(const S&... s) : Program(s...)
        {
                static_assert((std::is_same<ComputeShader, S>::value && ...), "ComputeProgram accepts only compute shaders");
        }

        void dispatch_compute(unsigned num_groups_x, unsigned num_groups_y, unsigned num_groups_z, unsigned group_size_x,
                              unsigned group_size_y, unsigned group_size_z) const noexcept
        {
                Program::use();
                glDispatchComputeGroupSizeARB(num_groups_x, num_groups_y, num_groups_z, group_size_x, group_size_y, group_size_z);
        }
};

class Texture2D final
{
        class Texture2DHandle final
        {
                GLuint m_texture = 0;

        public:
                Texture2DHandle() noexcept
                {
                        glCreateTextures(GL_TEXTURE_2D, 1, &m_texture);
                        glBindTexture(GL_TEXTURE_2D, m_texture);
                        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                        glBindTexture(GL_TEXTURE_2D, 0);
                }
                ~Texture2DHandle()
                {
                        glDeleteTextures(1, &m_texture);
                }
                Texture2DHandle(const Texture2DHandle&) = delete;
                Texture2DHandle& operator=(const Texture2DHandle&) = delete;
                Texture2DHandle(Texture2DHandle&& from) noexcept
                {
                        *this = std::move(from);
                }
                Texture2DHandle& operator=(Texture2DHandle&& from) noexcept
                {
                        if (this == &from)
                        {
                                return *this;
                        }
                        glDeleteTextures(1, &m_texture);
                        m_texture = from.m_texture;
                        from.m_texture = 0;
                        return *this;
                }
                operator GLuint() const noexcept
                {
                        return m_texture;
                }
        };

        Texture2DHandle m_texture;
        int m_width = 0, m_height = 0;

public:
        void texture_storage_2d(GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height) noexcept
        {
                glTextureStorage2D(m_texture, levels, internalformat, width, height);
                m_width = width;
                m_height = height;
        }
        void texture_sub_image_2d(GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format,
                                  GLenum type, const void* pixels) const noexcept
        {
                glTextureSubImage2D(m_texture, level, xoffset, yoffset, width, height, format, type, pixels);
        }

        void copy_texture_sub_image_2d(GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width,
                                       GLsizei height) const noexcept
        {
                glCopyTextureSubImage2D(m_texture, level, xoffset, yoffset, x, y, width, height);
        }

        void texture_parameter(GLenum pname, GLint param) const noexcept
        {
                glTextureParameteri(m_texture, pname, param);
        }
        void texture_parameter(GLenum pname, GLfloat param) const noexcept
        {
                glTextureParameterf(m_texture, pname, param);
        }

        GLuint64 get_texture_resident_handle() const noexcept
        {
                GLuint64 texture_handle = glGetTextureHandleARB(m_texture);
                glMakeTextureHandleResidentARB(texture_handle);
                return texture_handle;
        }
        GLuint64 get_image_resident_handle(GLint level, GLboolean layered, GLint layer, GLenum format, GLenum access) const
                noexcept
        {
                GLuint64 image_handle = glGetImageHandleARB(m_texture, level, layered, layer, format);
                glMakeImageHandleResidentARB(image_handle, access);
                return image_handle;
        }

        void bind_image_texture_read_only_RGBA32F(GLuint unit) const noexcept
        {
                glBindImageTexture(unit, m_texture, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
        }
        void bind_image_texture_write_only_RGBA32F(GLuint unit) const noexcept
        {
                glBindImageTexture(unit, m_texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        }
        void bind_image_texture_read_write_RGBA32F(GLuint unit) const noexcept
        {
                glBindImageTexture(unit, m_texture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
        }

        GLuint64 get_image_resident_handle_read_only_RGBA32F() const noexcept
        {
                return get_image_resident_handle(0, GL_FALSE, 0, GL_RGBA32F, GL_READ_ONLY);
        }
        GLuint64 get_image_resident_handle_write_only_RGBA32F() const noexcept
        {
                return get_image_resident_handle(0, GL_FALSE, 0, GL_RGBA32F, GL_WRITE_ONLY);
        }
        GLuint64 get_image_resident_handle_read_write_RGBA32F() const noexcept
        {
                return get_image_resident_handle(0, GL_FALSE, 0, GL_RGBA32F, GL_READ_WRITE);
        }

        GLuint64 get_image_resident_handle_read_only_R32F() const noexcept
        {
                return get_image_resident_handle(0, GL_FALSE, 0, GL_R32F, GL_READ_ONLY);
        }
        GLuint64 get_image_resident_handle_write_only_R32F() const noexcept
        {
                return get_image_resident_handle(0, GL_FALSE, 0, GL_R32F, GL_WRITE_ONLY);
        }
        GLuint64 get_image_resident_handle_read_write_R32F() const noexcept
        {
                return get_image_resident_handle(0, GL_FALSE, 0, GL_R32F, GL_READ_WRITE);
        }

        void clear_tex_image(GLint level, GLenum format, GLenum type, const void* data) const noexcept
        {
                glClearTexImage(m_texture, level, format, type, data);
        }
        void get_texture_image(GLint level, GLenum format, GLenum type, GLsizei bufSize, void* pixels) const noexcept
        {
                glGetTextureImage(m_texture, level, format, type, bufSize, pixels);
        }
        void get_texture_sub_image(GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height,
                                   GLsizei depth, GLenum format, GLenum type, GLsizei bufSize, void* pixels) const noexcept
        {
                glGetTextureSubImage(m_texture, level, xoffset, yoffset, zoffset, width, height, depth, format, type, bufSize,
                                     pixels);
        }

        void named_framebuffer_texture(GLuint framebuffer, GLenum attachment, GLint level) const noexcept
        {
                glNamedFramebufferTexture(framebuffer, attachment, m_texture, level);
        }

        int get_width() const noexcept
        {
                return m_width;
        }
        int get_height() const noexcept
        {
                return m_height;
        }
};

class FrameBuffer final
{
        GLuint m_framebuffer = 0;

public:
        FrameBuffer() noexcept
        {
                glCreateFramebuffers(1, &m_framebuffer);
                glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        ~FrameBuffer()
        {
                glDeleteFramebuffers(1, &m_framebuffer);
        }
        FrameBuffer(const FrameBuffer&) = delete;
        FrameBuffer& operator=(const FrameBuffer&) = delete;
        FrameBuffer(FrameBuffer&& from) noexcept
        {
                *this = std::move(from);
        }
        FrameBuffer& operator=(FrameBuffer&& from) noexcept
        {
                if (this == &from)
                {
                        return *this;
                }
                glDeleteFramebuffers(1, &m_framebuffer);
                m_framebuffer = from.m_framebuffer;
                from.m_framebuffer = 0;
                return *this;
        }

        GLenum check_named_framebuffer_status() const noexcept
        {
                return glCheckNamedFramebufferStatus(m_framebuffer, GL_FRAMEBUFFER);
        }

        void bind_framebuffer() const noexcept
        {
                glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
        }
        void unbind_framebuffer() const noexcept
        {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        void named_framebuffer_draw_buffer(GLenum buf) const noexcept
        {
                glNamedFramebufferDrawBuffer(m_framebuffer, buf);
        }
        void named_framebuffer_draw_buffers(GLsizei n, const GLenum* bufs) const noexcept
        {
                glNamedFramebufferDrawBuffers(m_framebuffer, n, bufs);
        }

        void named_framebuffer_texture(GLenum attachment, const Texture2D& texture, GLint level) const noexcept
        {
                texture.named_framebuffer_texture(m_framebuffer, attachment, level);
        }
};

class ShaderStorageBuffer final
{
        GLuint m_buffer = 0;

public:
        ShaderStorageBuffer() noexcept
        {
                glCreateBuffers(1, &m_buffer);
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_buffer);
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }
        ~ShaderStorageBuffer()
        {
                glDeleteBuffers(1, &m_buffer);
        }
        ShaderStorageBuffer(const ShaderStorageBuffer&) = delete;
        ShaderStorageBuffer& operator=(const ShaderStorageBuffer&) = delete;
        ShaderStorageBuffer(ShaderStorageBuffer&& from) noexcept
        {
                *this = std::move(from);
        }
        ShaderStorageBuffer& operator=(ShaderStorageBuffer&& from) noexcept
        {
                if (this == &from)
                {
                        return *this;
                }
                glDeleteBuffers(1, &m_buffer);
                m_buffer = from.m_buffer;
                from.m_buffer = 0;
                return *this;
        }

        template <typename T>
        void load_static_draw(const std::vector<T>& data) const
        {
                glNamedBufferData(m_buffer, data.size() * sizeof(T), data.data(), GL_STATIC_DRAW);
        }
        template <typename T>
        void load_static_copy(const std::vector<T>& data) const
        {
                glNamedBufferData(m_buffer, data.size() * sizeof(T), data.data(), GL_STATIC_COPY);
        }
        template <typename T>
        void load_dynamic_draw(const std::vector<T>& data) const
        {
                glNamedBufferData(m_buffer, data.size() * sizeof(T), data.data(), GL_DYNAMIC_DRAW);
        }
        template <typename T>
        void load_dynamic_copy(const std::vector<T>& data) const
        {
                glNamedBufferData(m_buffer, data.size() * sizeof(T), data.data(), GL_DYNAMIC_COPY);
        }
        void create_dynamic_copy(size_t size) const noexcept
        {
                glNamedBufferData(m_buffer, size, nullptr, GL_DYNAMIC_COPY);
        }
        void create_static_copy(size_t size) const noexcept
        {
                glNamedBufferData(m_buffer, size, nullptr, GL_STATIC_COPY);
        }
        template <typename T>
        void read(std::vector<T>* data) const
        {
                glGetNamedBufferSubData(m_buffer, 0, data->size() * sizeof(T), data->data());
        }

        void bind(GLuint binding_point) const noexcept
        {
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding_point, m_buffer);
        }
};

class ArrayBuffer final
{
        GLuint m_buffer = 0;

public:
        ArrayBuffer() noexcept
        {
                glCreateBuffers(1, &m_buffer);
                glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
        ~ArrayBuffer()
        {
                glDeleteBuffers(1, &m_buffer);
        }
        ArrayBuffer(const ArrayBuffer&) = delete;
        ArrayBuffer& operator=(const ArrayBuffer&) = delete;
        ArrayBuffer(ArrayBuffer&& from) noexcept
        {
                *this = std::move(from);
        }
        ArrayBuffer& operator=(ArrayBuffer&& from) noexcept
        {
                if (this == &from)
                {
                        return *this;
                }
                glDeleteBuffers(1, &m_buffer);
                m_buffer = from.m_buffer;
                from.m_buffer = 0;
                return *this;
        }

        void vertex_array_vertex_buffer(GLuint vertex_array, GLuint binding_index, GLintptr offset, GLsizei stride) const noexcept
        {
                glVertexArrayVertexBuffer(vertex_array, binding_index, m_buffer, offset, stride);
        }

        template <typename T>
        void load_static_draw(const std::vector<T>& v) const
        {
                glNamedBufferData(m_buffer, v.size() * sizeof(T), v.data(), GL_STATIC_DRAW);
        }

        template <typename T>
        void load_dynamic_draw(const std::vector<T>& v) const
        {
                glNamedBufferData(m_buffer, v.size() * sizeof(T), v.data(), GL_DYNAMIC_DRAW);
        }
};

class VertexArray final
{
        GLuint m_vertex_array = 0;

public:
        VertexArray() noexcept
        {
                glCreateVertexArrays(1, &m_vertex_array);
        }
        ~VertexArray()
        {
                glDeleteVertexArrays(1, &m_vertex_array);
        }
        VertexArray(const VertexArray&) = delete;
        VertexArray& operator=(const VertexArray&) = delete;
        VertexArray(VertexArray&& from) noexcept
        {
                *this = std::move(from);
        }
        VertexArray& operator=(VertexArray&& from) noexcept
        {
                if (this == &from)
                {
                        return *this;
                }
                glDeleteVertexArrays(1, &m_vertex_array);
                m_vertex_array = from.m_vertex_array;
                from.m_vertex_array = 0;
                return *this;
        }

        void bind() const noexcept
        {
                glBindVertexArray(m_vertex_array);
        }

        void attrib_pointer(GLuint attrib_index, GLint size, GLenum type, const ArrayBuffer& buffer, GLintptr offset,
                            GLsizei stride, bool enable) const noexcept
        {
                GLuint binding_index = attrib_index;
                glVertexArrayAttribFormat(m_vertex_array, attrib_index, size, type, GL_FALSE, 0);
                glVertexArrayAttribBinding(m_vertex_array, attrib_index, binding_index);
                buffer.vertex_array_vertex_buffer(m_vertex_array, binding_index, offset, stride);
                if (enable)
                {
                        glEnableVertexArrayAttrib(m_vertex_array, attrib_index);
                }
        }
        void attrib_i_pointer(GLuint attrib_index, GLint size, GLenum type, const ArrayBuffer& buffer, GLintptr offset,
                              GLsizei stride, bool enable) const noexcept
        {
                GLuint binding_index = attrib_index;
                glVertexArrayAttribIFormat(m_vertex_array, attrib_index, size, type, 0);
                glVertexArrayAttribBinding(m_vertex_array, attrib_index, binding_index);
                buffer.vertex_array_vertex_buffer(m_vertex_array, binding_index, offset, stride);
                if (enable)
                {
                        glEnableVertexArrayAttrib(m_vertex_array, attrib_index);
                }
        }
        void enable_attrib(GLuint index) const noexcept
        {
                glEnableVertexArrayAttrib(m_vertex_array, index);
        }
};

class TextureRGBA32F final
{
        Texture2D m_texture;

public:
        TextureRGBA32F(const sf::Image& image)
        {
                static_assert(sizeof(sf::Uint8) == sizeof(GLubyte));
                m_texture.texture_storage_2d(1, GL_RGBA32F, image.getSize().x, image.getSize().y);
                m_texture.texture_sub_image_2d(0, 0, 0, image.getSize().x, image.getSize().y, GL_RGBA, GL_UNSIGNED_BYTE,
                                               image.getPixelsPtr());
                m_texture.texture_parameter(GL_TEXTURE_WRAP_S, GL_REPEAT);
                m_texture.texture_parameter(GL_TEXTURE_WRAP_T, GL_REPEAT);
                m_texture.texture_parameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                m_texture.texture_parameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        }
        TextureRGBA32F(int width, int height) noexcept
        {
                m_texture.texture_storage_2d(1, GL_RGBA32F, width, height);
                m_texture.texture_parameter(GL_TEXTURE_WRAP_S, GL_REPEAT);
                m_texture.texture_parameter(GL_TEXTURE_WRAP_T, GL_REPEAT);
                m_texture.texture_parameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                m_texture.texture_parameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        }

        GLuint64 get_image_resident_handle_write_only() const noexcept
        {
                return m_texture.get_image_resident_handle(0, GL_FALSE, 0, GL_RGBA32F, GL_WRITE_ONLY);
        }
        GLuint64 get_image_resident_handle_read_only() const noexcept
        {
                return m_texture.get_image_resident_handle(0, GL_FALSE, 0, GL_RGBA32F, GL_READ_ONLY);
        }
        GLuint64 get_image_resident_handle_read_write() const noexcept
        {
                return m_texture.get_image_resident_handle(0, GL_FALSE, 0, GL_RGBA32F, GL_READ_WRITE);
        }

        void copy_texture_sub_image() const noexcept
        {
                m_texture.copy_texture_sub_image_2d(0, 0, 0, 0, 0, m_texture.get_width(), m_texture.get_height());
        }

        const Texture2D& get_texture() const noexcept
        {
                return m_texture;
        }
};

class TextureR32F final
{
        Texture2D m_texture;

public:
        TextureR32F(int w, int h, const unsigned char* buffer)
        {
                m_texture.texture_storage_2d(1, GL_R32F, w, h);
                m_texture.texture_sub_image_2d(0, 0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, buffer);
                m_texture.texture_parameter(GL_TEXTURE_WRAP_S, GL_REPEAT);
                m_texture.texture_parameter(GL_TEXTURE_WRAP_T, GL_REPEAT);
                m_texture.texture_parameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                m_texture.texture_parameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        }

        TextureR32F(int w, int h)
        {
                m_texture.texture_storage_2d(1, GL_R32F, w, h);
                m_texture.texture_parameter(GL_TEXTURE_WRAP_S, GL_REPEAT);
                m_texture.texture_parameter(GL_TEXTURE_WRAP_T, GL_REPEAT);
                m_texture.texture_parameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                m_texture.texture_parameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        }

        GLuint64 get_image_resident_handle_write_only() const noexcept
        {
                return m_texture.get_image_resident_handle(0, GL_FALSE, 0, GL_R32F, GL_WRITE_ONLY);
        }
        GLuint64 get_image_resident_handle_read_only() const noexcept
        {
                return m_texture.get_image_resident_handle(0, GL_FALSE, 0, GL_R32F, GL_READ_ONLY);
        }
        GLuint64 get_image_resident_handle_read_write() const noexcept
        {
                return m_texture.get_image_resident_handle(0, GL_FALSE, 0, GL_R32F, GL_READ_WRITE);
        }

        void clear_tex_image(GLfloat v) const noexcept
        {
                m_texture.clear_tex_image(0, GL_RED, GL_FLOAT, &v);
        }
        void get_texture_image(std::vector<GLfloat>* d) const noexcept
        {
                m_texture.get_texture_image(0, GL_RED, GL_FLOAT, m_texture.get_width() * m_texture.get_height() * sizeof(GLfloat),
                                            &(*d)[0]);
        }
        void get_texture_sub_image(GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLfloat* pixels) const noexcept
        {
                m_texture.get_texture_sub_image(0, xoffset, yoffset, 0, width, height, 1, GL_RED, GL_FLOAT,
                                                width * height * sizeof(GLfloat), pixels);
        }

        const Texture2D& get_texture() const noexcept
        {
                return m_texture;
        }
};

class TextureR32I final
{
        Texture2D m_texture;

public:
        TextureR32I(int w, int h)
        {
                m_texture.texture_storage_2d(1, GL_R32I, w, h);
                m_texture.texture_parameter(GL_TEXTURE_WRAP_S, GL_REPEAT);
                m_texture.texture_parameter(GL_TEXTURE_WRAP_T, GL_REPEAT);
                m_texture.texture_parameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                m_texture.texture_parameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        }

        GLuint64 get_image_resident_handle_write_only() const noexcept
        {
                return m_texture.get_image_resident_handle(0, GL_FALSE, 0, GL_R32I, GL_WRITE_ONLY);
        }
        GLuint64 get_image_resident_handle_read_only() const noexcept
        {
                return m_texture.get_image_resident_handle(0, GL_FALSE, 0, GL_R32I, GL_READ_ONLY);
        }
        GLuint64 get_image_resident_handle_read_write() const noexcept
        {
                return m_texture.get_image_resident_handle(0, GL_FALSE, 0, GL_R32I, GL_READ_WRITE);
        }

        void clear_tex_image(GLint v) const noexcept
        {
                m_texture.clear_tex_image(0, GL_RED_INTEGER, GL_INT, &v);
        }
        void get_texture_image(std::vector<GLint>* d) const noexcept
        {
                m_texture.get_texture_image(0, GL_RED_INTEGER, GL_INT,
                                            m_texture.get_width() * m_texture.get_height() * sizeof(GLint), &(*d)[0]);
        }
        void get_texture_sub_image(GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLint* pixels) const noexcept
        {
                m_texture.get_texture_sub_image(0, xoffset, yoffset, 0, width, height, 1, GL_RED_INTEGER, GL_INT,
                                                width * height * sizeof(GLint), pixels);
        }

        const Texture2D& get_texture() const noexcept
        {
                return m_texture;
        }
};

class ShadowBuffer final
{
        FrameBuffer m_fb;
        Texture2D m_depth;

public:
        ShadowBuffer(int width, int height)
        {
                m_depth.texture_storage_2d(1, GL_DEPTH_COMPONENT32, width, height);
                m_depth.texture_parameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                m_depth.texture_parameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                m_depth.texture_parameter(GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
                m_depth.texture_parameter(GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
                m_depth.texture_parameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                m_depth.texture_parameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

                m_fb.named_framebuffer_texture(GL_DEPTH_ATTACHMENT, m_depth, 0);

                GLenum check = m_fb.check_named_framebuffer_status();
                if (check != GL_FRAMEBUFFER_COMPLETE)
                {
                        error("Error create shadow framebuffer: " + std::to_string(check));
                }
        }

        void bind_buffer() const noexcept
        {
                m_fb.bind_framebuffer();
        }
        void unbind_buffer() const noexcept
        {
                m_fb.unbind_framebuffer();
        }

        const Texture2D& get_texture() const noexcept
        {
                return m_depth;
        }
};

class ColorBuffer final
{
        FrameBuffer m_fb;
        Texture2D m_color;
        Texture2D m_depth;

public:
        ColorBuffer(int width, int height)
        {
                m_depth.texture_storage_2d(1, GL_DEPTH_COMPONENT32, width, height);
                m_depth.texture_parameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                m_depth.texture_parameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                m_depth.texture_parameter(GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
                m_depth.texture_parameter(GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
                m_depth.texture_parameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                m_depth.texture_parameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

                m_color.texture_storage_2d(1, GL_RGBA32F, width, height);
                m_color.texture_parameter(GL_TEXTURE_WRAP_S, GL_REPEAT);
                m_color.texture_parameter(GL_TEXTURE_WRAP_T, GL_REPEAT);
                m_color.texture_parameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                m_color.texture_parameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                m_fb.named_framebuffer_texture(GL_DEPTH_ATTACHMENT, m_depth, 0);
                m_fb.named_framebuffer_texture(GL_COLOR_ATTACHMENT0, m_color, 0);

                GLenum check = m_fb.check_named_framebuffer_status();
                if (check != GL_FRAMEBUFFER_COMPLETE)
                {
                        error("Error create framebuffer: " + std::to_string(check));
                }

                const GLenum draw_buffers[] = {GL_COLOR_ATTACHMENT0};
                m_fb.named_framebuffer_draw_buffers(1, draw_buffers);
        }

        void bind_buffer() const noexcept
        {
                m_fb.bind_framebuffer();
        }
        void unbind_buffer() const noexcept
        {
                m_fb.unbind_framebuffer();
        }

        const Texture2D& get_texture() const noexcept
        {
                return m_color;
        }
};
