#pragma once

#include "utility.h"

#include <math/vec3.h>

#include <filament/Engine.h>
#include <filament/VertexBuffer.h>

/// Unpacked vertex information
struct Vertex {
    filament::math::float3 position;
    filament::math::float3 normal;
    filament::math::float2 texture;
};

/// Packed vertex suitable for upload to the GPU
struct PackedVertex {
    filament::math::float3  position;
    filament::math::short4  surface;
    filament::math::ushort2 texture;

    static void setup(filament::VertexBuffer::Builder&);
};

void vert_compress(std::span<const Vertex>                  src,
                   std::span<const filament::math::ushort3> index,
                   std::span<PackedVertex>                  out);

/// A vertex buffer, with async upload
class LocalVertexBuffer {
    Bytes                             m_pending_upload;
    UResource<filament::VertexBuffer> m_vertex_buffer;

    static void completion(void* buffer, size_t size, void* user);

public:
    DISABLE_MOVE_COPY(LocalVertexBuffer);

    LocalVertexBuffer(filament::Engine* engine,
                      Bytes             content,
                      size_t            vertex_count);

    filament::VertexBuffer* vertex_buffer() const {
        return m_vertex_buffer.get();
    }
};

enum class IndexType { U16, U32 };

/// An index buffer, with async upload
class LocalIndexBuffer {
    Bytes                            m_pending_upload;
    UResource<filament::IndexBuffer> m_index_buffer;
    size_t                           m_index_count;

    static void completion(void* buffer, size_t size, void* user);

public:
    DISABLE_MOVE_COPY(LocalIndexBuffer);

    LocalIndexBuffer(filament::Engine* engine,
                     Bytes             content,
                     size_t            index_count,
                     IndexType         type);

    size_t index_count() const { return m_index_count; }

    filament::IndexBuffer* index_buffer() const { return m_index_buffer.get(); }
};
