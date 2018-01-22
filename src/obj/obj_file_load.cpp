/*
Copyright (C) 2017, 2018 Topological Manifold

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

#include "obj_file_load.h"

#include "obj_alg.h"

#include "com/error.h"
#include "com/file/file_read.h"
#include "com/file/file_sys.h"
#include "com/log.h"
#include "com/math.h"
#include "com/print.h"
#include "com/string/ascii.h"
#include "com/string/str.h"
#include "com/thread.h"
#include "com/time.h"

#include <SFML/Graphics/Image.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <thread>

using atomic_counter = AtomicCounter<int>;

constexpr bool ATOMIC_COUNTER_LOCK_FREE = atomic_counter::is_always_lock_free;
constexpr unsigned MAX_FACES_PER_LINE = 5;

constexpr const char OBJ_v[] = "v";
constexpr const char OBJ_vt[] = "vt";
constexpr const char OBJ_vn[] = "vn";
constexpr const char OBJ_f[] = "f";
constexpr const char OBJ_usemtl[] = "usemtl";
constexpr const char OBJ_mtllib[] = "mtllib";

constexpr const char MTL_newmtl[] = "newmtl";
constexpr const char MTL_Ka[] = "Ka";
constexpr const char MTL_Kd[] = "Kd";
constexpr const char MTL_Ks[] = "Ks";
constexpr const char MTL_Ns[] = "Ns";
constexpr const char MTL_map_Ka[] = "map_Ka";
constexpr const char MTL_map_Kd[] = "map_Kd";
constexpr const char MTL_map_Ks[] = "map_Ks";

constexpr bool is_number_sign(char c)
{
        return c == '#';
}
constexpr bool is_hyphen_minus(char c)
{
        return c == '-';
}
constexpr bool is_solidus(char c)
{
        return c == '/';
}

constexpr bool str_equal(const char* s1, const char* s2)
{
        while (*s1 == *s2 && *s1)
        {
                ++s1;
                ++s2;
        }
        return *s1 == *s2;
}

static_assert(str_equal("ab", "ab") && str_equal("", "") && !str_equal("", "ab") && !str_equal("ab", "") &&
              !str_equal("ab", "ac") && !str_equal("ba", "ca") && !str_equal("a", "xyz"));

namespace
{
template <typename T>
void read(const std::string& line, size_t size, const T& op, size_t* i)
{
        while (*i < size && op(line[*i]))
        {
                ++(*i);
        }
}

template <typename T>
std::string get_string_list(const std::map<std::string, T>& m)
{
        std::string names;
        for (const auto& s : m)
        {
                if (names.size() > 0)
                {
                        names += ", " + s.first;
                }
                else
                {
                        names += s.first;
                }
        }
        return names;
}

bool check_range(float v, float min, float max)
{
        return v >= min && v <= max;
}
bool check_range(const vec3f& v, float min, float max)
{
        return v[0] >= min && v[0] <= max && v[1] >= min && v[1] <= max && v[2] >= min && v[2] <= max;
}

void find_line_begin(const std::string& s, std::vector<size_t>* line_begin)
{
        size_t size = s.size();

        size_t cnt = 0;
        for (size_t i = 0; i < size; ++i)
        {
                if (s[i] == '\n')
                {
                        ++cnt;
                }
        }

        line_begin->clear();
        line_begin->reserve(cnt);

        size_t b = 0;
        for (size_t i = 0; i < size; ++i)
        {
                if (s[i] == '\n')
                {
                        line_begin->push_back(b);
                        b = i + 1;
                }
        }
}

void read_file_lines(const std::string& file_name, std::string* file_str, std::vector<size_t>* line_begin)
{
        read_text_file(file_name, file_str);

        find_line_begin(*file_str, line_begin);
}

IObj::Image read_image_from_file(const std::string& file_name)
{
        sf::Image image;
        if (!image.loadFromFile(file_name))
        {
                error("Error open image file " + file_name);
        }

        unsigned long long buffer_size = 4ull * image.getSize().x * image.getSize().y;

        IObj::Image obj_image;
        obj_image.dimensions[0] = image.getSize().x;
        obj_image.dimensions[1] = image.getSize().y;
        obj_image.srgba_pixels.resize(buffer_size);

        static_assert(sizeof(decltype(*obj_image.srgba_pixels.data())) == sizeof(decltype(*image.getPixelsPtr())));

        std::memcpy(obj_image.srgba_pixels.data(), image.getPixelsPtr(), buffer_size);

        return obj_image;
}

void load_image(const std::string& dir_name, const std::string& image_name, std::map<std::string, int>* image_index,
                std::vector<IObj::Image>* images, int* index)
{
        std::string file_name = trim(image_name);

        if (file_name.size() == 0)
        {
                error("No image file name");
        }

#if defined(__linux__)
        // путь к файлу может быть указан в формате Windows, поэтому надо заменить разделители
        std::replace(file_name.begin(), file_name.end(), '\\', '/');
#endif

        file_name = dir_name + "/" + file_name;

        if (auto iter = image_index->find(file_name); iter != image_index->end())
        {
                *index = iter->second;
                return;
        };

        images->push_back(read_image_from_file(file_name));
        *index = images->size() - 1;
        image_index->emplace(file_name, *index);
}

// Между begin и end находится уже проверенное целое число в формате DDDDD без знака
int digits_to_integer(const std::string& s, long long begin, long long end)
{
        size_t length = end - begin;

        if (length > std::numeric_limits<int>::digits10)
        {
                error("Error convert to int (too big): " + s.substr(begin, length));
        }

        --end;
        int sum = ASCII::char_to_int(s[end]);
        int mul = 1;
        while (--end >= begin)
        {
                mul *= 10;
                sum += ASCII::char_to_int(s[end]) * mul;
        }

        return sum;
}

void read_integer(const std::string& line, size_t size, size_t* i, int* value)
{
        size_t begin = *i;
        if (*i < size && is_hyphen_minus(line[*i]))
        {
                ++begin;
        }

        size_t end = begin;

        read(line, size, ASCII::is_digit, &end);

        if (end > begin)
        {
                *value = (begin == *i) ? digits_to_integer(line, begin, end) : -digits_to_integer(line, begin, end);
                *i = end;
        }
}

// 0 означает, что нет индекса.
// Индексы находятся в порядке face, texture, normal.
template <typename T>
void check_indices(const T& v, unsigned group_count, const char* line_begin)
{
        int end = group_count * 3;

        ASSERT(end <= static_cast<int>(v.size()));

        for (int idx = 0; idx < end; idx += 3)
        {
                if (v[idx] == 0)
                {
                        error("Error read face from line:\n\"" + trim(line_begin) + "\"");
                }
        }

        for (int idx = 1; idx < end - 3; idx += 3)
        {
                if ((v[idx] == 0) != (v[idx + 3] == 0))
                {
                        error("Inconsistent face texture indices in the line:\n\"" + trim(line_begin) + "\"");
                }
        }

        for (int idx = 2; idx < end - 3; idx += 3)
        {
                if ((v[idx] == 0) != (v[idx + 3] == 0))
                {
                        error("Inconsistent face normal indices in the line:\n\"" + trim(line_begin) + "\"");
                }
        }
}

template <size_t N>
void read_vertex_groups(const std::string& line, const char* line_begin, size_t begin, size_t end, std::array<size_t, N>* begins,
                        std::array<size_t, N>* ends, unsigned* cnt)
{
        size_t i = begin;
        *cnt = 0;
        for (unsigned z = 0; z < N + 1; ++z)
        {
                read(line, end, ASCII::is_space, &i);
                size_t i2 = i;
                read(line, end, ASCII::is_not_space, &i2);
                if (i2 != i)
                {
                        if (z < N)
                        {
                                (*begins)[z] = i;
                                (*ends)[z] = i2;
                                i = i2;
                        }
                        else
                        {
                                error("Too many vertex groups (max=" + to_string(N) + ") in line:\n\"" + trim(line_begin) + "\"");
                        }
                }
                else
                {
                        *cnt = z;
                        return;
                }
        }
}

void read_v_vt_vn(const std::string& line, const char* line_begin, size_t begin, size_t end, int* v)
{
        size_t i = begin;
        for (int a = 0; a < 3; ++a)
        {
                if (i == end)
                {
                        if (a > 0) // a > 0 — считывается текстура или нормаль
                        {
                                v[a] = 0;
                                continue;
                        }
                        error("Error read face from line:\n\"" + trim(line_begin) + "\"");
                }

                if (a > 0)
                {
                        if (!is_solidus(line[i]))
                        {
                                error("Error read face from line:\n\"" + trim(line_begin) + "\"");
                        }
                        ++i;
                }

                size_t i2 = i;

                read_integer(line, end, &i2, &v[a]);
                if (i2 != i)
                {
                        if (v[a] == 0)
                        {
                                error("Zero face index:\n\"" + trim(line_begin) + "\"");
                        }
                }
                else
                {
                        if (a == 0) // a == 0 — считывается вершина
                        {
                                error("Error read face from line:\n\"" + trim(line_begin) + "\"");
                        }

                        v[a] = 0;
                }

                i = i2;
        }

        if (i != end)
        {
                error("Error read face from line:\n\"" + trim(line_begin) + "\"");
        }
}

// Разделение строки на 9 чисел
// " число/возможно_число/возможно_число число/возможно_число/возможно_число число/возможно_число/возможно_число ".
// Примеры: " 1/2/3 4/5/6 7/8/9", "1//2 3//4 5//6", " 1// 2// 3// ".
void read_faces(const std::string& line, size_t begin, size_t end, std::array<IObj::Face, MAX_FACES_PER_LINE>* faces,
                unsigned* face_count)

{
        constexpr unsigned MAX_GROUP_COUNT = MAX_FACES_PER_LINE + 2;

        std::array<int, MAX_GROUP_COUNT * 3> v;
        std::array<size_t, MAX_GROUP_COUNT> begins, ends;

        unsigned group_count;

        read_vertex_groups(line, &line[begin], begin, end, &begins, &ends, &group_count);

        if (group_count < 3)
        {
                error("Error read at least 3 vertices from line:\n\"" + trim(&line[begin]) + "\"");
        }

        for (unsigned z = 0; z < group_count; ++z)
        {
                read_v_vt_vn(line, &line[begin], begins[z], ends[z], &v[z * 3]);
        }

        // Обязательная проверка индексов
        check_indices(v, group_count, &line[begin]);

        *face_count = group_count - 2;

        for (unsigned i = 0, base = 0; i < *face_count; ++i, base += 3)
        {
                (*faces)[i].has_texcoord = !(v[1] == 0);
                (*faces)[i].has_normal = !(v[2] == 0);

                (*faces)[i].vertices[0].v = v[0];
                (*faces)[i].vertices[0].t = v[1];
                (*faces)[i].vertices[0].n = v[2];

                (*faces)[i].vertices[1].v = v[base + 3];
                (*faces)[i].vertices[1].t = v[base + 4];
                (*faces)[i].vertices[1].n = v[base + 5];

                (*faces)[i].vertices[2].v = v[base + 6];
                (*faces)[i].vertices[2].t = v[base + 7];
                (*faces)[i].vertices[2].n = v[base + 8];
        }
}

template <typename T>
bool read_float(const char** str, T* p)
{
        using FP = std::remove_volatile_t<T>;

        static_assert(std::is_same_v<FP, float> || std::is_same_v<FP, double> || std::is_same_v<FP, long double>);

        char* end;

        if constexpr (std::is_same_v<FP, float>)
        {
                *p = std::strtof(*str, &end);
        }
        if constexpr (std::is_same_v<FP, double>)
        {
                *p = std::strtod(*str, &end);
        }
        if constexpr (std::is_same_v<FP, long double>)
        {
                *p = std::strtold(*str, &end);
        }

        // В соответствии со спецификацией файла OBJ, между числами должны быть пробелы,
        // а после чисел пробелы, конец строки или комментарий.
        // Здесь без проверок этого.
        if (*str == end || errno == ERANGE || !is_finite(*p))
        {
                return false;
        }
        else
        {
                *str = end;
                return true;
        }
};

template <typename... T>
int string_to_float(const char* str, T*... floats)
{
        constexpr int N = sizeof...(T);

        static_assert(N > 0);
        static_assert(((std::is_same_v<std::remove_volatile_t<T>, float> || std::is_same_v<std::remove_volatile_t<T>, double> ||
                        std::is_same_v<std::remove_volatile_t<T>, long double>)&&...));

        errno = 0;
        int cnt = 0;

        ((read_float(&str, floats) ? ++cnt : false) && ...);

        return cnt;
}

void read_float(const char* str, vec3f* v)
{
        if (3 != string_to_float(str, &(*v)[0], &(*v)[1], &(*v)[2]))
        {
                std::string l = str;
                error("error read 3 floating points from line:\n\"" + trim(l) + "\"");
        }
}

void read_float_texture(const char* str, vec2f* v)
{
        float tmp;

        int n = string_to_float(str, &(*v)[0], &(*v)[1], &tmp);
        if (n != 2 && n != 3)
        {
                std::string l = str;
                error("error read 2 or 3 floating points from line:\n\"" + trim(l) + "\"");
        }
        if (n == 3 && tmp != 0.0f)
        {
                std::string l = str;
                error("3D textures not supported:\n\"" + trim(l) + "\"");
        }
}

void read_float(const char* str, float* v)
{
        if (1 != string_to_float(str, v))
        {
                std::string l = str;
                error("error read 1 floating point from line:\n\"" + trim(l) + "\"");
        }
}

void read_mtl_name(const std::string& line, size_t b, size_t e, std::string* res)
{
        const size_t size = e;

        size_t i = b;
        read(line, size, ASCII::is_space, &i);
        if (i == size)
        {
                std::string l = &line[b];
                error("Error read material name from line:\n\"" + trim(l) + "\"");
        }

        size_t i2 = i;
        read(line, size, ASCII::is_not_space, &i2);
        *res = line.substr(i, i2 - i);
        i = i2;

        read(line, size, ASCII::is_space, &i);
        if (i != size)
        {
                std::string l = &line[b];
                error("Error read material name from line:\n\"" + trim(l) + "\"");
        }
}

void read_library_names(const std::string& line, size_t b, size_t e, std::vector<std::string>* v,
                        std::set<std::string>* lib_unique_names)
{
        const size_t size = e;
        bool found = false;
        size_t i = b;

        while (true)
        {
                read(line, size, ASCII::is_space, &i);
                if (i == size)
                {
                        if (!found)
                        {
                                std::string l = &line[b];
                                error("Library name not found in line:\n\"" + trim(l) + "\"");
                        }
                        return;
                }

                size_t i2 = i;
                read(line, size, ASCII::is_not_space, &i2);
                std::string name{line.substr(i, i2 - i)};
                i = i2;
                found = true;

                if (lib_unique_names->find(name) == lib_unique_names->end())
                {
                        v->push_back(name);
                        lib_unique_names->insert(std::move(name));
                }
        }
}

// Разделение строки на 2 части " не_пробелы | остальной текст до символа комментария или конца строки"
template <typename T, typename C>
void split(const std::string& line, size_t begin, size_t end, const T& space, const C& comment, size_t* first_b, size_t* first_e,
           size_t* second_b, size_t* second_e)
{
        const size_t size = end;

        size_t i = begin;
        while (i < size && space(line[i]) && !comment(line[i]))
        {
                ++i;
        }
        if (i == size || comment(line[i]))
        {
                *first_b = i;
                *first_e = i;
                *second_b = i;
                *second_e = i;
                return;
        }

        size_t i2 = i + 1;
        while (i2 < size && !space(line[i2]) && !comment(line[i2]))
        {
                ++i2;
        }
        *first_b = i;
        *first_e = i2;

        i = i2;

        if (i == size || comment(line[i]))
        {
                *second_b = i;
                *second_e = i;
                return;
        }

        // первый пробел пропускается
        ++i;

        i2 = i;
        while (i2 < size && !comment(line[i2]))
        {
                ++i2;
        }

        *second_b = i;
        *second_e = i2;
}

void split_line(std::string* file_str, const std::vector<size_t>& line_begin, size_t line_num, size_t* first_b, size_t* first_e,
                size_t* second_b, size_t* second_e)
{
        size_t l_b = line_begin[line_num];
        size_t l_e = (line_num + 1 < line_begin.size()) ? line_begin[line_num + 1] : file_str->size();

        // В конце строки находится символ '\n', сместиться на него.
        --l_e;

        split(*file_str, l_b, l_e, ASCII::is_space, is_number_sign, first_b, first_e, second_b, second_e);

        (*file_str)[*first_e] = 0; // пробел, символ комментария '#' или символ '\n'
        (*file_str)[*second_e] = 0; // символ комментария '#' или символ '\n'
}

bool face_is_one_dimensional(const vec3f& v0, const vec3f& v1, const vec3f& v2)
{
        Vector<3, double> e0 = to_vector<double>(v1 - v0);
        Vector<3, double> e1 = to_vector<double>(v2 - v0);

        // Перебрать все возможные определители 2x2.
        // Здесь достаточно просто сравнить с 0.

        if (e0[1] * e1[2] - e0[2] * e1[1] != 0)
        {
                return false;
        }

        if (e0[0] * e1[2] - e0[2] * e1[0] != 0)
        {
                return false;
        }

        if (e0[0] * e1[1] - e0[1] * e1[0] != 0)
        {
                return false;
        }

        return true;
}

class FileObj final : public IObj
{
        std::vector<vec3f> m_vertices;
        std::vector<vec2f> m_texcoords;
        std::vector<vec3f> m_normals;
        std::vector<Face> m_faces;
        std::vector<Point> m_points;
        std::vector<Line> m_lines;
        std::vector<Material> m_materials;
        std::vector<Image> m_images;
        vec3f m_center;
        float m_length;

        enum class ObjLineType
        {
                V,
                VT,
                VN,
                F,
                USEMTL,
                MTLLIB,
                NONE,
                NOT_SUPPORTED
        };

        // enum class MtlLineType
        //{
        //        NEWMTL,
        //        KA,
        //        KD,
        //        KS,
        //        NS,
        //        MAP_KA,
        //        MAP_KD,
        //        MAP_KS,
        //        NONE,
        //        NOT_SUPPORTED
        //};

        struct ObjLine
        {
                ObjLineType type;
                size_t second_b, second_e;
                std::array<Face, MAX_FACES_PER_LINE> faces;
                unsigned face_count;
                vec3f v;
        };

        // struct MtlLine
        //{
        //        MtlLineType type;
        //        size_t second_b, second_e;
        //        vec3f v;
        //};

        struct Counters
        {
                atomic_counter v = 0;
                atomic_counter vt = 0;
                atomic_counter vn = 0;
                atomic_counter f = 0;
        };

        void check_face_indices() const;

        bool remove_one_dimensional_faces();

        static void read_obj_stage_one(unsigned thread_num, unsigned thread_count, Counters* counters, std::string* file_ptr,
                                       std::vector<size_t>* line_begin, std::vector<ObjLine>* line_prop, ProgressRatio* progress);
        void read_obj_stage_two(const Counters* counters, std::string* file_ptr, std::vector<ObjLine>* line_prop,
                                ProgressRatio* progress, std::map<std::string, int>* material_index,
                                std::vector<std::string>* library_names);
        void read_obj_thread(unsigned thread_num, unsigned thread_count, Counters* counters, ThreadBarrier* barrier,
                             std::atomic_bool* error_found, std::string* file_str, std::vector<size_t>* line_begin,
                             std::vector<ObjLine>* line_prop, ProgressRatio* progress, std::map<std::string, int>* material_index,
                             std::vector<std::string>* library_names);
        void read_obj(const std::string& file_name, ProgressRatio* progress, std::map<std::string, int>* material_index,
                      std::vector<std::string>* library_names);

        void read_lib(const std::string& dir_name, const std::string& file_name, ProgressRatio* progress,
                      std::map<std::string, int>* material_index, std::map<std::string, int>* image_index);
        void read_libs(const std::string& dir_name, ProgressRatio* progress, std::map<std::string, int>* material_index,
                       const std::vector<std::string>& library_names);

        void read_obj_and_mtl(const std::string& file_name, ProgressRatio* progress);

        const std::vector<vec3f>& vertices() const override
        {
                return m_vertices;
        }
        const std::vector<vec2f>& texcoords() const override
        {
                return m_texcoords;
        }
        const std::vector<vec3f>& normals() const override
        {
                return m_normals;
        }
        const std::vector<Face>& faces() const override
        {
                return m_faces;
        }
        const std::vector<Point>& points() const override
        {
                return m_points;
        }
        const std::vector<Line>& lines() const override
        {
                return m_lines;
        }
        const std::vector<Material>& materials() const override
        {
                return m_materials;
        }
        const std::vector<Image>& images() const override
        {
                return m_images;
        }
        vec3f center() const override
        {
                return m_center;
        }
        float length() const override
        {
                return m_length;
        }

public:
        FileObj(const std::string& file_name, ProgressRatio* progress);
};

void FileObj::check_face_indices() const
{
        int vertex_count = m_vertices.size();
        int texcoord_count = m_texcoords.size();
        int normal_count = m_normals.size();

        for (const Face& face : m_faces)
        {
                for (const Vertex& vertex : face.vertices)
                {
                        if (vertex.v < 0 || vertex.v >= vertex_count)
                        {
                                error("Vertex index " + std::to_string(vertex.v) + " is out of bounds [0, " +
                                      std::to_string(vertex_count) + ")");
                        }
                        if (face.has_texcoord && (vertex.t < 0 || vertex.t >= texcoord_count))
                        {
                                error("Texture coord index " + std::to_string(vertex.t) + " is out of bounds [0, " +
                                      std::to_string(texcoord_count) + ")");
                        }
                        if (face.has_normal && (vertex.n < 0 || vertex.n >= normal_count))
                        {
                                error("Normal index " + std::to_string(vertex.n) + " is out of bounds [0, " +
                                      std::to_string(normal_count) + ")");
                        }
                }
        }
}

bool FileObj::remove_one_dimensional_faces()
{
        std::vector<bool> one_d_faces(m_faces.size(), false);

        int one_d_face_count = 0;

        for (unsigned i = 0; i < m_faces.size(); ++i)
        {
                vec3f v0 = m_vertices[m_faces[i].vertices[0].v];
                vec3f v1 = m_vertices[m_faces[i].vertices[1].v];
                vec3f v2 = m_vertices[m_faces[i].vertices[2].v];

                if (face_is_one_dimensional(v0, v1, v2))
                {
                        one_d_faces[i] = true;
                        ++one_d_face_count;
                }
        }

        if (one_d_face_count == 0)
        {
                return false;
        }

        std::vector<Face> faces;
        faces.reserve(m_faces.size() - one_d_face_count);

        for (unsigned i = 0; i < m_faces.size(); ++i)
        {
                if (!one_d_faces[i])
                {
                        faces.push_back(m_faces[i]);
                }
        }

        m_faces = std::move(faces);

        return true;
}

void FileObj::read_obj_stage_one(unsigned thread_num, unsigned thread_count, Counters* counters, std::string* file_ptr,
                                 std::vector<size_t>* line_begin, std::vector<ObjLine>* line_prop, ProgressRatio* progress)
{
        std::string& file_str = *file_ptr;
        const size_t line_count = line_begin->size();
        const double line_count_reciprocal = 1.0 / line_begin->size();

        for (unsigned line_num = thread_num; line_num < line_count; line_num += thread_count)
        {
                if ((line_num & 0xfff) == 0xfff)
                {
                        progress->set(line_num * line_count_reciprocal);
                }

                ObjLine lp;
                size_t first_b, first_e;

                split_line(&file_str, *line_begin, line_num, &first_b, &first_e, &lp.second_b, &lp.second_e);

                const char* first = &file_str[first_b];

                if (str_equal(first, OBJ_v))
                {
                        lp.type = ObjLineType::V;
                        vec3f v;
                        read_float(&file_str[lp.second_b], &v);
                        lp.v = v;
                        if (ATOMIC_COUNTER_LOCK_FREE)
                        {
                                ++(counters->v);
                        }
                }
                else if (str_equal(first, OBJ_vt))
                {
                        lp.type = ObjLineType::VT;
                        vec2f v;
                        read_float_texture(&file_str[lp.second_b], &v);
                        lp.v[0] = v[0];
                        lp.v[1] = v[1];
                        if (ATOMIC_COUNTER_LOCK_FREE)
                        {
                                ++(counters->vt);
                        }
                }
                else if (str_equal(first, OBJ_vn))
                {
                        lp.type = ObjLineType::VN;
                        vec3f v;
                        read_float(&file_str[lp.second_b], &v);
                        lp.v = normalize(v);
                        if (ATOMIC_COUNTER_LOCK_FREE)
                        {
                                ++(counters->vn);
                        }
                }
                else if (str_equal(first, OBJ_f))
                {
                        lp.type = ObjLineType::F;
                        read_faces(file_str, lp.second_b, lp.second_e, &lp.faces, &lp.face_count);
                        if (ATOMIC_COUNTER_LOCK_FREE)
                        {
                                ++(counters->f);
                        }
                }
                else if (str_equal(first, OBJ_usemtl))
                {
                        lp.type = ObjLineType::USEMTL;
                }
                else if (str_equal(first, OBJ_mtllib))
                {
                        lp.type = ObjLineType::MTLLIB;
                }
                else if (!*first)
                {
                        lp.type = ObjLineType::NONE;
                }
                else
                {
                        lp.type = ObjLineType::NOT_SUPPORTED;
                }

                (*line_prop)[line_num] = lp;
        }
}

// Индексы в OBJ:
//   начинаются с 1 для абсолютных значений,
//   начинаются с -1 для относительных значений назад.
// Преобразование в абсолютные значения с началом от 0.
void correct_indices(IObj::Face* face, int vertices_size, int texcoords_size, int normals_size)
{
        for (int i = 0; i < 3; ++i)
        {
                int& v = face->vertices[i].v;
                int& t = face->vertices[i].t;
                int& n = face->vertices[i].n;

                ASSERT(v != 0);

                v = v > 0 ? v - 1 : vertices_size + v;
                t = t > 0 ? t - 1 : (t < 0 ? texcoords_size + t : -1);
                n = n > 0 ? n - 1 : (n < 0 ? normals_size + n : -1);
        }
}

void FileObj::read_obj_stage_two(const Counters* counters, std::string* file_ptr, std::vector<ObjLine>* line_prop,
                                 ProgressRatio* progress, std::map<std::string, int>* material_index,
                                 std::vector<std::string>* library_names)
{
        if (ATOMIC_COUNTER_LOCK_FREE)
        {
                m_vertices.reserve(counters->v);
                m_texcoords.reserve(counters->vt);
                m_normals.reserve(counters->vn);
                m_faces.reserve(counters->f);
        }

        const std::string& file_str = *file_ptr;
        const size_t line_count = line_prop->size();
        const double line_count_reciprocal = 1.0 / line_prop->size();

        int mtl_index = -1;
        std::string mtl_name;
        std::set<std::string> unique_library_names;

        for (size_t line_num = 0; line_num < line_count; ++line_num)
        {
                if ((line_num & 0xfff) == 0xfff)
                {
                        progress->set(line_num * line_count_reciprocal);
                }

                ObjLine& lp = (*line_prop)[line_num];

                switch (lp.type)
                {
                case ObjLineType::V:
                        m_vertices.push_back(lp.v);
                        break;
                case ObjLineType::VT:
                        m_texcoords.emplace_back(lp.v[0], lp.v[1]);
                        break;
                case ObjLineType::VN:
                        m_normals.push_back(lp.v);
                        break;
                case ObjLineType::F:
                        for (unsigned i = 0; i < lp.face_count; ++i)
                        {
                                lp.faces[i].material = mtl_index;
                                correct_indices(&lp.faces[i], m_vertices.size(), m_texcoords.size(), m_normals.size());
                                m_faces.push_back(std::move(lp.faces[i]));
                        }
                        break;
                case ObjLineType::USEMTL:
                {
                        read_mtl_name(file_str, lp.second_b, lp.second_e, &mtl_name);
                        auto iter = material_index->find(mtl_name);
                        if (iter != material_index->end())
                        {
                                mtl_index = iter->second;
                        }
                        else
                        {
                                IObj::Material mtl;
                                mtl.name = mtl_name;
                                m_materials.push_back(std::move(mtl));
                                material_index->emplace(std::move(mtl_name), m_materials.size() - 1);
                                mtl_index = m_materials.size() - 1;
                        }
                        break;
                }
                case ObjLineType::MTLLIB:
                        read_library_names(file_str, lp.second_b, lp.second_e, library_names, &unique_library_names);
                        break;
                case ObjLineType::NONE:
                        break;
                case ObjLineType::NOT_SUPPORTED:
                        break;
                }
        }

        if (!ATOMIC_COUNTER_LOCK_FREE)
        {
                m_vertices.shrink_to_fit();
                m_texcoords.shrink_to_fit();
                m_normals.shrink_to_fit();
                m_faces.shrink_to_fit();
        }
}

void FileObj::read_obj_thread(unsigned thread_num, unsigned thread_count, Counters* counters, ThreadBarrier* barrier,
                              std::atomic_bool* error_found, std::string* file_ptr, std::vector<size_t>* line_begin,
                              std::vector<ObjLine>* line_prop, ProgressRatio* progress,
                              std::map<std::string, int>* material_index, std::vector<std::string>* library_names)
{
        // параллельно

        try
        {
                read_obj_stage_one(thread_num, thread_count, counters, file_ptr, line_begin, line_prop, progress);
        }
        catch (...)
        {
                error_found->store(true); // нет исключений
                barrier->wait();
                throw;
        }
        barrier->wait();
        if (*error_found)
        {
                return;
        }

        if (thread_num != 0)
        {
                return;
        }

        //последовательно

        line_begin->clear();
        line_begin->shrink_to_fit();

        read_obj_stage_two(counters, file_ptr, line_prop, progress, material_index, library_names);
}

void FileObj::read_lib(const std::string& dir_name, const std::string& file_name, ProgressRatio* progress,
                       std::map<std::string, int>* material_index, std::map<std::string, int>* image_index)
{
        std::string file_str;
        std::vector<size_t> line_begin;

        const std::string lib_name = dir_name + "/" + file_name;

        read_file_lines(lib_name, &file_str, &line_begin);

        const std::string lib_dir = get_dir_name(lib_name);

        FileObj::Material* mtl = nullptr;
        std::string mtl_name;

        const size_t line_count = line_begin.size();
        const double line_count_reciprocal = 1.0 / line_begin.size();

        for (size_t line_num = 0; line_num < line_count; ++line_num)
        {
                if ((line_num & 0xfff) == 0xfff)
                {
                        progress->set(line_num * line_count_reciprocal);
                }

                size_t first_b, first_e, second_b, second_e;

                split_line(&file_str, line_begin, line_num, &first_b, &first_e, &second_b, &second_e);

                const char* first = &file_str[first_b];

                if (!*first)
                {
                        continue;
                }
                else if (str_equal(first, MTL_newmtl))
                {
                        if (material_index->size() == 0)
                        {
                                // все материалы найдены
                                break;
                        }

                        read_mtl_name(file_str, second_b, second_e, &mtl_name);

                        auto iter = material_index->find(mtl_name);
                        if (iter != material_index->end())
                        {
                                mtl = &(m_materials[iter->second]);
                                material_index->erase(mtl_name);
                        }
                        else
                        {
                                // ненужный материал
                                mtl = nullptr;
                        }
                }
                else if (str_equal(first, MTL_Ka))
                {
                        if (!mtl)
                        {
                                continue;
                        }
                        read_float(&file_str[second_b], &mtl->Ka);

                        if (!check_range(mtl->Ka, 0, 1))
                        {
                                error("Error Ka in material " + mtl->name);
                        }
                }
                else if (str_equal(first, MTL_Kd))
                {
                        if (!mtl)
                        {
                                continue;
                        }
                        read_float(&file_str[second_b], &mtl->Kd);

                        if (!check_range(mtl->Kd, 0, 1))
                        {
                                error("Error Kd in material " + mtl->name);
                        }
                }
                else if (str_equal(first, MTL_Ks))
                {
                        if (!mtl)
                        {
                                continue;
                        }
                        read_float(&file_str[second_b], &mtl->Ks);

                        if (!check_range(mtl->Ks, 0, 1))
                        {
                                error("Error Ks in material " + mtl->name);
                        }
                }
                else if (str_equal(first, MTL_Ns))
                {
                        if (!mtl)
                        {
                                continue;
                        }
                        read_float(&file_str[second_b], &mtl->Ns);

                        if (!check_range(mtl->Ns, 0, 1000))
                        {
                                error("Error Ns in material " + mtl->name);
                        }
                }
                else if (str_equal(first, MTL_map_Ka))
                {
                        if (!mtl)
                        {
                                continue;
                        }
                        std::string image_name = file_str.substr(second_b, second_e - second_b);
                        load_image(lib_dir, image_name, image_index, &m_images, &mtl->map_Ka);
                }
                else if (str_equal(first, MTL_map_Kd))
                {
                        if (!mtl)
                        {
                                continue;
                        }
                        std::string image_name = file_str.substr(second_b, second_e - second_b);
                        load_image(lib_dir, image_name, image_index, &m_images, &mtl->map_Kd);
                }
                else if (str_equal(first, MTL_map_Ks))
                {
                        if (!mtl)
                        {
                                continue;
                        }
                        std::string image_name = file_str.substr(second_b, second_e - second_b);
                        load_image(lib_dir, image_name, image_index, &m_images, &mtl->map_Ks);
                }
        }
}

void FileObj::read_libs(const std::string& dir_name, ProgressRatio* progress, std::map<std::string, int>* material_index,
                        const std::vector<std::string>& library_names)
{
        std::map<std::string, int> image_index;

        for (size_t i = 0; (i < library_names.size()) && (material_index->size() > 0); ++i)
        {
                read_lib(dir_name, library_names[i], progress, material_index, &image_index);
        }

        if (material_index->size() != 0)
        {
                error("Materials not found in libraries: " + get_string_list(*material_index));
        }

        m_materials.shrink_to_fit();
        m_images.shrink_to_fit();
}

void FileObj::read_obj(const std::string& file_name, ProgressRatio* progress, std::map<std::string, int>* material_index,
                       std::vector<std::string>* library_names)
{
        const int hardware_concurrency = get_hardware_concurrency();

        std::string file_str;
        std::vector<size_t> line_begin;

        read_file_lines(file_name, &file_str, &line_begin);

        std::vector<ObjLine> line_prop(line_begin.size());
        ThreadBarrier barrier(hardware_concurrency);
        std::atomic_bool error_found{false};
        Counters counters;

        ThreadsWithCatch threads(hardware_concurrency);
        for (int i = 0; i < hardware_concurrency; ++i)
        {
                threads.add([&, i]() {
                        read_obj_thread(i, hardware_concurrency, &counters, &barrier, &error_found, &file_str, &line_begin,
                                        &line_prop, progress, material_index, library_names);
                });
        }
        threads.join();
}

void FileObj::read_obj_and_mtl(const std::string& file_name, ProgressRatio* progress)
{
        progress->set_undefined();

        std::map<std::string, int> material_index;
        std::vector<std::string> library_names;

        read_obj(file_name, progress, &material_index, &library_names);

        if (m_faces.size() == 0)
        {
                error("No faces found in OBJ file");
        }

        check_face_indices();

        center_and_length(m_vertices, m_faces, &m_center, &m_length);

        if (remove_one_dimensional_faces())
        {
                if (m_faces.size() == 0)
                {
                        error("No 2D faces found in OBJ file");
                }
                center_and_length(m_vertices, m_faces, &m_center, &m_length);
        }

        read_libs(get_dir_name(file_name), progress, &material_index, library_names);
}

FileObj::FileObj(const std::string& file_name, ProgressRatio* progress)
{
        double start_time = time_in_seconds();

        read_obj_and_mtl(file_name, progress);

        LOG("OBJ loaded, " + to_string_fixed(time_in_seconds() - start_time, 5) + " s");
}

// Чтение вершин из текстового файла. Одна вершина на строку. Три координаты через пробел.
// x y z
// x y z
class FileTxt final : public IObj
{
        std::vector<vec3f> m_vertices;
        std::vector<vec2f> m_texcoords;
        std::vector<vec3f> m_normals;
        std::vector<Face> m_faces;
        std::vector<Point> m_points;
        std::vector<Line> m_lines;
        std::vector<Material> m_materials;
        std::vector<Image> m_images;
        vec3f m_center;
        float m_length;

        void read_points_thread(unsigned thread_num, unsigned thread_count, std::string* file_ptr,
                                std::vector<size_t>* line_begin, std::vector<vec3f>* lines, ProgressRatio* progress) const;
        void read_points(const std::string& file_name, ProgressRatio* progress);
        void read_text(const std::string& file_name, ProgressRatio* progress);

        const std::vector<vec3f>& vertices() const override
        {
                return m_vertices;
        }
        const std::vector<vec2f>& texcoords() const override
        {
                return m_texcoords;
        }
        const std::vector<vec3f>& normals() const override
        {
                return m_normals;
        }
        const std::vector<Face>& faces() const override
        {
                return m_faces;
        }
        const std::vector<Point>& points() const override
        {
                return m_points;
        }
        const std::vector<Line>& lines() const override
        {
                return m_lines;
        }
        const std::vector<Material>& materials() const override
        {
                return m_materials;
        }
        const std::vector<Image>& images() const override
        {
                return m_images;
        }
        vec3f center() const override
        {
                return m_center;
        }
        float length() const override
        {
                return m_length;
        }

public:
        FileTxt(const std::string& file_name, ProgressRatio* progress);
};

void FileTxt::read_points_thread(unsigned thread_num, unsigned thread_count, std::string* file_ptr,
                                 std::vector<size_t>* line_begin, std::vector<vec3f>* lines, ProgressRatio* progress) const
{
        const size_t line_count = line_begin->size();
        const double line_count_reciprocal = 1.0 / line_begin->size();

        for (size_t line_num = thread_num; line_num < line_count; line_num += thread_count)
        {
                if ((line_num & 0xfff) == 0xfff)
                {
                        progress->set(line_num * line_count_reciprocal);
                }

                size_t l_b = (*line_begin)[line_num];
                size_t l_e = (line_num + 1 < line_begin->size()) ? (*line_begin)[line_num + 1] : file_ptr->size();

                // В конце строки находится символ '\n', сместиться на него и записать вместо него 0
                --l_e;
                (*file_ptr)[l_e] = 0;

                read_float(&(*file_ptr)[l_b], &(*lines)[line_num]);
        }
}

void FileTxt::read_points(const std::string& file_name, ProgressRatio* progress)
{
        const int hardware_concurrency = get_hardware_concurrency();

        std::string file_str;
        std::vector<size_t> line_begin;

        read_file_lines(file_name, &file_str, &line_begin);

        m_vertices.resize(line_begin.size());

        ThreadsWithCatch threads(hardware_concurrency);
        for (int i = 0; i < hardware_concurrency; ++i)
        {
                threads.add(
                        [&, i]() { read_points_thread(i, hardware_concurrency, &file_str, &line_begin, &m_vertices, progress); });
        }
        threads.join();
}

void FileTxt::read_text(const std::string& file_name, ProgressRatio* progress)
{
        progress->set_undefined();

        read_points(file_name, progress);

        if (m_vertices.size() == 0)
        {
                error("No vertices found in Text file");
        }

        m_points.resize(m_vertices.size());
        for (unsigned i = 0; i < m_points.size(); ++i)
        {
                m_points[i].vertex = i;
        }

        center_and_length(m_vertices, m_points, &m_center, &m_length);
}

FileTxt::FileTxt(const std::string& file_name, ProgressRatio* progress)
{
        double start_time = time_in_seconds();

        read_text(file_name, progress);

        LOG("TEXT loaded, " + to_string_fixed(time_in_seconds() - start_time, 5) + " s");
}
}

std::unique_ptr<IObj> load_obj_from_file(const std::string& file_name, ProgressRatio* progress)
{
        std::string upper_extension = to_upper(get_extension(file_name));

        if (upper_extension == "OBJ")
        {
                return std::make_unique<FileObj>(file_name, progress);
        }

        if (upper_extension == "TXT")
        {
                return std::make_unique<FileTxt>(file_name, progress);
        }

        std::string ext = get_extension(file_name);
        if (ext.size() > 0)
        {
                error("Unsupported file format " + ext);
        }
        else
        {
                error("File extension not found");
        }
}
