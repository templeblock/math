/*
Copyright (C) 2017-2019 Topological Manifold

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

#include "renderer.h"

#include "com/log.h"
#include "com/math.h"
#include "com/string/vector.h"
#include "com/time.h"
#include "com/vec.h"
#include "graphics/vulkan/create.h"
#include "graphics/vulkan/device.h"
#include "graphics/vulkan/error.h"
#include "graphics/vulkan/query.h"
#include "obj/alg/alg.h"
#include "show/renderers/com/storage.h"
#include "show/renderers/vulkan/objects/buffers.h"
#include "show/renderers/vulkan/objects/sampler.h"
#include "show/renderers/vulkan/shader/memory.h"
#include "show/renderers/vulkan/shader/vertex.h"

#include <algorithm>
#include <array>
#include <thread>
#include <unordered_set>

// clang-format off
constexpr std::initializer_list<const char*> INSTANCE_EXTENSIONS =
{
};
constexpr std::initializer_list<const char*> DEVICE_EXTENSIONS =
{
};
constexpr std::initializer_list<vulkan::PhysicalDeviceFeatures> REQUIRED_DEVICE_FEATURES =
{
        vulkan::PhysicalDeviceFeatures::GeometryShader,
        vulkan::PhysicalDeviceFeatures::FragmentStoresAndAtomics
};
constexpr std::initializer_list<VkFormat> DEPTH_IMAGE_FORMATS =
{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
};
// clang-format on

// Это в шейдерах layout(set = N, ...)
constexpr uint32_t TRIANGLES_SHARED_SET_NUMBER = 0;
constexpr uint32_t TRIANGLES_MATERIAL_SET_NUMBER = 1;
constexpr uint32_t SHADOW_SET_NUMBER = 0;
constexpr uint32_t POINTS_SET_NUMBER = 0;

static_assert(TRIANGLES_SHARED_SET_NUMBER != TRIANGLES_MATERIAL_SET_NUMBER);

// Число используется в шейдере для определения наличия текстурных координат
constexpr vec2f NO_TEXTURE_COORDINATES = vec2f(-1e10);

// constexpr VkIndexType VULKAN_VERTEX_INDEX_TYPE = VK_INDEX_TYPE_UINT32;
// using VERTEX_INDEX_TYPE =
//        std::conditional_t<VULKAN_VERTEX_INDEX_TYPE == VK_INDEX_TYPE_UINT32, uint32_t,
//                           std::conditional_t<VULKAN_VERTEX_INDEX_TYPE == VK_INDEX_TYPE_UINT16, uint16_t, void>>;

// clang-format off
constexpr uint32_t triangles_vert[]
{
#include "renderer_triangles.vert.spr"
};
constexpr uint32_t triangles_geom[]
{
#include "renderer_triangles.geom.spr"
};
constexpr uint32_t triangles_frag[]
{
#include "renderer_triangles.frag.spr"
};
constexpr uint32_t shadow_vert[]
{
#include "renderer_shadow.vert.spr"
};
constexpr uint32_t shadow_frag[]
{
#include "renderer_shadow.frag.spr"
};
constexpr uint32_t points_vert[]
{
#include "renderer_points.vert.spr"
};
constexpr uint32_t points_frag[]
{
#include "renderer_points.frag.spr"
};
// clang-format on

namespace impl = vulkan_renderer_implementation;

namespace
{
std::unique_ptr<vulkan::VertexBufferWithDeviceLocalMemory> load_vertices(const vulkan::VulkanInstance& instance,
                                                                         const Obj<3>& obj,
                                                                         const std::vector<int>& sorted_face_indices)
{
        if (obj.facets().size() == 0)
        {
                error("No OBJ facets found");
        }

        ASSERT(sorted_face_indices.size() == obj.facets().size());

        const std::vector<Obj<3>::Facet>& obj_faces = obj.facets();
        const std::vector<vec3f>& obj_vertices = obj.vertices();
        const std::vector<vec3f>& obj_normals = obj.normals();
        const std::vector<vec2f>& obj_texcoords = obj.texcoords();

        std::vector<impl::Vertex> shader_vertices;
        shader_vertices.reserve(3 * obj_faces.size());

        vec3f v0, v1, v2;
        vec3f n0, n1, n2;
        vec2f t0, t1, t2;

        for (int face_index : sorted_face_indices)
        {
                const Obj<3>::Facet& f = obj_faces[face_index];

                v0 = obj_vertices[f.vertices[0]];
                v1 = obj_vertices[f.vertices[1]];
                v2 = obj_vertices[f.vertices[2]];

                vec3f geometric_normal = normalize(cross(v1 - v0, v2 - v0));
                if (!is_finite(geometric_normal))
                {
                        error("Face unit orthogonal vector is not finite for the face with vertices (" + to_string(v0) + ", " +
                              to_string(v1) + ", " + to_string(v2) + ")");
                }

                if (f.has_normal)
                {
                        n0 = obj_normals[f.normals[0]];
                        n1 = obj_normals[f.normals[1]];
                        n2 = obj_normals[f.normals[2]];

                        // Векторы вершин грани могут быть направлены в разные стороны от грани,
                        // поэтому надо им задать одинаковое направление
                        n0 = dot(n0, geometric_normal) >= 0 ? n0 : -n0;
                        n1 = dot(n1, geometric_normal) >= 0 ? n1 : -n1;
                        n2 = dot(n2, geometric_normal) >= 0 ? n2 : -n2;
                }
                else
                {
                        n0 = n1 = n2 = geometric_normal;
                }

                if (f.has_texcoord)
                {
                        t0 = obj_texcoords[f.texcoords[0]];
                        t1 = obj_texcoords[f.texcoords[1]];
                        t2 = obj_texcoords[f.texcoords[2]];
                }
                else
                {
                        t0 = t1 = t2 = NO_TEXTURE_COORDINATES;
                }

                shader_vertices.emplace_back(v0, n0, geometric_normal, t0);
                shader_vertices.emplace_back(v1, n1, geometric_normal, t1);
                shader_vertices.emplace_back(v2, n2, geometric_normal, t2);
        }

        ASSERT((shader_vertices.size() >= 3) && (shader_vertices.size() % 3 == 0));

        return std::make_unique<vulkan::VertexBufferWithDeviceLocalMemory>(instance.create_vertex_buffer(shader_vertices));
}

std::unique_ptr<vulkan::VertexBufferWithDeviceLocalMemory> load_point_vertices(const vulkan::VulkanInstance& instance,
                                                                               const Obj<3>& obj)
{
        if (obj.points().size() == 0)
        {
                error("No OBJ points found");
        }

        const std::vector<Obj<3>::Point>& obj_points = obj.points();
        const std::vector<vec3f>& obj_vertices = obj.vertices();

        std::vector<impl::PointVertex> shader_vertices;
        shader_vertices.reserve(obj_points.size());

        for (const Obj<3>::Point& p : obj_points)
        {
                shader_vertices.push_back(obj_vertices[p.vertex]);
        }

        return std::make_unique<vulkan::VertexBufferWithDeviceLocalMemory>(instance.create_vertex_buffer(shader_vertices));
}

std::unique_ptr<vulkan::VertexBufferWithDeviceLocalMemory> load_line_vertices(const vulkan::VulkanInstance& instance,
                                                                              const Obj<3>& obj)
{
        if (obj.lines().size() == 0)
        {
                error("No OBJ lines found");
        }

        const std::vector<Obj<3>::Line>& obj_lines = obj.lines();
        const std::vector<vec3f>& obj_vertices = obj.vertices();

        std::vector<impl::PointVertex> shader_vertices;
        shader_vertices.reserve(2 * obj_lines.size());

        for (const Obj<3>::Line& line : obj_lines)
        {
                for (int index : line.vertices)
                {
                        shader_vertices.push_back(obj_vertices[index]);
                }
        }

        return std::make_unique<vulkan::VertexBufferWithDeviceLocalMemory>(instance.create_vertex_buffer(shader_vertices));
}

std::vector<vulkan::ColorTexture> load_textures(const vulkan::VulkanInstance& instance, const Obj<3>& obj)
{
        std::vector<vulkan::ColorTexture> textures;

        for (const typename Obj<3>::Image& image : obj.images())
        {
                textures.push_back(instance.create_texture(image.size[0], image.size[1], image.srgba_pixels));
        }

        // На одну текстуру больше для её указания, но не использования в тех материалах, где нет текстуры
        std::vector<std::uint_least8_t> pixels = {/*0*/ 0, 0, 0, 0, /*1*/ 0, 0, 0, 0,
                                                  /*2*/ 0, 0, 0, 0, /*3*/ 0, 0, 0, 0};
        textures.push_back(instance.create_texture(2, 2, pixels));

        return textures;
}

std::unique_ptr<impl::TrianglesMaterialMemory> load_materials(const vulkan::Device& device, VkSampler sampler,
                                                              VkDescriptorSetLayout descriptor_set_layout, const Obj<3>& obj,
                                                              const std::vector<vulkan::ColorTexture>& textures)
{
        // Текстур имеется больше на одну для её использования в тех материалах, где нет текстуры

        ASSERT(textures.size() == obj.images().size() + 1);

        const vulkan::ColorTexture* const no_texture = &textures.back();

        std::vector<impl::TrianglesMaterialMemory::MaterialAndTexture> materials;
        materials.reserve(obj.materials().size() + 1);

        for (const typename Obj<3>::Material& material : obj.materials())
        {
                ASSERT(material.map_Ka < static_cast<int>(textures.size()) - 1);
                ASSERT(material.map_Kd < static_cast<int>(textures.size()) - 1);
                ASSERT(material.map_Ks < static_cast<int>(textures.size()) - 1);

                impl::TrianglesMaterialMemory::MaterialAndTexture m;

                m.material.Ka = material.Ka.to_rgb_vector<float>();
                m.material.Kd = material.Kd.to_rgb_vector<float>();
                m.material.Ks = material.Ks.to_rgb_vector<float>();

                m.material.Ns = material.Ns;

                m.material.use_texture_Ka = (material.map_Ka >= 0) ? 1 : 0;
                m.texture_Ka = (material.map_Ka >= 0) ? &textures[material.map_Ka] : no_texture;

                m.material.use_texture_Kd = (material.map_Kd >= 0) ? 1 : 0;
                m.texture_Kd = (material.map_Kd >= 0) ? &textures[material.map_Kd] : no_texture;

                m.material.use_texture_Ks = (material.map_Ks >= 0) ? 1 : 0;
                m.texture_Ks = (material.map_Ks >= 0) ? &textures[material.map_Ks] : no_texture;

                m.material.use_material = 1;

                materials.push_back(m);
        }

        // На один материал больше для его указания, но не использования в вершинах, не имеющих материала
        impl::TrianglesMaterialMemory::MaterialAndTexture m;
        m.material.Ka = vec3f(0);
        m.material.Kd = vec3f(0);
        m.material.Ks = vec3f(0);
        m.material.Ns = 0;
        m.material.use_texture_Ka = 0;
        m.texture_Ka = no_texture;
        m.material.use_texture_Kd = 0;
        m.texture_Kd = no_texture;
        m.material.use_texture_Ks = 0;
        m.texture_Ks = no_texture;
        m.material.use_material = 0;
        materials.push_back(m);

        return std::make_unique<impl::TrianglesMaterialMemory>(device, sampler, descriptor_set_layout, materials);
}

struct DrawInfo
{
        VkPipelineLayout triangles_pipeline_layout;
        VkPipeline triangles_pipeline;
        VkDescriptorSet triangles_shared_set;
        unsigned triangles_shared_set_number;
        unsigned triangles_material_set_number;

        VkPipelineLayout points_pipeline_layout;
        VkPipeline points_pipeline;
        VkDescriptorSet points_set;
        unsigned points_set_number;

        VkPipelineLayout lines_pipeline_layout;
        VkPipeline lines_pipeline;
        VkDescriptorSet lines_set;
        unsigned lines_set_number;
};

struct DrawShadowInfo
{
        VkPipelineLayout triangles_pipeline_layout;
        VkPipeline triangles_pipeline;
        VkDescriptorSet triangles_set;
        unsigned triangles_set_number;
};

struct DrawObjectInterface
{
        virtual ~DrawObjectInterface() = default;

        virtual bool has_shadow() const noexcept = 0;

        virtual void draw_commands(VkCommandBuffer command_buffer, const DrawInfo& info) const = 0;
        virtual void draw_shadow_commands(VkCommandBuffer command_buffer, const DrawShadowInfo& info) const = 0;
};

class DrawObjectTriangles final : public DrawObjectInterface
{
        std::unique_ptr<vulkan::VertexBufferWithDeviceLocalMemory> m_vertex_buffer;
        std::vector<vulkan::ColorTexture> m_textures;
        std::unique_ptr<impl::TrianglesMaterialMemory> m_shader_memory;
        unsigned m_vertex_count;

        //

        std::array<VkBuffer, 1> m_buffers;
        std::array<VkDeviceSize, 1> m_offsets;

        struct Material
        {
                VkDescriptorSet set;
                unsigned vertex_offset;
                unsigned vertex_count;

                Material(VkDescriptorSet set_, unsigned vertex_offset_, unsigned vertex_count_)
                        : set(set_), vertex_offset(vertex_offset_), vertex_count(vertex_count_)
                {
                }
        };
        std::vector<Material> m_materials;

public:
        DrawObjectTriangles(const vulkan::VulkanInstance& instance, VkSampler sampler,
                            VkDescriptorSetLayout triangles_material_descriptor_set_layout, const Obj<3>& obj)
        {
                ASSERT(obj.facets().size() > 0);

                std::vector<int> sorted_face_indices;
                std::vector<int> material_face_offset;
                std::vector<int> material_face_count;

                sort_facets_by_material(obj, sorted_face_indices, material_face_offset, material_face_count);

                m_vertex_buffer = load_vertices(instance, obj, sorted_face_indices);
                m_textures = load_textures(instance, obj);
                m_shader_memory =
                        load_materials(instance.device(), sampler, triangles_material_descriptor_set_layout, obj, m_textures);
                m_vertex_count = 3 * obj.facets().size();

                ASSERT(material_face_offset.size() == material_face_count.size());
                ASSERT(material_face_offset.size() == m_shader_memory->descriptor_set_count());

                for (unsigned i = 0; i < m_shader_memory->descriptor_set_count(); ++i)
                {
                        if (material_face_count[i] > 0)
                        {
                                m_materials.emplace_back(m_shader_memory->descriptor_set(i), 3 * material_face_offset[i],
                                                         3 * material_face_count[i]);
                        }
                }

                m_buffers = {*m_vertex_buffer};
                m_offsets = {0};
        }

        bool has_shadow() const noexcept override
        {
                return true;
        }

        void draw_commands(VkCommandBuffer command_buffer, const DrawInfo& info) const override
        {
                vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, info.triangles_pipeline);

                vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, info.triangles_pipeline_layout,
                                        info.triangles_shared_set_number, 1 /*set count*/, &info.triangles_shared_set, 0,
                                        nullptr);

                vkCmdBindVertexBuffers(command_buffer, 0, m_buffers.size(), m_buffers.data(), m_offsets.data());

                for (const Material& material : m_materials)
                {
                        ASSERT(material.vertex_count > 0);

                        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, info.triangles_pipeline_layout,
                                                info.triangles_material_set_number, 1 /*set count*/, &material.set, 0, nullptr);

                        vkCmdDraw(command_buffer, material.vertex_count, 1, material.vertex_offset, 0);
                }
        }

        void draw_shadow_commands(VkCommandBuffer command_buffer, const DrawShadowInfo& info) const override
        {
                vkCmdSetDepthBias(command_buffer, 1.5f, 0.0f, 1.5f);

                vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, info.triangles_pipeline);

                vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, info.triangles_pipeline_layout,
                                        info.triangles_set_number, 1 /*set count*/, &info.triangles_set, 0, nullptr);

                vkCmdBindVertexBuffers(command_buffer, 0, m_buffers.size(), m_buffers.data(), m_offsets.data());

                vkCmdDraw(command_buffer, m_vertex_count, 1, 0, 0);
        }
};

class DrawObjectPoints final : public DrawObjectInterface
{
        std::unique_ptr<vulkan::VertexBufferWithDeviceLocalMemory> m_vertex_buffer;
        unsigned m_vertex_count;

        //

        std::array<VkBuffer, 1> m_buffers;
        std::array<VkDeviceSize, 1> m_offsets;

public:
        DrawObjectPoints(const vulkan::VulkanInstance& instance, const Obj<3>& obj)
        {
                ASSERT(obj.points().size() > 0);

                m_vertex_buffer = load_point_vertices(instance, obj);
                m_vertex_count = obj.points().size();

                m_buffers = {*m_vertex_buffer};
                m_offsets = {0};
        }

        bool has_shadow() const noexcept override
        {
                return false;
        }

        void draw_commands(VkCommandBuffer command_buffer, const DrawInfo& info) const override
        {
                vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, info.points_pipeline);

                vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, info.points_pipeline_layout,
                                        info.points_set_number, 1 /*set count*/, &info.points_set, 0, nullptr);

                vkCmdBindVertexBuffers(command_buffer, 0, m_buffers.size(), m_buffers.data(), m_offsets.data());

                vkCmdDraw(command_buffer, m_vertex_count, 1, 0, 0);
        }

        void draw_shadow_commands(VkCommandBuffer /*command_buffer*/, const DrawShadowInfo& /*info*/) const override
        {
        }
};

class DrawObjectLines final : public DrawObjectInterface
{
        std::unique_ptr<vulkan::VertexBufferWithDeviceLocalMemory> m_vertex_buffer;
        unsigned m_vertex_count;

        //

        std::array<VkBuffer, 1> m_buffers;
        std::array<VkDeviceSize, 1> m_offsets;

public:
        DrawObjectLines(const vulkan::VulkanInstance& instance, const Obj<3>& obj)
        {
                ASSERT(obj.lines().size() > 0);

                m_vertex_buffer = load_line_vertices(instance, obj);
                m_vertex_count = 2 * obj.lines().size();

                m_buffers = {*m_vertex_buffer};
                m_offsets = {0};
        }

        bool has_shadow() const noexcept override
        {
                return false;
        }

        void draw_commands(VkCommandBuffer command_buffer, const DrawInfo& info) const override
        {
                vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, info.lines_pipeline);

                vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, info.lines_pipeline_layout,
                                        info.lines_set_number, 1 /*set count*/, &info.lines_set, 0, nullptr);

                vkCmdBindVertexBuffers(command_buffer, 0, m_buffers.size(), m_buffers.data(), m_offsets.data());

                vkCmdDraw(command_buffer, m_vertex_count, 1, 0, 0);
        }

        void draw_shadow_commands(VkCommandBuffer /*command_buffer*/, const DrawShadowInfo& /*info*/) const override
        {
        }
};

class DrawObject
{
        mat4 m_model_matrix;
        std::vector<std::unique_ptr<DrawObjectInterface>> m_objects;

        bool m_has_shadow;

public:
        DrawObject(const vulkan::VulkanInstance& instance, VkSampler sampler, VkDescriptorSetLayout descriptor_set_layout,
                   const Obj<3>& obj, double size, const vec3& position)
                : m_model_matrix(model_vertex_matrix(obj, size, position))
        {
                if (obj.facets().size() > 0)
                {
                        m_objects.push_back(std::make_unique<DrawObjectTriangles>(instance, sampler, descriptor_set_layout, obj));
                }
                if (obj.points().size() > 0)
                {
                        m_objects.push_back(std::make_unique<DrawObjectPoints>(instance, obj));
                }
                if (obj.lines().size() > 0)
                {
                        m_objects.push_back(std::make_unique<DrawObjectLines>(instance, obj));
                }

                m_has_shadow = false;
                for (const std::unique_ptr<DrawObjectInterface>& object : m_objects)
                {
                        m_has_shadow = m_has_shadow || object->has_shadow();
                }
        }

        bool has_shadow() const noexcept
        {
                return m_has_shadow;
        }

        const mat4& model_matrix() const noexcept
        {
                return m_model_matrix;
        }

        void draw_commands(VkCommandBuffer command_buffer, const DrawInfo& info) const
        {
                for (const std::unique_ptr<DrawObjectInterface>& object : m_objects)
                {
                        object->draw_commands(command_buffer, info);
                }
        }

        void draw_shadow_commands(VkCommandBuffer command_buffer, const DrawShadowInfo& info) const
        {
                for (const std::unique_ptr<DrawObjectInterface>& object : m_objects)
                {
                        object->draw_shadow_commands(command_buffer, info);
                }
        }
};

class Renderer final : public VulkanRenderer
{
        // Для получения текстуры для тени результат рисования находится в интервалах x(-1, 1) y(-1, 1) z(0, 1).
        // Для работы с этой текстурой надо преобразовать в интервалы x(0, 1) y(0, 1) z(0, 1).
        static constexpr mat4 SCALE = scale<double>(0.5, 0.5, 1);
        static constexpr mat4 TRANSLATE = translate<double>(1, 1, 0);
        const mat4 SCALE_BIAS_MATRIX = SCALE * TRANSLATE;

        const std::thread::id m_thread_id = std::this_thread::get_id();

        const int m_minimum_sample_count;
        const bool m_sample_shading;

        Color m_clear_color = Color(0);

        mat4 m_main_matrix = mat4(1);
        mat4 m_shadow_matrix = mat4(1);
        mat4 m_scale_bias_shadow_matrix = mat4(1);

        double m_shadow_zoom = 1;
        bool m_show_shadow = false;

        const vulkan::VulkanInstance& m_instance;
        const vulkan::Swapchain* m_swapchain = nullptr;

        std::vector<vulkan::Semaphore> m_shadow_available_semaphores;

        vulkan::Sampler m_texture_sampler;
        vulkan::Sampler m_shadow_sampler;

        vulkan::DescriptorSetLayout m_triangles_material_descriptor_set_layout;

        impl::TrianglesSharedMemory m_triangles_shared_shader_memory;
        impl::ShadowMemory m_shadow_shader_memory;
        impl::PointsMemory m_points_shader_memory;

        vulkan::VertexShader m_triangles_vert;
        vulkan::GeometryShader m_triangles_geom;
        vulkan::FragmentShader m_triangles_frag;

        vulkan::VertexShader m_shadow_vert;
        vulkan::FragmentShader m_shadow_frag;

        vulkan::VertexShader m_points_vert;
        vulkan::FragmentShader m_points_frag;

        vulkan::PipelineLayout m_triangles_pipeline_layout;
        vulkan::PipelineLayout m_shadow_pipeline_layout;
        vulkan::PipelineLayout m_points_pipeline_layout;

        std::unique_ptr<impl::MainBuffers> m_main_buffers;
        std::unique_ptr<impl::ShadowBuffers> m_shadow_buffers;
        std::unique_ptr<vulkan::StorageImage> m_object_image;

        RendererObjectStorage<DrawObject> m_storage;

        VkPipeline m_triangles_pipeline = VK_NULL_HANDLE;
        VkPipeline m_shadow_pipeline = VK_NULL_HANDLE;
        VkPipeline m_points_pipeline = VK_NULL_HANDLE;
        VkPipeline m_lines_pipeline = VK_NULL_HANDLE;

        void set_light_a(const Color& light) override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                m_triangles_shared_shader_memory.set_light_a(light);
                m_points_shader_memory.set_light_a(light);
        }
        void set_light_d(const Color& light) override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                m_triangles_shared_shader_memory.set_light_d(light);
        }
        void set_light_s(const Color& light) override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                m_triangles_shared_shader_memory.set_light_s(light);
        }
        void set_background_color(const Color& color) override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                m_clear_color = color;
                m_points_shader_memory.set_background_color(color);
                create_command_buffers();
        }
        void set_default_color(const Color& color) override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                m_triangles_shared_shader_memory.set_default_color(color);
                m_points_shader_memory.set_default_color(color);
        }
        void set_wireframe_color(const Color& color) override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                m_triangles_shared_shader_memory.set_wireframe_color(color);
        }
        void set_default_ns(double default_ns) override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                m_triangles_shared_shader_memory.set_default_ns(default_ns);
        }
        void set_show_smooth(bool show) override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                m_triangles_shared_shader_memory.set_show_smooth(show);
        }
        void set_show_wireframe(bool show) override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                m_triangles_shared_shader_memory.set_show_wireframe(show);
        }
        void set_show_shadow(bool show) override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                m_triangles_shared_shader_memory.set_show_shadow(show);
                m_show_shadow = show;
        }
        void set_show_fog(bool show) override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                m_points_shader_memory.set_show_fog(show);
        }
        void set_show_materials(bool show) override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                m_triangles_shared_shader_memory.set_show_materials(show);
        }
        void set_shadow_zoom(double zoom) override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                m_shadow_zoom = zoom;

                create_buffers();
        }
        void set_matrices(const mat4& shadow_matrix, const mat4& main_matrix) override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                m_main_matrix = main_matrix;
                m_shadow_matrix = shadow_matrix;
                m_scale_bias_shadow_matrix = SCALE_BIAS_MATRIX * shadow_matrix;

                set_matrices();
        }
        void set_light_direction(vec3 dir) override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                m_triangles_shared_shader_memory.set_direction_to_light(-to_vector<float>(dir));
        }
        void set_camera_direction(vec3 dir) override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                m_triangles_shared_shader_memory.set_direction_to_camera(-to_vector<float>(dir));
        }
        void set_size(int /*width*/, int /*height*/) override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());
        }

        void object_add(const Obj<3>* obj, double size, const vec3& position, int id, int scale_id) override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                std::unique_ptr draw_object = std::make_unique<DrawObject>(
                        m_instance, m_texture_sampler, m_triangles_material_descriptor_set_layout, *obj, size, position);

                m_storage.add_object(std::move(draw_object), id, scale_id);

                set_matrices();
        }
        void object_delete(int id) override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                bool delete_and_create_command_buffers = m_storage.is_current_object(id);
                if (delete_and_create_command_buffers)
                {
                        delete_command_buffers();
                }
                m_storage.delete_object(id);
                if (delete_and_create_command_buffers)
                {
                        create_command_buffers();
                }
                set_matrices();
        }
        void object_delete_all() override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                bool delete_and_create_command_buffers = m_storage.object() != nullptr;
                if (delete_and_create_command_buffers)
                {
                        delete_command_buffers();
                }
                m_storage.delete_all();
                if (delete_and_create_command_buffers)
                {
                        create_command_buffers();
                }
                set_matrices();
        }
        void object_show(int id) override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                if (m_storage.is_current_object(id))
                {
                        return;
                }
                const DrawObject* object = m_storage.object();
                m_storage.show_object(id);
                if (object != m_storage.object())
                {
                        create_command_buffers();
                }
                set_matrices();
        }

        bool draw(VkFence queue_fence, VkQueue graphics_queue, VkSemaphore wait_semaphore, VkSemaphore finished_semaphore,
                  unsigned image_index, unsigned current_frame) const override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                //

                if (!m_show_shadow || !m_storage.object() || !m_storage.object()->has_shadow())
                {
                        std::array<VkSemaphore, 1> wait_semaphores = {wait_semaphore};
                        std::array<VkPipelineStageFlags, 1> wait_stages = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

                        VkSubmitInfo info = {};

                        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                        info.waitSemaphoreCount = wait_semaphores.size();
                        info.pWaitSemaphores = wait_semaphores.data();
                        info.pWaitDstStageMask = wait_stages.data();
                        info.commandBufferCount = 1;
                        info.pCommandBuffers = &m_main_buffers->command_buffer(image_index);
                        info.signalSemaphoreCount = 1;
                        info.pSignalSemaphores = &finished_semaphore;

                        VkResult result = vkQueueSubmit(graphics_queue, 1, &info, queue_fence);
                        if (result != VK_SUCCESS)
                        {
                                vulkan::vulkan_function_error("vkQueueSubmit", result);
                        }
                }
                else
                {
                        VkSemaphore shadow_available_semaphore = m_shadow_available_semaphores[current_frame];

                        {
                                VkSubmitInfo info = {};

                                info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                                info.commandBufferCount = 1;
                                info.pCommandBuffers = &m_shadow_buffers->command_buffer();
                                info.signalSemaphoreCount = 1;
                                info.pSignalSemaphores = &shadow_available_semaphore;

                                VkResult result = vkQueueSubmit(graphics_queue, 1, &info, VK_NULL_HANDLE);
                                if (result != VK_SUCCESS)
                                {
                                        vulkan::vulkan_function_error("vkQueueSubmit", result);
                                }
                        }

                        {
                                std::array<VkSemaphore, 2> wait_semaphores;
                                std::array<VkPipelineStageFlags, 2> wait_stages;

                                wait_semaphores[0] = shadow_available_semaphore;
                                wait_stages[0] = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

                                wait_semaphores[1] = wait_semaphore;
                                wait_stages[1] = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

                                VkSubmitInfo info = {};

                                info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                                info.waitSemaphoreCount = wait_semaphores.size();
                                info.pWaitSemaphores = wait_semaphores.data();
                                info.pWaitDstStageMask = wait_stages.data();
                                info.commandBufferCount = 1;
                                info.pCommandBuffers = &m_main_buffers->command_buffer(image_index);
                                info.signalSemaphoreCount = 1;
                                info.pSignalSemaphores = &finished_semaphore;

                                VkResult result = vkQueueSubmit(graphics_queue, 1, &info, queue_fence);
                                if (result != VK_SUCCESS)
                                {
                                        vulkan::vulkan_function_error("vkQueueSubmit", result);
                                }
                        }
                }

                return m_storage.object() != nullptr;
        }

        void create_buffers()
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                //

                ASSERT(m_swapchain);

                m_instance.device_wait_idle();

                m_main_buffers.reset();
                m_shadow_buffers.reset();
                m_object_image.reset();

                //

                m_main_buffers = std::make_unique<impl::MainBuffers>(*m_swapchain, m_instance.attachment_family_indices(),
                                                                     m_instance.device(), m_instance.graphics_command_pool(),
                                                                     m_instance.graphics_queue(), m_minimum_sample_count,
                                                                     DEPTH_IMAGE_FORMATS);

                m_shadow_buffers = std::make_unique<impl::ShadowBuffers>(
                        *m_swapchain, m_instance.attachment_family_indices(), m_instance.device(),
                        m_instance.graphics_command_pool(), m_instance.graphics_queue(), DEPTH_IMAGE_FORMATS, m_shadow_zoom);

                m_object_image = std::make_unique<vulkan::StorageImage>(
                        m_instance.create_storage_image(VK_FORMAT_R32_UINT, m_swapchain->width(), m_swapchain->height()));

                m_triangles_shared_shader_memory.set_shadow_texture(m_shadow_sampler, m_shadow_buffers->texture());
                m_triangles_shared_shader_memory.set_object_image(m_object_image.get());

                m_triangles_pipeline = m_main_buffers->create_pipeline(
                        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, m_sample_shading,
                        {&m_triangles_vert, &m_triangles_geom, &m_triangles_frag}, m_triangles_pipeline_layout,
                        impl::Vertex::binding_descriptions(), impl::Vertex::triangles_attribute_descriptions());
                m_shadow_pipeline = m_shadow_buffers->create_pipeline(
                        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, {&m_shadow_vert, &m_shadow_frag}, m_shadow_pipeline_layout,
                        impl::Vertex::binding_descriptions(), impl::Vertex::shadow_attribute_descriptions());

                m_points_pipeline = m_main_buffers->create_pipeline(
                        VK_PRIMITIVE_TOPOLOGY_POINT_LIST, false, {&m_points_vert, &m_points_frag}, m_points_pipeline_layout,
                        impl::PointVertex::binding_descriptions(), impl::PointVertex::attribute_descriptions());

                m_lines_pipeline = m_main_buffers->create_pipeline(
                        VK_PRIMITIVE_TOPOLOGY_LINE_LIST, false, {&m_points_vert, &m_points_frag}, m_points_pipeline_layout,
                        impl::PointVertex::binding_descriptions(), impl::PointVertex::attribute_descriptions());

                create_command_buffers(false /*wait_idle*/);
        }

        void create_buffers(const vulkan::Swapchain* swapchain) override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                //

                m_swapchain = swapchain;

                create_buffers();
        }

        void delete_buffers() override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                //

                ASSERT((m_main_buffers && m_shadow_buffers && m_object_image) ||
                       (!m_main_buffers && !m_shadow_buffers && !m_object_image));

                if (m_main_buffers || m_shadow_buffers || m_object_image)
                {
                        m_instance.device_wait_idle();

                        m_main_buffers.reset();
                        m_shadow_buffers.reset();
                        m_object_image.reset();
                }
        }

        void set_matrices()
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                //

                ASSERT(m_storage.scale_object() || !m_storage.object());

                if (m_storage.scale_object())
                {
                        mat4 matrix = m_main_matrix * m_storage.scale_object()->model_matrix();
                        mat4 scale_bias_shadow_matrix = m_scale_bias_shadow_matrix * m_storage.scale_object()->model_matrix();
                        mat4 shadow_matrix = m_shadow_matrix * m_storage.scale_object()->model_matrix();

                        m_triangles_shared_shader_memory.set_matrices(matrix, scale_bias_shadow_matrix);
                        m_shadow_shader_memory.set_matrix(shadow_matrix);
                        m_points_shader_memory.set_matrix(matrix);
                }
        }

        void before_render_pass_commands(VkCommandBuffer command_buffer) const
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                //

                m_object_image->clear_commands(command_buffer);
        }

        void draw_commands(VkCommandBuffer command_buffer) const
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                //

                if (!m_storage.object())
                {
                        return;
                }

                DrawInfo info;

                info.triangles_pipeline_layout = m_triangles_pipeline_layout;
                info.triangles_pipeline = m_triangles_pipeline;
                info.triangles_shared_set = m_triangles_shared_shader_memory.descriptor_set();
                info.triangles_shared_set_number = TRIANGLES_SHARED_SET_NUMBER;
                info.triangles_material_set_number = TRIANGLES_MATERIAL_SET_NUMBER;

                info.points_pipeline_layout = m_points_pipeline_layout;
                info.points_pipeline = m_points_pipeline;
                info.points_set = m_points_shader_memory.descriptor_set();
                info.points_set_number = POINTS_SET_NUMBER;

                info.lines_pipeline_layout = m_points_pipeline_layout;
                // Отличие линий от точек состоит только в типе объекта
                info.lines_pipeline = m_lines_pipeline;
                info.lines_set = m_points_shader_memory.descriptor_set();
                info.lines_set_number = POINTS_SET_NUMBER;

                m_storage.object()->draw_commands(command_buffer, info);
        }

        void draw_shadow_commands(VkCommandBuffer command_buffer) const
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                //

                if (!m_storage.object())
                {
                        return;
                }

                DrawShadowInfo info;

                info.triangles_pipeline_layout = m_shadow_pipeline_layout;
                info.triangles_pipeline = m_shadow_pipeline;
                info.triangles_set = m_shadow_shader_memory.descriptor_set();
                info.triangles_set_number = SHADOW_SET_NUMBER;

                m_storage.object()->draw_shadow_commands(command_buffer, info);
        }

        void create_command_buffers(bool wait_idle = true)
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                //

                ASSERT(m_main_buffers && m_shadow_buffers);

                if (wait_idle)
                {
                        m_instance.device_wait_idle();
                }

                m_main_buffers->create_command_buffers(
                        m_clear_color, std::bind(&Renderer::before_render_pass_commands, this, std::placeholders::_1),
                        std::bind(&Renderer::draw_commands, this, std::placeholders::_1));

                m_shadow_buffers->create_command_buffers(std::bind(&Renderer::draw_shadow_commands, this, std::placeholders::_1));
        }

        void delete_command_buffers()
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                //

                ASSERT(m_main_buffers && m_shadow_buffers);

                m_instance.device_wait_idle();

                m_main_buffers->delete_command_buffers();
                m_shadow_buffers->delete_command_buffers();
        }

public:
        Renderer(const vulkan::VulkanInstance& instance, int minimum_sample_count, bool sample_shading, bool sampler_anisotropy,
                 int max_frames_in_flight)
                : m_minimum_sample_count(minimum_sample_count),
                  m_sample_shading(sample_shading),
                  m_instance(instance),
                  m_shadow_available_semaphores(vulkan::create_semaphores(m_instance.device(), max_frames_in_flight)),
                  m_texture_sampler(impl::create_texture_sampler(m_instance.device(), sampler_anisotropy)),
                  m_shadow_sampler(impl::create_shadow_sampler(m_instance.device())),
                  //
                  m_triangles_material_descriptor_set_layout(vulkan::create_descriptor_set_layout(
                          m_instance.device(), impl::TrianglesMaterialMemory::descriptor_set_layout_bindings())),
                  //
                  m_triangles_shared_shader_memory(m_instance.device()),
                  m_shadow_shader_memory(m_instance.device()),
                  m_points_shader_memory(m_instance.device()),
                  //
                  m_triangles_vert(m_instance.device(), triangles_vert, "main"),
                  m_triangles_geom(m_instance.device(), triangles_geom, "main"),
                  m_triangles_frag(m_instance.device(), triangles_frag, "main"),
                  m_shadow_vert(m_instance.device(), shadow_vert, "main"),
                  m_shadow_frag(m_instance.device(), shadow_frag, "main"),
                  m_points_vert(m_instance.device(), points_vert, "main"),
                  m_points_frag(m_instance.device(), points_frag, "main"),
                  //
                  m_triangles_pipeline_layout(create_pipeline_layout(m_instance.device(),
                                                                     {TRIANGLES_SHARED_SET_NUMBER, TRIANGLES_MATERIAL_SET_NUMBER},
                                                                     {m_triangles_shared_shader_memory.descriptor_set_layout(),
                                                                      m_triangles_material_descriptor_set_layout})),
                  m_shadow_pipeline_layout(create_pipeline_layout(m_instance.device(), {SHADOW_SET_NUMBER},
                                                                  {m_shadow_shader_memory.descriptor_set_layout()})),
                  m_points_pipeline_layout(create_pipeline_layout(m_instance.device(), {POINTS_SET_NUMBER},
                                                                  {m_points_shader_memory.descriptor_set_layout()}))
        {
        }

        ~Renderer() override
        {
                ASSERT(m_thread_id == std::this_thread::get_id());

                try
                {
                        m_instance.device_wait_idle();
                }
                catch (std::exception& e)
                {
                        LOG(std::string("Device wait idle exception in the Vulkan renderer destructor: ") + e.what());
                }
                catch (...)
                {
                        LOG("Device wait idle unknown exception in the Vulkan renderer destructor");
                }
        }
};
}

mat4 VulkanRenderer::ortho(double left, double right, double bottom, double top, double near, double far)
{
        return ortho_vulkan<double>(left, right, bottom, top, near, far);
}

std::vector<std::string> VulkanRenderer::instance_extensions()
{
        return string_vector(INSTANCE_EXTENSIONS);
}
std::vector<std::string> VulkanRenderer::device_extensions()
{
        return string_vector(DEVICE_EXTENSIONS);
}
std::vector<vulkan::PhysicalDeviceFeatures> VulkanRenderer::required_device_features()
{
        return REQUIRED_DEVICE_FEATURES;
}

std::unique_ptr<VulkanRenderer> create_vulkan_renderer(const vulkan::VulkanInstance& instance, int minimum_sample_count,
                                                       bool sample_shading, bool sampler_anisotropy, int max_frames_in_flight)
{
        return std::make_unique<Renderer>(instance, minimum_sample_count, sample_shading, sampler_anisotropy,
                                          max_frames_in_flight);
}
