#pragma once

#include "utility.h"


namespace filament {
class Engine;
}


class LocalEngine {
    filament::Engine* m_pointer;

public:
    DISABLE_MOVE_COPY(LocalEngine);

    LocalEngine();

    ~LocalEngine();

    filament::Engine* operator->() { return m_pointer; }

    operator filament::Engine*() const { return m_pointer; }
};
