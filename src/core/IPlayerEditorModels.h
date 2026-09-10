#pragma once

#include "core/SequencerTypes.h"

namespace lps
{

class IPatternEditorModel
{
public:
    virtual ~IPatternEditorModel() = default;

    [[nodiscard]] virtual PatternView patternView() const noexcept = 0;
    [[nodiscard]] virtual PatternPlaybackSnapshot patternPlaybackSnapshot()
        const noexcept = 0;
};

class IModulationEditorModel
{
public:
    virtual ~IModulationEditorModel() = default;

    [[nodiscard]] virtual ModulationPlaybackSnapshot modulationPlaybackSnapshot()
        const noexcept = 0;
};

} // namespace lps
