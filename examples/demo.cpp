
#include <filesystem>
#include <span>

#include <fcntl.h>    // For open()
#include <sys/mman.h> // For mmap(), munmap()
#include <sys/stat.h> // For fstat()
#include <unistd.h>   // For close()

#include <spdlog/spdlog.h>

#include <backfill/api.h>


template <>
struct fmt::formatter<float4> {
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
        return ctx.end();
    }

    template <typename FormatContext>
    auto format(float4 const& input, FormatContext& ctx) const
        -> decltype(ctx.out()) {
        return format_to(
            ctx.out(), "({} {} {} {})", input.x, input.y, input.z, input.w);
    }
};

template <>
struct fmt::formatter<mat4> {
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
        return ctx.end();
    }

    template <typename FormatContext>
    auto format(mat4 const& input, FormatContext& ctx) const
        -> decltype(ctx.out()) {
        return format_to(ctx.out(),
                         "(a={}, b={}, c={}, d={})",
                         input.a,
                         input.b,
                         input.c,
                         input.d);
    }
};

constexpr float3 SPHERE_POS[] = {
    { 0.000000, -1.000000, 0.000000 },   { 0.723607, -0.447220, 0.525725 },
    { -0.276388, -0.447220, 0.850649 },  { -0.894426, -0.447216, 0.000000 },
    { -0.276388, -0.447220, -0.850649 }, { 0.723607, -0.447220, -0.525725 },
    { 0.276388, 0.447220, 0.850649 },    { -0.723607, 0.447220, 0.525725 },
    { -0.723607, 0.447220, -0.525725 },  { 0.276388, 0.447220, -0.850649 },
    { 0.894426, 0.447216, 0.000000 },    { 0.000000, 1.000000, 0.000000 },
    { -0.232822, -0.657519, 0.716563 },  { -0.162456, -0.850654, 0.499995 },
    { -0.077607, -0.967950, 0.238853 },  { 0.203181, -0.967950, 0.147618 },
    { 0.425323, -0.850654, 0.309011 },   { 0.609547, -0.657519, 0.442856 },
    { 0.531941, -0.502302, 0.681712 },   { 0.262869, -0.525738, 0.809012 },
    { -0.029639, -0.502302, 0.864184 },  { 0.812729, -0.502301, -0.295238 },
    { 0.850648, -0.525736, 0.000000 },   { 0.812729, -0.502301, 0.295238 },
    { 0.203181, -0.967950, -0.147618 },  { 0.425323, -0.850654, -0.309011 },
    { 0.609547, -0.657519, -0.442856 },  { -0.753442, -0.657515, 0.000000 },
    { -0.525730, -0.850652, 0.000000 },  { -0.251147, -0.967949, 0.000000 },
    { -0.483971, -0.502302, 0.716565 },  { -0.688189, -0.525736, 0.499997 },
    { -0.831051, -0.502299, 0.238853 },  { -0.232822, -0.657519, -0.716563 },
    { -0.162456, -0.850654, -0.499995 }, { -0.077607, -0.967950, -0.238853 },
    { -0.831051, -0.502299, -0.238853 }, { -0.688189, -0.525736, -0.499997 },
    { -0.483971, -0.502302, -0.716565 }, { -0.029639, -0.502302, -0.864184 },
    { 0.262869, -0.525738, -0.809012 },  { 0.531941, -0.502302, -0.681712 },
    { 0.956626, 0.251149, 0.147618 },    { 0.951058, -0.000000, 0.309013 },
    { 0.860698, -0.251151, 0.442858 },   { 0.860698, -0.251151, -0.442858 },
    { 0.951058, 0.000000, -0.309013 },   { 0.956626, 0.251149, -0.147618 },
    { 0.155215, 0.251152, 0.955422 },    { 0.000000, -0.000000, 1.000000 },
    { -0.155215, -0.251152, 0.955422 },  { 0.687159, -0.251152, 0.681715 },
    { 0.587786, 0.000000, 0.809017 },    { 0.436007, 0.251152, 0.864188 },
    { -0.860698, 0.251151, 0.442858 },   { -0.951058, -0.000000, 0.309013 },
    { -0.956626, -0.251149, 0.147618 },  { -0.436007, -0.251152, 0.864188 },
    { -0.587786, 0.000000, 0.809017 },   { -0.687159, 0.251152, 0.681715 },
    { -0.687159, 0.251152, -0.681715 },  { -0.587786, -0.000000, -0.809017 },
    { -0.436007, -0.251152, -0.864188 }, { -0.956626, -0.251149, -0.147618 },
    { -0.951058, 0.000000, -0.309013 },  { -0.860698, 0.251151, -0.442858 },
    { 0.436007, 0.251152, -0.864188 },   { 0.587786, -0.000000, -0.809017 },
    { 0.687159, -0.251152, -0.681715 },  { -0.155215, -0.251152, -0.955422 },
    { 0.000000, 0.000000, -1.000000 },   { 0.155215, 0.251152, -0.955422 },
    { 0.831051, 0.502299, 0.238853 },    { 0.688189, 0.525736, 0.499997 },
    { 0.483971, 0.502302, 0.716565 },    { 0.029639, 0.502302, 0.864184 },
    { -0.262869, 0.525738, 0.809012 },   { -0.531941, 0.502302, 0.681712 },
    { -0.812729, 0.502301, 0.295238 },   { -0.850648, 0.525736, 0.000000 },
    { -0.812729, 0.502301, -0.295238 },  { -0.531941, 0.502302, -0.681712 },
    { -0.262869, 0.525738, -0.809012 },  { 0.029639, 0.502302, -0.864184 },
    { 0.483971, 0.502302, -0.716565 },   { 0.688189, 0.525736, -0.499997 },
    { 0.831051, 0.502299, -0.238853 },   { 0.077607, 0.967950, 0.238853 },
    { 0.162456, 0.850654, 0.499995 },    { 0.232822, 0.657519, 0.716563 },
    { 0.753442, 0.657515, 0.000000 },    { 0.525730, 0.850652, 0.000000 },
    { 0.251147, 0.967949, 0.000000 },    { -0.203181, 0.967950, 0.147618 },
    { -0.425323, 0.850654, 0.309011 },   { -0.609547, 0.657519, 0.442856 },
    { -0.203181, 0.967950, -0.147618 },  { -0.425323, 0.850654, -0.309011 },
    { -0.609547, 0.657519, -0.442856 },  { 0.077607, 0.967950, -0.238853 },
    { 0.162456, 0.850654, -0.499995 },   { 0.232822, 0.657519, -0.716563 },
    { 0.361800, 0.894429, -0.262863 },   { 0.638194, 0.723610, -0.262864 },
    { 0.447209, 0.723612, -0.525728 },   { -0.138197, 0.894430, -0.425319 },
    { -0.052790, 0.723612, -0.688185 },  { -0.361804, 0.723612, -0.587778 },
    { -0.447210, 0.894429, 0.000000 },   { -0.670817, 0.723611, -0.162457 },
    { -0.670817, 0.723611, 0.162457 },   { -0.138197, 0.894430, 0.425319 },
    { -0.361804, 0.723612, 0.587778 },   { -0.052790, 0.723612, 0.688185 },
    { 0.361800, 0.894429, 0.262863 },    { 0.447209, 0.723612, 0.525728 },
    { 0.638194, 0.723610, 0.262864 },    { 0.861804, 0.276396, -0.425322 },
    { 0.809019, 0.000000, -0.587782 },   { 0.670821, 0.276397, -0.688189 },
    { -0.138199, 0.276397, -0.951055 },  { -0.309016, -0.000000, -0.951057 },
    { -0.447215, 0.276397, -0.850649 },  { -0.947213, 0.276396, -0.162458 },
    { -1.000000, 0.000001, 0.000000 },   { -0.947213, 0.276397, 0.162458 },
    { -0.447216, 0.276397, 0.850648 },   { -0.309017, -0.000001, 0.951056 },
    { -0.138199, 0.276397, 0.951055 },   { 0.670820, 0.276396, 0.688190 },
    { 0.809019, -0.000002, 0.587783 },   { 0.861804, 0.276394, 0.425323 },
    { 0.309017, -0.000000, -0.951056 },  { 0.447216, -0.276398, -0.850648 },
    { 0.138199, -0.276398, -0.951055 },  { -0.809018, -0.000000, -0.587783 },
    { -0.670819, -0.276397, -0.688191 }, { -0.861803, -0.276396, -0.425324 },
    { -0.809018, 0.000000, 0.587783 },   { -0.861803, -0.276396, 0.425324 },
    { -0.670819, -0.276397, 0.688191 },  { 0.309017, 0.000000, 0.951056 },
    { 0.138199, -0.276398, 0.951055 },   { 0.447216, -0.276398, 0.850648 },
    { 1.000000, 0.000000, 0.000000 },    { 0.947213, -0.276396, 0.162458 },
    { 0.947213, -0.276396, -0.162458 },  { 0.361803, -0.723612, -0.587779 },
    { 0.138197, -0.894429, -0.425321 },  { 0.052789, -0.723611, -0.688186 },
    { -0.447211, -0.723612, -0.525727 }, { -0.361801, -0.894429, -0.262863 },
    { -0.638195, -0.723609, -0.262863 }, { -0.638195, -0.723609, 0.262864 },
    { -0.361801, -0.894428, 0.262864 },  { -0.447211, -0.723610, 0.525729 },
    { 0.670817, -0.723611, -0.162457 },  { 0.670818, -0.723610, 0.162458 },
    { 0.447211, -0.894428, 0.000001 },   { 0.052790, -0.723612, 0.688185 },
    { 0.138199, -0.894429, 0.425321 },   { 0.361805, -0.723611, 0.587779 },
};

constexpr ushort3 SPHERE_INDEX[] = {
    { 0, 15, 14 },     { 1, 17, 23 },     { 0, 14, 29 },     { 0, 29, 35 },
    { 0, 35, 24 },     { 1, 23, 44 },     { 2, 20, 50 },     { 3, 32, 56 },
    { 4, 38, 62 },     { 5, 41, 68 },     { 1, 44, 51 },     { 2, 50, 57 },
    { 3, 56, 63 },     { 4, 62, 69 },     { 5, 68, 45 },     { 6, 74, 89 },
    { 7, 77, 95 },     { 8, 80, 98 },     { 9, 83, 101 },    { 10, 86, 90 },
    { 92, 99, 11 },    { 91, 102, 92 },   { 90, 103, 91 },   { 92, 102, 99 },
    { 102, 100, 99 },  { 91, 103, 102 },  { 103, 104, 102 }, { 102, 104, 100 },
    { 104, 101, 100 }, { 90, 86, 103 },   { 86, 85, 103 },   { 103, 85, 104 },
    { 85, 84, 104 },   { 104, 84, 101 },  { 84, 9, 101 },    { 99, 96, 11 },
    { 100, 105, 99 },  { 101, 106, 100 }, { 99, 105, 96 },   { 105, 97, 96 },
    { 100, 106, 105 }, { 106, 107, 105 }, { 105, 107, 97 },  { 107, 98, 97 },
    { 101, 83, 106 },  { 83, 82, 106 },   { 106, 82, 107 },  { 82, 81, 107 },
    { 107, 81, 98 },   { 81, 8, 98 },     { 96, 93, 11 },    { 97, 108, 96 },
    { 98, 109, 97 },   { 96, 108, 93 },   { 108, 94, 93 },   { 97, 109, 108 },
    { 109, 110, 108 }, { 108, 110, 94 },  { 110, 95, 94 },   { 98, 80, 109 },
    { 80, 79, 109 },   { 109, 79, 110 },  { 79, 78, 110 },   { 110, 78, 95 },
    { 78, 7, 95 },     { 93, 87, 11 },    { 94, 111, 93 },   { 95, 112, 94 },
    { 93, 111, 87 },   { 111, 88, 87 },   { 94, 112, 111 },  { 112, 113, 111 },
    { 111, 113, 88 },  { 113, 89, 88 },   { 95, 77, 112 },   { 77, 76, 112 },
    { 112, 76, 113 },  { 76, 75, 113 },   { 113, 75, 89 },   { 75, 6, 89 },
    { 87, 92, 11 },    { 88, 114, 87 },   { 89, 115, 88 },   { 87, 114, 92 },
    { 114, 91, 92 },   { 88, 115, 114 },  { 115, 116, 114 }, { 114, 116, 91 },
    { 116, 90, 91 },   { 89, 74, 115 },   { 74, 73, 115 },   { 115, 73, 116 },
    { 73, 72, 116 },   { 116, 72, 90 },   { 72, 10, 90 },    { 47, 86, 10 },
    { 46, 117, 47 },   { 45, 118, 46 },   { 47, 117, 86 },   { 117, 85, 86 },
    { 46, 118, 117 },  { 118, 119, 117 }, { 117, 119, 85 },  { 119, 84, 85 },
    { 45, 68, 118 },   { 68, 67, 118 },   { 118, 67, 119 },  { 67, 66, 119 },
    { 119, 66, 84 },   { 66, 9, 84 },     { 71, 83, 9 },     { 70, 120, 71 },
    { 69, 121, 70 },   { 71, 120, 83 },   { 120, 82, 83 },   { 70, 121, 120 },
    { 121, 122, 120 }, { 120, 122, 82 },  { 122, 81, 82 },   { 69, 62, 121 },
    { 62, 61, 121 },   { 121, 61, 122 },  { 61, 60, 122 },   { 122, 60, 81 },
    { 60, 8, 81 },     { 65, 80, 8 },     { 64, 123, 65 },   { 63, 124, 64 },
    { 65, 123, 80 },   { 123, 79, 80 },   { 64, 124, 123 },  { 124, 125, 123 },
    { 123, 125, 79 },  { 125, 78, 79 },   { 63, 56, 124 },   { 56, 55, 124 },
    { 124, 55, 125 },  { 55, 54, 125 },   { 125, 54, 78 },   { 54, 7, 78 },
    { 59, 77, 7 },     { 58, 126, 59 },   { 57, 127, 58 },   { 59, 126, 77 },
    { 126, 76, 77 },   { 58, 127, 126 },  { 127, 128, 126 }, { 126, 128, 76 },
    { 128, 75, 76 },   { 57, 50, 127 },   { 50, 49, 127 },   { 127, 49, 128 },
    { 49, 48, 128 },   { 128, 48, 75 },   { 48, 6, 75 },     { 53, 74, 6 },
    { 52, 129, 53 },   { 51, 130, 52 },   { 53, 129, 74 },   { 129, 73, 74 },
    { 52, 130, 129 },  { 130, 131, 129 }, { 129, 131, 73 },  { 131, 72, 73 },
    { 51, 44, 130 },   { 44, 43, 130 },   { 130, 43, 131 },  { 43, 42, 131 },
    { 131, 42, 72 },   { 42, 10, 72 },    { 66, 71, 9 },     { 67, 132, 66 },
    { 68, 133, 67 },   { 66, 132, 71 },   { 132, 70, 71 },   { 67, 133, 132 },
    { 133, 134, 132 }, { 132, 134, 70 },  { 134, 69, 70 },   { 68, 41, 133 },
    { 41, 40, 133 },   { 133, 40, 134 },  { 40, 39, 134 },   { 134, 39, 69 },
    { 39, 4, 69 },     { 60, 65, 8 },     { 61, 135, 60 },   { 62, 136, 61 },
    { 60, 135, 65 },   { 135, 64, 65 },   { 61, 136, 135 },  { 136, 137, 135 },
    { 135, 137, 64 },  { 137, 63, 64 },   { 62, 38, 136 },   { 38, 37, 136 },
    { 136, 37, 137 },  { 37, 36, 137 },   { 137, 36, 63 },   { 36, 3, 63 },
    { 54, 59, 7 },     { 55, 138, 54 },   { 56, 139, 55 },   { 54, 138, 59 },
    { 138, 58, 59 },   { 55, 139, 138 },  { 139, 140, 138 }, { 138, 140, 58 },
    { 140, 57, 58 },   { 56, 32, 139 },   { 32, 31, 139 },   { 139, 31, 140 },
    { 31, 30, 140 },   { 140, 30, 57 },   { 30, 2, 57 },     { 48, 53, 6 },
    { 49, 141, 48 },   { 50, 142, 49 },   { 48, 141, 53 },   { 141, 52, 53 },
    { 49, 142, 141 },  { 142, 143, 141 }, { 141, 143, 52 },  { 143, 51, 52 },
    { 50, 20, 142 },   { 20, 19, 142 },   { 142, 19, 143 },  { 19, 18, 143 },
    { 143, 18, 51 },   { 18, 1, 51 },     { 42, 47, 10 },    { 43, 144, 42 },
    { 44, 145, 43 },   { 42, 144, 47 },   { 144, 46, 47 },   { 43, 145, 144 },
    { 145, 146, 144 }, { 144, 146, 46 },  { 146, 45, 46 },   { 44, 23, 145 },
    { 23, 22, 145 },   { 145, 22, 146 },  { 22, 21, 146 },   { 146, 21, 45 },
    { 21, 5, 45 },     { 26, 41, 5 },     { 25, 147, 26 },   { 24, 148, 25 },
    { 26, 147, 41 },   { 147, 40, 41 },   { 25, 148, 147 },  { 148, 149, 147 },
    { 147, 149, 40 },  { 149, 39, 40 },   { 24, 35, 148 },   { 35, 34, 148 },
    { 148, 34, 149 },  { 34, 33, 149 },   { 149, 33, 39 },   { 33, 4, 39 },
    { 33, 38, 4 },     { 34, 150, 33 },   { 35, 151, 34 },   { 33, 150, 38 },
    { 150, 37, 38 },   { 34, 151, 150 },  { 151, 152, 150 }, { 150, 152, 37 },
    { 152, 36, 37 },   { 35, 29, 151 },   { 29, 28, 151 },   { 151, 28, 152 },
    { 28, 27, 152 },   { 152, 27, 36 },   { 27, 3, 36 },     { 27, 32, 3 },
    { 28, 153, 27 },   { 29, 154, 28 },   { 27, 153, 32 },   { 153, 31, 32 },
    { 28, 154, 153 },  { 154, 155, 153 }, { 153, 155, 31 },  { 155, 30, 31 },
    { 29, 14, 154 },   { 14, 13, 154 },   { 154, 13, 155 },  { 13, 12, 155 },
    { 155, 12, 30 },   { 12, 2, 30 },     { 21, 26, 5 },     { 22, 156, 21 },
    { 23, 157, 22 },   { 21, 156, 26 },   { 156, 25, 26 },   { 22, 157, 156 },
    { 157, 158, 156 }, { 156, 158, 25 },  { 158, 24, 25 },   { 23, 17, 157 },
    { 17, 16, 157 },   { 157, 16, 158 },  { 16, 15, 158 },   { 158, 15, 24 },
    { 15, 0, 24 },     { 12, 20, 2 },     { 13, 159, 12 },   { 14, 160, 13 },
    { 12, 159, 20 },   { 159, 19, 20 },   { 13, 160, 159 },  { 160, 161, 159 },
    { 159, 161, 19 },  { 161, 18, 19 },   { 14, 15, 160 },   { 15, 16, 160 },
    { 160, 16, 161 },  { 16, 17, 161 },   { 161, 17, 18 },   { 17, 1, 18 },
};

float dot(float3 a, float3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float3 normalize(float3 v) {
    float norm = std::sqrt(dot(v, v));

    return { v.x / norm, v.y / norm, v.z / norm };
}

static void _die(std::string_view message, int line) {
    spdlog::critical("Error @ {}: {}", line, message);
}

#define DIE(message) _die(message, __LINE__)

static std::vector<FPackedVertex> make_sphere() {
    std::vector<FVertexPNU> ret;
    ret.resize(std::size(SPHERE_POS));

    for (int i = 0; i < ret.size(); i++) {
        auto const& p = SPHERE_POS[i];

        auto position = p;
        auto normal   = normalize({ position.x, position.y, position.z });

        ret[i] = FVertexPNU {
            .position = position,
            .normal   = float3 { .x = normal.x, .y = normal.y, .z = normal.z },
            .uv       = { 0, 0 },
        };
    }

    std::vector<FPackedVertex> out;
    out.resize(ret.size());


    pack_vertex_u16(ret.data(),
                    ret.size(),
                    SPHERE_INDEX,
                    std::size(SPHERE_INDEX),
                    out.data());

    return out;
}

void setup_lights(FSession* session) {
    spdlog::info("Starting up testing lights...");

    constexpr float intensity = 1200000.0f;

    struct Spec {
        float3 position;
        float3 direction; // normalized
        FColor color;
        float  intensity; // lumens
        float  falloff;   // meters
        float  innerDeg, outerDeg;
    } specs[] = {
        // -X (red): place at +X, point toward -X (origin)
        { { 10.0f, 0.0f, 0.0f },
          float3 { -1, 0, 0 },
          { 1, 0, 0 },
          intensity,
          30.0f,
          20.0f,
          25.0f },

        // -Y (green): place above, point downward
        { { 0.0f, 10.0f, 0.0f },
          { 0, -1, 0 },
          { 0, 1, 0 },
          intensity,
          30.0f,
          20.0f,
          25.0f },

        // -Z (blue): place in front, point toward -Z
        { { 0.0f, 0.0f, 10.0f },
          { 0, 0, -1 },
          { 0, 0, 1 },
          intensity,
          30.0f,
          20.0f,
          25.0f },
    };

    constexpr auto DEG_TO_RAD = (M_PI / 180);


    for (const auto& s : specs) {
        spdlog::info("Adding light...");
        auto entity = fs_new_entity(session);

        auto light_config = flightconfig_init(SPOT);

        flc_set_intensity(light_config, s.intensity);
        flc_set_color(light_config, s.color);
        flc_set_falloff(light_config, s.falloff);
        flc_set_direction(light_config, s.direction);
        flc_set_spot_cone(
            light_config, DEG_TO_RAD * (s.innerDeg), DEG_TO_RAD * (s.outerDeg));
        flc_set_shadows(light_config, false);

        fs_add_light(session, entity, light_config);

        flightconfig_destroy(light_config);

        mat4 transform;
        mat4_identity(&transform);
        mat4_translate(&transform, s.position);

        spdlog::info("Transform {}", transform);

        fs_set_transform(session, entity, &transform);
    }
}

std::string find_and_set(std::vector<std::string> const& args, const char* flag) {
    for (int i = 0; i < args.size(); i++) {
        auto const& a = args[i];
        if (a == flag) {
            return args.at(i+1);
        }
    }
    return "";
}

FBlob* blob_from_file(std::string const& path) {
    spdlog::debug("Memmap {}...", path);
    if (!std::filesystem::exists(path)) { return nullptr; }


    int fd = open(path.c_str(), O_RDWR);
    if (fd == -1) {
        spdlog::error("Unable to read {}", path);
        return nullptr;
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        spdlog::error("Unable to read {}", path);
        return nullptr;
    }

    size_t file_size = sb.st_size;

    spdlog::debug("Memmap {}: {}", path, file_size);

    void* mapped_data =
        mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped_data == MAP_FAILED) {
        spdlog::error("Unable to map {}", path);
        return nullptr;
    }

    char* data_ptr = static_cast<char*>(mapped_data);

    auto* img_blob = fblob_init_copy(data_ptr, file_size);

    munmap(mapped_data, file_size);
    close(fd);

    return img_blob;
}

std::pair<FImage*, FImageFileInfo> image_from_file(std::string const& path) {
    auto img_blob = blob_from_file(path);

    if (!img_blob) {
        spdlog::error("Unable to create image");
        return std::make_pair(nullptr, FImageFileInfo {});
    }

    auto ref = fblobref_whole(img_blob);

    FImageFileInfo info { IMG_UNKNOWN };
    fimg_probe(ref, &info);

    auto* image = fimg_init_decode_file(ref);

    fblob_release(img_blob);

    return std::make_pair(image, info);
}

FTexture* texture_from_file(FSession*          session,
                            std::string const& path,
                            FTextureFormat     format) {
    spdlog::info("Loading image {}...", path);

    auto [img, info] = image_from_file(path);

    if (!img) { return nullptr; }

    auto* cfg = ftex_config_init(img, format);
    auto* tex = ftex_init(session, cfg);
    ftex_config_destroy(cfg);
    fimg_release(img);

    return tex;
}


void make_ground_plane(FSession* session, std::string const& img_root) {
    // Simple 2-triangle quad centered at origin on Y=0
    const FVertexPNU planeVerts[] = {
        { { -10.0f, 0.0f, -10.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
        { { 10.0f, 0.0f, -10.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
        { { 10.0f, 0.0f, 10.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f } },
        { { -10.0f, 0.0f, 10.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f } },
    };

    const ushort3 planeIdx[] = {
        { 0, 2, 1 },
        { 0, 3, 2 },
    };

    FPackedVertex planePacked[4];
    pack_vertex_u16(planeVerts, 4, planeIdx, 2, planePacked);

    auto* planeVBlob =
        fblob_init_copy((char const*)planePacked, sizeof(planePacked));
    auto* planeIBlob = fblob_init_copy((char const*)planeIdx, sizeof(planeIdx));

    auto* planeMesh =
        fmesh_init(session,
                   fblobref_whole(planeVBlob),
                   4,
                   fblobref_whole(planeIBlob),
                   6,
                   FMeshIndexType::U16,
                   aabb { { -10.0f, 0.0f, -10.0f }, { 10.0f, 0.0f, 10.0f } });

    // Material for the ground with textures
    auto planeMatCfg = fmaterialconfig_init();


    // Sampler: linear filtering + mipmaps, repeat
    Sampler samp;
    fsamp_init(&samp);
    fsamp_set_mag(&samp, MAG_FILTER_LINEAR);
    fsamp_set_min(&samp, MIN_FILTER_LINEAR_MIPMAP_LINEAR);
    fsamp_set_wrap(&samp, WRAP_REPEAT, AXIS_U);
    fsamp_set_wrap(&samp, WRAP_REPEAT, AXIS_V);
    fsamp_set_aniso(&samp, 4);

    // Base color (sRGB)
    {
        auto path = img_root + "/MetalPlates006_1K-JPG_Color.jpg";

        auto* tex = texture_from_file(session, path, FMT_AUTO_SRGB_COLOR);

        if (!tex) { DIE("Unable to load base color texture"); }

        fmc_set_texture(
            planeMatCfg, BASE_COLOR_TEX, FMatTexUVSlot::UV0, tex, &samp);

        ftex_release(tex);
    }


    // Normal map (linear)
    {
        auto path = img_root + "/MetalPlates006_1K-JPG_NormalGL.jpg";

        auto* tex = texture_from_file(session, path, FMT_AUTO_LINEAR_DATA);

        if (!tex) { DIE("Unable to load normal texture"); }

        fmc_set_texture(
            planeMatCfg, NORMAL_TEX, FMatTexUVSlot::UV0, tex, &samp);

        ftex_release(tex);
    }


    // Metallic-Roughness map (linear)

    {
        auto path = img_root + "/MetalPlates006_1K-JPG_RM.png";

        auto* tex = texture_from_file(session, path, FMT_AUTO_LINEAR_DATA);

        if (!tex) { DIE("Unable to load RM texture"); }

        fmc_set_texture(
            planeMatCfg, METAL_ROUGH_TEX, FMatTexUVSlot::UV0, tex, &samp);

        ftex_release(tex);
    }

    auto* planeMat = fmaterial_init(session, planeMatCfg);
    fmaterialconfig_destroy(planeMatCfg);

    fmaterial_set_base_color(planeMat, { .r = 1, .g = 1, .b = 1, .a = 1 });
    fmaterial_set_roughness_metallic(planeMat, 1.0, 0);

    auto planeEntity = fs_new_entity(session);
    fs_add_renderable(session, planeEntity, planeMesh, planeMat);
    // Plane is already at world Y=0; no transform needed.
}


int main(int argc, char** argv) {

    std::vector<std::string> arguments;
    for (int i = 1; i < argc; i++) {
        arguments.emplace_back(argv[i]);
    }

    // We intentionally don't clean anything up..
    spdlog::set_level(spdlog::level::debug);

    spdlog::info("Starting up...");


#if 1
    FScreenPlane plane {
        .lower_left  = { -2.5, 0, -1.768 },
        .lower_right = { 2.5, 0, -1.768 },
        .upper_right = { 2.5, 2.5, -1.768 },
    };
#else

    FScreenPlane plane {
        .lower_left  = { -2.5, 0, -0.1750 },
        .lower_right = { 2.5, 0, -0.1750 },
        .upper_right = { 2.5, 0, -1.768 },
    };
#endif

    auto* ptr = fconfig_init();

    fconfig_set_title(ptr, "Test Window");
    fconfig_set_screen(ptr, 1920, 1200);
    fconfig_set_offaxis_plane(ptr, &plane);
    fconfig_set_log_debug(ptr, 1);

    auto card = find_and_set(arguments, "-d");

    if (!card.empty()) {
        auto index = std::atoi(card.data());

        fconfig_set_device(ptr, index);
    }

    auto is_fullscreen = find_and_set(arguments, "-f");
    if (!is_fullscreen.empty()) {
        fconfig_set_fullscreen(ptr, 1);
    }

    auto* session = fs_init(ptr);

    fconfig_destroy(ptr);

    fs_set_postprocess(session, true);

    fs_set_skybox_color(session, { 0.1, 0.125, 0.25, 1.0 });

    auto mat_config = fmaterialconfig_init();

    fmc_set_option(mat_config, FMatTexOption::IOR, 1);
    fmc_set_option(mat_config, FMatTexOption::TRANSMISSION, 1);

    auto* mat = fmaterial_init(session, mat_config);

    fmaterialconfig_destroy(mat_config);

    fmaterial_set_base_color(mat, { 1, 1, 1, 1 });
    fmaterial_set_roughness_metallic(mat, .25, 0.0);
    fmaterial_set_ior(mat, 1.5);
    fmaterial_set_transmission(mat, .9);

    auto sphere = make_sphere();

    auto vertex_span = std::span(sphere);

    auto* vblob = fblob_init_copy((char const*)vertex_span.data(),
                                  vertex_span.size_bytes());

    auto* fblob = fblob_init_copy((char const*)SPHERE_INDEX,
                                  std::size(SPHERE_INDEX) * sizeof(ushort3));

    auto* mesh = fmesh_init(session,
                            fblobref_whole(vblob),
                            vertex_span.size(),
                            fblobref_whole(fblob),
                            std::size(SPHERE_INDEX) * 3,
                            FMeshIndexType::U16,
                            aabb {
                                .minimum = { -1, -1, -1 },
                                .maximum = { 1, 1, 1 },
                            });

    auto entity = fs_new_entity(session);

    fs_add_renderable(session, entity, mesh, mat);

    {
        // Move the entity up a bit
        mat4 transform;
        mat4_identity(&transform);
        mat4_translate(&transform, { 0, 1.5, 0 });

        fs_set_transform(session, entity, &transform);
    }

    // ---------------------------------------------------------------------
    // Add a large ground plane to receive shadows
    auto plane_asset = find_and_set(arguments, "-a");

    if (!plane_asset.empty()) { make_ground_plane(session, plane_asset); }


    // ---------------------------------------------------------------------
    // Add a downward directional light that casts shadows
    {
        auto dirLight = fs_new_entity(session);
        auto* lc      = flightconfig_init(DIRECTIONAL);
        // Sun-like brightness in lux; tweak as needed
        flc_set_intensity(lc, 100000.0f);
        flc_set_color(lc, { 1.0f, 1.0f, 1.0f, 1.0f });
        flc_set_direction(lc, { 0.0f, -1.0f, 0.0f });
        flc_set_shadows(lc, 1);
        fs_add_light(session, dirLight, lc);
        flightconfig_destroy(lc);
    }

    // setup_lights(session);

    bool debug_off_axis = !find_and_set(arguments, "-m").empty();


    auto maybe_image = find_and_set(arguments, "-i");

    if (!maybe_image.empty()) {

        auto [image, info] = image_from_file(maybe_image);

        if (!image) {
            spdlog::error("Unable to read envmap");
            return EXIT_FAILURE;
        }

        FTextureFormat desired_fmt =
            (info.kind == IMG_EXR || info.kind == IMG_HDR)
                ? FMT_R11F_G11F_B10F
                : FMT_AUTO_SRGB_COLOR;

        auto* texture_cfg = ftex_config_init(image, desired_fmt);

        auto* texture = ftex_init(session, texture_cfg);

        ftex_config_destroy(texture_cfg);

        auto* ibl = fenv_light_init_equirect(session, texture);

        ftex_release(texture);
        fimg_release(image);

        fs_set_environment_light(session, ibl);

        fenv_light_release(ibl);

        spdlog::info("Setting env light");
    }

    float debug_head = 0;


    auto prev_frame_time = std::chrono::high_resolution_clock::now();

    while (fs_frame(session)) {
        // Keep spinning

        auto frame_time = std::chrono::high_resolution_clock::now();

        auto duration =
            std::chrono::duration<double>(frame_time - prev_frame_time).count();

        float new_head_x = std::sin(debug_head) * 2.0f - 1.0f;
        // float new_head_x = 1.0;

        float3 head_pos = { new_head_x, 1.5f, 5.0f };
        float4 head_rot = { 0.0f, 0.0f, 0.0f, 1.0f };

        fs_update_head(session, head_pos, head_rot);

        debug_head += 1.0f * duration;

        prev_frame_time = frame_time;

        if (debug_off_axis) {
            fs_frame(session);
            fs_debug_camera_obj(session, "camera.obj");
            break;
        }
    }
}
