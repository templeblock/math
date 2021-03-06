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

#include "show.h"

#include "camera.h"

#include "com/conversion.h"
#include "com/error.h"
#include "com/frequency.h"
#include "com/log.h"
#include "com/matrix.h"
#include "com/matrix_alg.h"
#include "com/merge.h"
#include "com/print.h"
#include "com/string/vector.h"
#include "com/thread.h"
#include "com/type/limit.h"
#include "graphics/vulkan/create.h"
#include "graphics/vulkan/error.h"
#include "graphics/vulkan/instance.h"
#include "numerical/linear.h"
#include "show/canvases/opengl/canvas.h"
#include "show/canvases/vulkan/canvas.h"
#include "show/event_queue.h"
#include "show/renderers/opengl/renderer.h"
#include "show/renderers/vulkan/renderer.h"
#include "window/manage.h"
#include "window/opengl/window.h"
#include "window/vulkan/window.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <type_traits>
#include <vector>

// 2 - double buffering, 3 - triple buffering
constexpr int VULKAN_PREFERRED_IMAGE_COUNT = 2;
constexpr int VULKAN_MAX_FRAMES_IN_FLIGHT = 1;
// Шейдеры пишут результат в цветовом пространстве RGB, поэтому _SRGB (для результата в sRGB нужен _UNORM).
constexpr VkSurfaceFormatKHR VULKAN_SURFACE_FORMAT = {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};

constexpr int OPENGL_MINIMUM_SAMPLE_COUNT = 4;

constexpr int VULKAN_MINIMUM_SAMPLE_COUNT = 4;
constexpr bool VULKAN_SAMPLE_SHADING = true; // supersampling
constexpr bool VULKAN_SAMPLER_ANISOTROPY = true; // anisotropic filtering

constexpr double ZOOM_BASE = 1.1;
constexpr double ZOOM_EXP_MIN = -50;
constexpr double ZOOM_EXP_MAX = 100;

constexpr double FPS_TEXT_SIZE_IN_POINTS = 9.0;
constexpr double FPS_TEXT_STEP_Y_IN_POINTS = 1.3 * FPS_TEXT_SIZE_IN_POINTS;
constexpr double FPS_TEXT_X_IN_POINTS = 5;
constexpr double FPS_TEXT_Y_IN_POINTS = FPS_TEXT_STEP_Y_IN_POINTS;

constexpr const char FPS_TEXT[] = "FPS: ";
constexpr double FPS_INTERVAL_LENGTH = 1;
constexpr int FPS_SAMPLE_COUNT = 10;

constexpr std::chrono::milliseconds IDLE_MODE_FRAME_DURATION(100);

// Это только для начального значения, а далее оно устанавливается командой set_vertical_sync
constexpr vulkan::PresentMode VULKAN_INIT_PRESENT_MODE = vulkan::PresentMode::PreferFast;

namespace
{
#if 0
int object_under_mouse(int mouse_x, int mouse_y, int window_height, const TextureR32I& tex)
{
        int x = mouse_x;
        int y = window_height - mouse_y - 1;
        std::array<GLint, 1> v;
        tex.get_texture_sub_image(x, y, 1, 1, &v);
        return v[0];
}
#endif

void sleep(std::chrono::steady_clock::time_point& last_frame_time)
{
        std::this_thread::sleep_until(last_frame_time + IDLE_MODE_FRAME_DURATION);
        last_frame_time = std::chrono::steady_clock::now();
}

void make_fullscreen(bool fullscreen, WindowID window, WindowID parent)
{
        if (fullscreen)
        {
                make_window_fullscreen(window);
        }
        else
        {
                move_window_to_parent(window, parent);
        }
        set_focus(window);
}

// Объекты для std::unique_ptr создаются и удаляются в отдельном потоке. Поток этого
// класса не должен владеть такими объектами. Этот класс нужен, чтобы в явном виде
// не работать с std::unique_ptr.
template <typename T>
class UniquePointerView
{
        const std::unique_ptr<T>* m_pointer = nullptr;

public:
        void set(const std::unique_ptr<T>& pointer)
        {
                m_pointer = &pointer;
        }
        T* operator->() const
        {
                return m_pointer->get();
        }
        operator bool() const
        {
                return static_cast<bool>(*m_pointer);
        }
};

// Матрица для рисования на плоскости окна, точка (0, 0) слева вверху
template <typename Renderer>
mat4 ortho_matrix_for_2d_rendering(int width, int height)
{
        static_assert(std::is_same_v<Renderer, OpenGLRenderer> || std::is_same_v<Renderer, VulkanRenderer>);

        double left = 0;
        double right = width;
        double bottom = height;
        double top = 0;
        double near = 1;
        double far = -1;
        return Renderer::ortho(left, right, bottom, top, near, far) * scale<double>(1, 1, 0);
}

template <GraphicsAndComputeAPI API>
class ShowObject final : public EventQueue, public WindowEvent
{
        static_assert(API == GraphicsAndComputeAPI::Vulkan || API == GraphicsAndComputeAPI::OpenGL);
        using Renderer = std::conditional_t<API == GraphicsAndComputeAPI::Vulkan, VulkanRenderer, OpenGLRenderer>;
        using Window = std::conditional_t<API == GraphicsAndComputeAPI::Vulkan, VulkanWindow, OpenGLWindow>;
        using Canvas = std::conditional_t<API == GraphicsAndComputeAPI::Vulkan, VulkanCanvas, OpenGLCanvas>;

        // Камера и тени рассчитаны на размер объекта 2 и на положение в точке (0, 0, 0).
        static constexpr double OBJECT_SIZE = 2;
        static constexpr vec3 OBJECT_POSITION = vec3(0);

        //

        ShowCallback* const m_callback;
        const WindowID m_parent_window;
        const double m_parent_window_ppi;
        std::thread m_thread;
        std::atomic_bool m_stop{false};

        //

        const int m_text_size = points_to_pixels(FPS_TEXT_SIZE_IN_POINTS, m_parent_window_ppi);
        const int m_text_step_y = points_to_pixels(FPS_TEXT_STEP_Y_IN_POINTS, m_parent_window_ppi);
        const int m_text_x = points_to_pixels(FPS_TEXT_X_IN_POINTS, m_parent_window_ppi);
        const int m_text_y = points_to_pixels(FPS_TEXT_Y_IN_POINTS, m_parent_window_ppi);

        //

        Camera m_camera;

        //

        Frequency m_fps{FPS_INTERVAL_LENGTH, FPS_SAMPLE_COUNT};
        std::vector<std::string> m_fps_text{FPS_TEXT, ""};

        //

        UniquePointerView<Window> m_window;
        UniquePointerView<Renderer> m_renderer;
        UniquePointerView<Canvas> m_canvas;

        //

        int m_window_width = limits<int>::lowest();
        int m_window_height = limits<int>::lowest();

        int m_draw_width = limits<int>::lowest();
        int m_draw_height = limits<int>::lowest();

        int m_mouse_x = 0;
        int m_mouse_y = 0;

        int m_mouse_pressed_x = 0;
        int m_mouse_pressed_y = 0;
        bool m_mouse_pressed = false;
        MouseButton m_mouse_pressed_button;

        vec2 m_window_center = vec2(0, 0);
        double m_zoom_exponent = 0;
        double m_default_ortho_scale = 1;

        bool m_fullscreen_active = false;

        //

        std::function<void(bool)> m_function_set_vertical_sync;

        //

        void direct_add_object(const std::shared_ptr<const Obj<3>>& obj_ptr, int id, int scale_id) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                if (!obj_ptr)
                {
                        error("Null object received");
                }
                m_renderer->object_add(obj_ptr.get(), OBJECT_SIZE, OBJECT_POSITION, id, scale_id);
                m_callback->object_loaded(id);
        }

        void direct_delete_object(int id) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                m_renderer->object_delete(id);
        }

        void direct_show_object(int id) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                m_renderer->object_show(id);
        }

        void direct_delete_all_objects() override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                m_renderer->object_delete_all();

                reset_view_handler();
        }

        void direct_reset_view() override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                reset_view_handler();
        }

        void direct_set_ambient(double v) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                Color light = Color(v);
                m_renderer->set_light_a(light);
        }

        void direct_set_diffuse(double v) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                Color light = Color(v);
                m_renderer->set_light_d(light);
        }

        void direct_set_specular(double v) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                Color light = Color(v);
                m_renderer->set_light_s(light);
        }

        void direct_set_background_color(const Color& c) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                m_renderer->set_background_color(c);

                bool background_is_dark = c.luminance() <= 0.5;
                m_canvas->set_text_color(background_is_dark ? Color(1) : Color(0));
        }

        void direct_set_default_color(const Color& c) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                m_renderer->set_default_color(c);
        }

        void direct_set_wireframe_color(const Color& c) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                m_renderer->set_wireframe_color(c);
        }

        void direct_set_default_ns(double ns) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                m_renderer->set_default_ns(ns);
        }

        void direct_show_smooth(bool v) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                m_renderer->set_show_smooth(v);
        }

        void direct_show_wireframe(bool v) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                m_renderer->set_show_wireframe(v);
        }

        void direct_show_shadow(bool v) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                m_renderer->set_show_shadow(v);
        }

        void direct_show_fog(bool v) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                m_renderer->set_show_fog(v);
        }

        void direct_show_materials(bool v) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                m_renderer->set_show_materials(v);
        }

        void direct_show_fps(bool v) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                m_canvas->set_text_active(v);
        }

        void direct_show_pencil_sketch(bool v) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                m_canvas->set_pencil_sketch_active(v);
        }

        void direct_show_dft(bool v) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                if (m_canvas->dft_active() != v)
                {
                        m_canvas->set_dft_active(v);
                        window_resize_handler();
                }
        }

        void direct_set_dft_brightness(double v) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                m_canvas->set_dft_brightness(v);
        }

        void direct_set_dft_background_color(const Color& c) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                m_canvas->set_dft_background_color(c);
        }

        void direct_set_dft_color(const Color& c) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                m_canvas->set_dft_color(c);
        }

        void direct_show_convex_hull_2d(bool v) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                m_canvas->set_convex_hull_active(v);
        }

        void direct_show_optical_flow(bool v) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                m_canvas->set_optical_flow_active(v);
        }

        void direct_parent_resized() override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                if (!m_fullscreen_active)
                {
                        set_size_to_parent(m_window->system_handle(), m_parent_window);
                }
        }

        void direct_mouse_wheel(double delta) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                // Для полноэкранного режима обрабатывается в функции window_mouse_wheel
                if (!m_fullscreen_active)
                {
                        mouse_wheel_handler(delta);
                }
        }

        void direct_toggle_fullscreen() override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                m_fullscreen_active = !m_fullscreen_active;
                make_fullscreen(m_fullscreen_active, m_window->system_handle(), m_parent_window);
        }

        void direct_set_vertical_sync(bool v) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                ASSERT(m_function_set_vertical_sync);

                m_function_set_vertical_sync(v);
        }

        void direct_set_shadow_zoom(double v) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                m_renderer->set_shadow_zoom(v);
        }

        //

        void camera_information(vec3* camera_up, vec3* camera_direction, vec3* view_center, double* view_width, int* paint_width,
                                int* paint_height) const override
        {
                ASSERT(std::this_thread::get_id() != m_thread.get_id());

                m_camera.camera_information(camera_up, camera_direction, view_center, view_width, paint_width, paint_height);
        }

        vec3 light_direction() const override
        {
                ASSERT(std::this_thread::get_id() != m_thread.get_id());

                return m_camera.light_direction();
        }

        double object_size() const override
        {
                ASSERT(std::this_thread::get_id() != m_thread.get_id());

                return OBJECT_SIZE;
        }

        vec3 object_position() const override
        {
                ASSERT(std::this_thread::get_id() != m_thread.get_id());

                return OBJECT_POSITION;
        }

        //

        void window_keyboard_pressed(KeyboardButton button) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                switch (button)
                {
                case KeyboardButton::F11:
                        toggle_fullscreen();
                        break;
                case KeyboardButton::Escape:
                        if (m_fullscreen_active)
                        {
                                toggle_fullscreen();
                        }
                        break;
                }
        }

        void window_mouse_pressed(MouseButton button) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                if (!m_mouse_pressed && m_mouse_x < m_draw_width && m_mouse_y < m_draw_height)
                {
                        m_mouse_pressed = true;
                        m_mouse_pressed_button = button;
                        m_mouse_pressed_x = m_mouse_x;
                        m_mouse_pressed_y = m_mouse_y;
                }
        }

        void window_mouse_released(MouseButton button) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                if (m_mouse_pressed && button == m_mouse_pressed_button)
                {
                        m_mouse_pressed = false;
                }
        }

        void window_mouse_moved(int x, int y) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                m_mouse_x = x;
                m_mouse_y = y;

                mouse_move_handler();
        }

        void window_mouse_wheel(int delta) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                // Для режима встроенного окна обработка колеса мыши происходит
                // в функции direct_mouse_wheel, так как на Винде не приходит
                // это сообщение для дочернего окна.
                if (m_fullscreen_active)
                {
                        mouse_wheel_handler(delta);
                }
        }

        void window_resized(int width, int height) override
        {
                ASSERT(std::this_thread::get_id() == m_thread.get_id());

                if (width <= 0 || height <= 0)
                {
                        error("Window resize error: width = " + to_string(width) + ", height = " + to_string(height));
                }

                m_window_width = width;
                m_window_height = height;

                window_resize_handler();
        }

        //

        void mouse_wheel_handler(int delta);
        void mouse_move_handler();
        void reset_view_handler();
        void window_resize_handler();

        //

        void pull_and_dispatch_all_events();
        void init_window_and_view();
        void compute_matrices();

        //

        void loop();
        void loop_thread();

public:
        ShowObject(const ShowCreateInfo& info) try : m_callback(info.callback.value()),
                                                     m_parent_window(info.parent_window.value()),
                                                     m_parent_window_ppi(info.parent_window_ppi.value())
        {
                ASSERT(m_callback);
                ASSERT(m_parent_window_ppi > 0);

                set_ambient(info.ambient.value());
                set_diffuse(info.diffuse.value());
                set_specular(info.specular.value());
                set_background_color(info.background_color.value());
                set_default_color(info.default_color.value());
                set_wireframe_color(info.wireframe_color.value());
                set_default_ns(info.default_ns.value());
                show_smooth(info.with_smooth.value());
                show_wireframe(info.with_wireframe.value());
                show_shadow(info.with_shadow.value());
                show_fog(info.with_fog.value());
                show_fps(info.with_fps.value());
                show_pencil_sketch(info.with_pencil_sketch.value());
                show_dft(info.with_dft.value());
                set_dft_brightness(info.dft_brightness.value());
                set_dft_background_color(info.dft_background_color.value());
                set_dft_color(info.dft_color.value());
                show_materials(info.with_materials.value());
                show_convex_hull_2d(info.with_convex_hull.value());
                show_optical_flow(info.with_optical_flow.value());
                set_vertical_sync(info.vertical_sync.value());
                set_shadow_zoom(info.shadow_zoom.value());

                m_thread = std::thread(&ShowObject::loop_thread, this);
        }
        catch (std::bad_optional_access&)
        {
                error("Show create information is not complete");
        }

        ~ShowObject() override
        {
                if (m_thread.joinable())
                {
                        m_stop = true;
                        m_thread.join();
                }
        }

        ShowObject(const ShowObject&) = delete;
        ShowObject(ShowObject&&) = delete;
        ShowObject& operator=(const ShowObject&) = delete;
        ShowObject& operator=(ShowObject&&) = delete;
};

template <GraphicsAndComputeAPI API>
void ShowObject<API>::mouse_wheel_handler(int delta)
{
        if (!(m_mouse_x < m_draw_width && m_mouse_y < m_draw_height))
        {
                return;
        }
        if ((delta < 0 && m_zoom_exponent <= ZOOM_EXP_MIN) || (delta > 0 && m_zoom_exponent >= ZOOM_EXP_MAX) || delta == 0)
        {
                return;
        }

        m_zoom_exponent += delta;

        vec2 mouse_local(m_mouse_x - m_draw_width * 0.5, m_draw_height * 0.5 - m_mouse_y);
        vec2 mouse_global(mouse_local + m_window_center);
        // Формула
        //   new_center = old_center + (mouse_global * zoom_r - mouse_global)
        //   -> center = center + mouse_global * zoom_r - mouse_global
        //   -> center += mouse_global * (zoom_r - 1)
        m_window_center += mouse_global * (std::pow(ZOOM_BASE, delta) - 1);

        //

        compute_matrices();
}

template <GraphicsAndComputeAPI API>
void ShowObject<API>::mouse_move_handler()
{
        if (!m_mouse_pressed)
        {
                return;
        }

        int delta_x = m_mouse_x - m_mouse_pressed_x;
        int delta_y = m_mouse_y - m_mouse_pressed_y;

        if (delta_x == 0 && delta_y == 0)
        {
                return;
        }

        m_mouse_pressed_x = m_mouse_x;
        m_mouse_pressed_y = m_mouse_y;

        switch (m_mouse_pressed_button)
        {
        case MouseButton::Right:
                m_camera.rotate(-delta_x, -delta_y);
                break;
        case MouseButton::Left:
                m_window_center -= vec2(delta_x, -delta_y);
                break;
        }

        //

        compute_matrices();
}

template <GraphicsAndComputeAPI API>
void ShowObject<API>::reset_view_handler()
{
        m_zoom_exponent = 0;
        m_window_center = vec2(0, 0);
        m_camera.set(vec3(1, 0, 0), vec3(0, 1, 0));

        if (m_draw_width > 0 && m_draw_height > 0)
        {
                m_default_ortho_scale = 2.0 / std::min(m_draw_width, m_draw_height);
        }
        else
        {
                m_default_ortho_scale = 1;
        }

        //

        compute_matrices();
}

template <GraphicsAndComputeAPI API>
void ShowObject<API>::window_resize_handler()
{
        if (m_window_width <= 0 || m_window_height <= 0)
        {
                // Вызов не из обработчика измерения окна (там есть проверка),
                // а вызов из обработчика включения-выключения ДПФ в то время,
                // когда окно ещё не готово.
                return;
        }

        m_draw_width = m_canvas->dft_active() ? m_window_width / 2 : m_window_width;
        m_draw_height = m_window_height;

        m_renderer->set_size(m_draw_width, m_draw_height);

        if constexpr (API == GraphicsAndComputeAPI::OpenGL)
        {
                int dft_dst_x = (m_window_width & 1) ? (m_draw_width + 1) : m_draw_width;
                int dft_dst_y = 0;

                m_canvas->create_objects(m_window_width, m_window_height,
                                         ortho_matrix_for_2d_rendering<OpenGLRenderer>(m_window_width, m_window_height),
                                         m_renderer->color_buffer(), m_renderer->color_buffer_is_srgb(), m_renderer->objects(),
                                         m_draw_width, m_draw_height, dft_dst_x, dft_dst_y, m_renderer->frame_buffer_is_srgb());
        }

        //

        compute_matrices();
}

template <GraphicsAndComputeAPI API>
void ShowObject<API>::compute_matrices()
{
        vec3 camera_up, camera_direction, light_up, light_direction;

        m_camera.get(&camera_up, &camera_direction, &light_up, &light_direction);

        mat4 shadow_projection_matrix = Renderer::ortho(-1, 1, -1, 1, 1, -1);
        mat4 shadow_view_matrix = look_at(vec3(0, 0, 0), light_direction, light_up);

        double ortho_scale = std::pow(ZOOM_BASE, -m_zoom_exponent) * m_default_ortho_scale;
        double left = ortho_scale * (m_window_center[0] - 0.5 * m_draw_width);
        double right = ortho_scale * (m_window_center[0] + 0.5 * m_draw_width);
        double bottom = ortho_scale * (m_window_center[1] - 0.5 * m_draw_height);
        double top = ortho_scale * (m_window_center[1] + 0.5 * m_draw_height);
        double near = 1;
        double far = -1;
        mat4 projection_matrix = Renderer::ortho(left, right, bottom, top, near, far);
        mat4 view_matrix = look_at<double>(vec3(0, 0, 0), camera_direction, camera_up);

        m_renderer->set_matrices(shadow_projection_matrix * shadow_view_matrix, projection_matrix * view_matrix);

        m_renderer->set_light_direction(light_direction);
        m_renderer->set_camera_direction(camera_direction);

        vec4 screen_center((right + left) * 0.5, (top + bottom) * 0.5, (far + near) * 0.5, 1.0);
        vec4 view_center = numerical::inverse(view_matrix) * screen_center;
        m_camera.set_view_center_and_width(vec3(view_center[0], view_center[1], view_center[2]), right - left, m_draw_width,
                                           m_draw_height);
}

template <GraphicsAndComputeAPI API>
void ShowObject<API>::pull_and_dispatch_all_events()
{
        // Вначале команды, потом сообщения окна, потому что в командах
        // могут быть действия с окном, а в событиях окна нет комманд
        this->pull_and_dispatch_events();
        m_window->pull_and_dispath_events();
}

template <GraphicsAndComputeAPI API>
void ShowObject<API>::init_window_and_view()
{
        move_window_to_parent(m_window->system_handle(), m_parent_window);

        for (int i = 1; m_window_width != m_window->width() && m_window_height != m_window->height(); ++i)
        {
                if (i > 10)
                {
                        error("Failed to receive the resize window event for the window size (" + to_string(m_window->width()) +
                              ", " + to_string(m_window->height()) + ")");
                }
                pull_and_dispatch_all_events();
        }

        if (m_draw_width <= 0 || m_draw_height <= 0)
        {
                error("Draw size error (" + to_string(m_draw_width) + ", " + to_string(m_draw_height) + ")");
        }

        reset_view_handler();
}

//

bool render_opengl(OpenGLWindow& window, OpenGLRenderer& renderer, OpenGLCanvas& canvas, int text_step_y, int text_x, int text_y,
                   const std::vector<std::string>& text)
{
        // Параметр true означает рисование в цветной буфер,
        // параметр false означает рисование в буфер экрана.
        // Если возвращает false, то нет объекта для рисования.
        bool object_rendered = renderer.draw(canvas.pencil_sketch_active());

        canvas.draw();

        canvas.draw_text(text_step_y, text_x, text_y, text);

        window.display();

        return object_rendered;
}

template <>
void ShowObject<GraphicsAndComputeAPI::OpenGL>::loop()
{
        ASSERT(std::this_thread::get_id() == m_thread.get_id());

        std::unique_ptr<OpenGLWindow> window = create_opengl_window(OPENGL_MINIMUM_SAMPLE_COUNT, this);
        std::unique_ptr<OpenGLRenderer> renderer = create_opengl_renderer();
        std::unique_ptr<OpenGLCanvas> canvas = create_opengl_canvas(m_text_size, m_parent_window_ppi);

        //

        m_window.set(window);
        m_renderer.set(renderer);
        m_canvas.set(canvas);

        m_function_set_vertical_sync = [&](bool v) { window->set_vertical_sync_enabled(v); };

        //

        init_window_and_view();

        //

        std::chrono::steady_clock::time_point last_frame_time = std::chrono::steady_clock::now();
        while (!m_stop)
        {
                pull_and_dispatch_all_events();

                m_fps_text[1] = to_string(std::lround(m_fps.calculate()));

                if (!render_opengl(*window, *renderer, *canvas, m_text_step_y, m_text_x, m_text_y, m_fps_text))
                {
                        sleep(last_frame_time);
                }
        }
}

//

void create_swapchain(const vulkan::VulkanInstance& instance, VulkanRenderer* renderer, VulkanCanvas* canvas,
                      std::unique_ptr<vulkan::Swapchain>* swapchain, vulkan::PresentMode preferred_present_mode)
{
        instance.device_wait_idle();

        canvas->delete_buffers();
        renderer->delete_buffers();

        swapchain->reset();
        *swapchain = std::make_unique<vulkan::Swapchain>(
                instance.create_swapchain(VULKAN_SURFACE_FORMAT, VULKAN_PREFERRED_IMAGE_COUNT, preferred_present_mode));

        mat4 m = ortho_matrix_for_2d_rendering<VulkanRenderer>((*swapchain)->width(), (*swapchain)->height());

        renderer->create_buffers(swapchain->get());
        canvas->create_buffers(swapchain->get(), m);
}

enum class VulkanResult
{
        CreateSwapchain,
        NoObject,
        ObjectRendered
};

VulkanResult render_vulkan(VkSwapchainKHR swapchain, VkQueue presentation_queue, VkQueue graphics_queue, VkDevice device,
                           VkFence current_frame_fence, VkSemaphore image_available_semaphore,
                           VkSemaphore renderer_finished_semaphore, VkSemaphore canvas_finished_semaphore, unsigned current_frame,
                           VulkanRenderer& renderer, VulkanCanvas& canvas, int text_step_y, int text_x, int text_y,
                           const std::vector<std::string>& text, bool show_text)
{
        VkResult result;

        result = vkWaitForFences(device, 1, &current_frame_fence, VK_TRUE, limits<uint64_t>::max());
        if (result != VK_SUCCESS)
        {
                vulkan::vulkan_function_error("vkWaitForFences", result);
        }
        result = vkResetFences(device, 1, &current_frame_fence);
        if (result != VK_SUCCESS)
        {
                vulkan::vulkan_function_error("vkResetFences", result);
        }

        //

        uint32_t image_index;
        result = vkAcquireNextImageKHR(device, swapchain, limits<uint64_t>::max(), image_available_semaphore, VK_NULL_HANDLE,
                                       &image_index);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
                return VulkanResult::CreateSwapchain;
        }
        else if (result == VK_SUBOPTIMAL_KHR)
        {
        }
        else if (result != VK_SUCCESS)
        {
                vulkan::vulkan_function_error("vkAcquireNextImageKHR", result);
        }

        //

        bool object_rendered;
        std::array<VkSemaphore, 1> finished_semaphores;

        if (show_text)
        {
                object_rendered = renderer.draw(VK_NULL_HANDLE, graphics_queue, image_available_semaphore,
                                                renderer_finished_semaphore, image_index, current_frame);

                canvas.draw_text(current_frame_fence, graphics_queue, renderer_finished_semaphore, canvas_finished_semaphore,
                                 image_index, text_step_y, text_x, text_y, text);

                finished_semaphores = {canvas_finished_semaphore};
        }

        else
        {
                object_rendered = renderer.draw(current_frame_fence, graphics_queue, image_available_semaphore,
                                                renderer_finished_semaphore, image_index, current_frame);

                finished_semaphores = {renderer_finished_semaphore};
        }

        //

        std::array<VkSwapchainKHR, 1> swapchains = {swapchain};
        std::array<uint32_t, 1> image_indices = {image_index};

        VkPresentInfoKHR present_info = {};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = finished_semaphores.size();
        present_info.pWaitSemaphores = finished_semaphores.data();
        present_info.swapchainCount = swapchains.size();
        present_info.pSwapchains = swapchains.data();
        present_info.pImageIndices = image_indices.data();
        // present_info.pResults = nullptr;

        result = vkQueuePresentKHR(presentation_queue, &present_info);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
                return VulkanResult::CreateSwapchain;
        }
        else if (result != VK_SUCCESS)
        {
                vulkan::vulkan_function_error("vkQueuePresentKHR", result);
        }

        return object_rendered ? VulkanResult::ObjectRendered : VulkanResult::NoObject;
}

std::vector<vulkan::PhysicalDeviceFeatures> device_features_sample_shading(int sample_count, bool sample_shading)
{
        if (sample_count > 1 && sample_shading)
        {
                return {vulkan::PhysicalDeviceFeatures::SampleRateShading};
        }
        else
        {
                return {};
        }
}
std::vector<vulkan::PhysicalDeviceFeatures> device_features_sampler_anisotropy(bool sampler_anisotropy)
{
        if (sampler_anisotropy)
        {
                return {vulkan::PhysicalDeviceFeatures::SamplerAnisotropy};
        }
        else
        {
                return {};
        }
}

template <>
void ShowObject<GraphicsAndComputeAPI::Vulkan>::loop()
{
        ASSERT(std::this_thread::get_id() == m_thread.get_id());

        static_assert(VULKAN_MAX_FRAMES_IN_FLIGHT == 1);

        std::unique_ptr<VulkanWindow> window = create_vulkan_window(this);

        vulkan::VulkanInstance instance(
                merge<std::string>(VulkanRenderer::instance_extensions(), VulkanWindow::instance_extensions()),
                VulkanRenderer::device_extensions(),
                merge<vulkan::PhysicalDeviceFeatures>(
                        VulkanRenderer::required_device_features(),
                        device_features_sample_shading(VULKAN_MINIMUM_SAMPLE_COUNT, VULKAN_SAMPLE_SHADING),
                        device_features_sampler_anisotropy(VULKAN_SAMPLER_ANISOTROPY)),
                {} /*optional_features*/, [w = window.get()](VkInstance i) { return w->create_surface(i); });

        std::vector<vulkan::Fence> in_flight_fences =
                vulkan::create_fences(instance.device(), VULKAN_MAX_FRAMES_IN_FLIGHT, true /*signaled_state*/);

        std::vector<vulkan::Semaphore> image_available_semaphores =
                vulkan::create_semaphores(instance.device(), VULKAN_MAX_FRAMES_IN_FLIGHT);

        std::vector<vulkan::Semaphore> renderer_finished_semaphores =
                vulkan::create_semaphores(instance.device(), VULKAN_MAX_FRAMES_IN_FLIGHT);

        std::vector<vulkan::Semaphore> canvas_finished_semaphores =
                vulkan::create_semaphores(instance.device(), VULKAN_MAX_FRAMES_IN_FLIGHT);

        //

        // В последовательности swapchain, а затем renderer и canvas,
        // так как буферы renderer и canvas могут зависеть от swapchain

        std::unique_ptr<vulkan::Swapchain> swapchain;

        std::unique_ptr<VulkanRenderer> renderer =
                create_vulkan_renderer(instance, VULKAN_MINIMUM_SAMPLE_COUNT, VULKAN_SAMPLE_SHADING, VULKAN_SAMPLER_ANISOTROPY,
                                       VULKAN_MAX_FRAMES_IN_FLIGHT);

        std::unique_ptr<VulkanCanvas> canvas = create_vulkan_canvas(instance, m_text_size);

        //

        vulkan::PresentMode present_mode = VULKAN_INIT_PRESENT_MODE;

        create_swapchain(instance, renderer.get(), canvas.get(), &swapchain, present_mode);

        //

        m_window.set(window);
        m_renderer.set(renderer);
        m_canvas.set(canvas);

        m_function_set_vertical_sync = [&](bool v) {
                if (v && present_mode != vulkan::PresentMode::PreferSync)
                {
                        present_mode = vulkan::PresentMode::PreferSync;
                        create_swapchain(instance, renderer.get(), canvas.get(), &swapchain, present_mode);
                        return;
                }
                if (!v && present_mode != vulkan::PresentMode::PreferFast)
                {
                        present_mode = vulkan::PresentMode::PreferFast;
                        create_swapchain(instance, renderer.get(), canvas.get(), &swapchain, present_mode);
                        return;
                }
        };

        //

        init_window_and_view();

        //

        std::chrono::steady_clock::time_point last_frame_time = std::chrono::steady_clock::now();
        for (unsigned frame = 0; !m_stop; frame = (frame + 1) % VULKAN_MAX_FRAMES_IN_FLIGHT)
        {
                pull_and_dispatch_all_events();

                m_fps_text[1] = to_string(std::lround(m_fps.calculate()));

                switch (render_vulkan(swapchain->swapchain(), instance.presentation_queue(), instance.graphics_queue(),
                                      instance.device(), in_flight_fences[frame], image_available_semaphores[frame],
                                      renderer_finished_semaphores[frame], canvas_finished_semaphores[frame], frame, *renderer,
                                      *canvas, m_text_step_y, m_text_x, m_text_y, m_fps_text, canvas->text_active()))
                {
                case VulkanResult::NoObject:
                        sleep(last_frame_time);
                        break;
                case VulkanResult::ObjectRendered:
                        break;
                case VulkanResult::CreateSwapchain:
                        create_swapchain(instance, renderer.get(), canvas.get(), &swapchain, present_mode);
                        break;
                }
        }
}

template <GraphicsAndComputeAPI API>
void ShowObject<API>::loop_thread()
{
        ASSERT(std::this_thread::get_id() == m_thread.get_id());

        try
        {
                loop();

                if (!m_stop)
                {
                        m_callback->message_error_fatal("Thread ended.");
                }
        }
        catch (ErrorSourceException& e)
        {
                m_callback->message_error_source(e.msg(), e.src());
        }
        catch (std::exception& e)
        {
                m_callback->message_error_fatal(e.what());
        }
        catch (...)
        {
                m_callback->message_error_fatal("Unknown Error. Thread ended.");
        }
}
}

std::unique_ptr<Show> create_show(GraphicsAndComputeAPI api, const ShowCreateInfo& info)
{
        switch (api)
        {
        case GraphicsAndComputeAPI::Vulkan:
                return std::make_unique<ShowObject<GraphicsAndComputeAPI::Vulkan>>(info);
        case GraphicsAndComputeAPI::OpenGL:
                return std::make_unique<ShowObject<GraphicsAndComputeAPI::OpenGL>>(info);
        }
        error_fatal("Unknown graphics and compute API for show creation");
}
