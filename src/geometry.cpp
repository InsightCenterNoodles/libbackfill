#include "geometry.h"

#include <filament/IndexBuffer.h>
#include <geometry/SurfaceOrientation.h>
#include <math/TVecHelpers.h>
#include <math/mat3.h>
#include <math/norm.h>

using namespace filament::math;


inline short4 packFilamentTangent(float3 n, float3 t, float handedness) {
    t        = normalize(t);
    n        = normalize(n);
    float3 b = normalize(handedness * cross(n, t));
    // Columns are T, B, N
    const mat3f tbn { t, b, n };
    // Convert to quaternion in the way Filament expects, then pack to snorm16.
    const quatf q = mat3f::packTangentFrame(tbn);
    return packSnorm16(float4 { q.x, q.y, q.z, q.w });
}

static_assert(sizeof(Vertex) == 32);
static_assert(sizeof(PackedVertex) == 24);

void PackedVertex::setup(filament::VertexBuffer::Builder& vb) {
    vb.attribute(filament::VertexAttribute::POSITION,
                 0,
                 filament::VertexBuffer::AttributeType::FLOAT3,
                 offsetof(PackedVertex, position),
                 sizeof(PackedVertex))


        .attribute(filament::VertexAttribute::TANGENTS,
                   0,
                   filament::VertexBuffer::AttributeType::SHORT4,
                   offsetof(PackedVertex, surface),
                   sizeof(PackedVertex))
        .normalized(filament::VertexAttribute::TANGENTS)

        .attribute(filament::VertexAttribute::UV0,
                   0,
                   filament::VertexBuffer::AttributeType::USHORT2,
                   offsetof(PackedVertex, texture),
                   sizeof(PackedVertex))
        .normalized(filament::VertexAttribute::UV0);
}

template <class T>
void vert_compress_common(std::span<const Vertex> src,
                          std::span<const T>      index,
                          std::span<PackedVertex> out) {
    auto vertex_count = src.size();

    expect(out.size() == src.size(), "Mismatched source and dest");

    // Well, we cant use strides in the builder yet. So, yes, we have to copy.

    // TODO: Check when we can get non-zero stride support

    auto positions = std::vector<float3>(src.size());
    auto normals   = std::vector<float3>(src.size());
    auto uvs       = std::vector<float2>(src.size());

    for (size_t i = 0; i < src.size(); i++) {
        positions[i] = src[i].position;
        normals[i]   = src[i].normal;
        uvs[i]       = src[i].texture;
    }

    auto* triangles = index.data();


    filament::geometry::SurfaceOrientation::Builder sb;
    sb.vertexCount(vertex_count)
        .positions(positions.data())
        .normals(normals.data())
        .uvs(uvs.data())
        .triangleCount(index.size())
        .triangles(triangles);

    auto so = sb.build();

    // Get the quaternions, then pack to SHORT4 (snorm).
    std::vector<quatf> quats(vertex_count);
    so->getQuats(quats.data(), vertex_count);

    delete so;

    for (size_t i = 0; i < vertex_count; ++i) {
        out[i].position = src[i].position;
        out[i].texture  = src[i].texture;
        // pack quaternion to SHORT4 (snorm16)
        const float4 q =
            float4 { quats[i].x, quats[i].y, quats[i].z, quats[i].w };
        out[i].surface = packSnorm16(q);
    }
}

void vert_compress(std::span<const Vertex>  src,
                   std::span<const ushort3> index,
                   std::span<PackedVertex>  out) {
    vert_compress_common(src, index, out);
}

void vert_compress(std::span<const Vertex>                src,
                   std::span<const filament::math::uint3> index,
                   std::span<PackedVertex>                out) {
    vert_compress_common(src, index, out);
}


void LocalVertexBuffer::completion(void* buffer, size_t size, void* user) {
    auto* ptr = (LocalVertexBuffer*)user;

    ptr->m_pending_upload = {};
}

LocalVertexBuffer::LocalVertexBuffer(filament::Engine* engine,
                                     Bytes             buffer,
                                     size_t            vertex_count) {
    m_pending_upload = buffer;

    auto vb = filament::VertexBuffer::Builder()
                  .vertexCount(vertex_count)
                  .bufferCount(1);

    PackedVertex::setup(vb);

    m_vertex_buffer = make_wrapper(engine, vb.build(*engine));

    m_vertex_buffer->setBufferAt(
        *engine,
        0,
        filament::VertexBuffer::BufferDescriptor((const char*)buffer.data(),
                                                 buffer.span().size_bytes(),
                                                 completion,
                                                 this));
}

void LocalIndexBuffer::completion(void* buffer, size_t size, void* user) {
    auto* ptr = (LocalIndexBuffer*)user;

    ptr->m_pending_upload = {};
}

LocalIndexBuffer::LocalIndexBuffer(filament::Engine* engine,
                                   Bytes             content,
                                   size_t            index_count,
                                   IndexType         type)
    : m_index_count(index_count) {

    filament::IndexBuffer::IndexType t_type;

    switch (type) {

    case IndexType::U16:
        t_type = filament::IndexBuffer::IndexType::USHORT;
        break;
    case IndexType::U32: t_type = filament::IndexBuffer::IndexType::UINT; break;
    }


    m_pending_upload = content;

    auto* ib = filament::IndexBuffer::Builder()
                   .indexCount(index_count)
                   .bufferType(t_type)
                   .build(*engine);


    m_index_buffer = make_wrapper(engine, ib);

    m_index_buffer->setBuffer(
        *engine,
        filament::IndexBuffer::BufferDescriptor((const char*)content.data(),
                                                content.span().size_bytes(),
                                                completion,
                                                this));
}
