#pragma once

#include "config.h"
#include "utility.h"


namespace filament {
class Engine;
}


/// Our implementation of the Filament engine
class LocalEngine {
    filament::Engine*            m_pointer        = nullptr;
    filament::backend::Platform* m_custom_backend = nullptr;

public:
    DISABLE_MOVE_COPY(LocalEngine);

    LocalEngine(FConfig const& config);

    ~LocalEngine();

    filament::Engine* operator->() { return m_pointer; }

    operator filament::Engine*() const { return m_pointer; }
};
