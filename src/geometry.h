#pragma once

#include "utility.h"

#include <math/vec3.h>

#include <filament/Engine.h>
#include <filament/VertexBuffer.h>

struct Vertex {
    filament::math::float3 position;
    filament::math::float3 normal;
    filament::math::float2 texture;
};

struct PackedVertex {
    filament::math::float3 position;
    filament::math::short4 tangent;
    filament::math::ushort2 texture;

    static void setup(filament::VertexBuffer::Builder&);
};

SharedArray<PackedVertex>            sphere_verts();
SharedArray<filament::math::ushort3> sphere_index();


class LocalVertexBuffer {
    SharedArray<PackedVertex>         m_pending_upload;
    UResource<filament::VertexBuffer> m_vertex_buffer;

    static void completion(void* buffer, size_t size, void* user);

public:
    LocalVertexBuffer(filament::Engine* engine, SharedArray<PackedVertex>);

    filament::VertexBuffer* vertex_buffer() const {
        return m_vertex_buffer.get();
    }
};

class LocalIndexBuffer {
    SharedArray<filament::math::ushort3> m_pending_upload;
    UResource<filament::IndexBuffer>     m_index_buffer;
    size_t                               m_index_count;

    static void completion(void* buffer, size_t size, void* user);

public:
    LocalIndexBuffer(filament::Engine* engine,
                     SharedArray<filament::math::ushort3>);

    size_t index_count() const { return m_index_count; }

    filament::IndexBuffer* index_buffer() const { return m_index_buffer.get(); }
};
