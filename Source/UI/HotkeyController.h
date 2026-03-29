#pragma once

#include <algorithm>
#include <functional>
#include <vector>

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "GridEditorComponent.h"

namespace bbg
{
class HotkeyController
{
public:
    struct Binding
    {
        juce::String actionId;
        juce::String displayName;
        juce::KeyPress keyPress;
        bool usesMouseModifier = false;
        juce::ModifierKeys::Flags mouseModifier = juce::ModifierKeys::noModifiers;
        juce::KeyPress defaultKeyPress;
        juce::ModifierKeys::Flags defaultMouseModifier = juce::ModifierKeys::noModifiers;
    };

    using ConflictConfirmFn = std::function<bool(const juce::String&, const juce::String&, const juce::String&)>;

    static constexpr auto kHotkeysRootProp = "ui.hotkeys";
    static constexpr auto kActionDrawMode = "draw_mode";
    static constexpr auto kActionVelocityEdit = "velocity_edit";
    static constexpr auto kActionPanView = "pan_view";
    static constexpr auto kActionHorizontalZoomModifier = "horizontal_zoom_modifier";
    static constexpr auto kActionLaneHeightZoomModifier = "lane_height_zoom_modifier";
    static constexpr auto kActionPencilTool = "tool_pencil";
    static constexpr auto kActionBrushTool = "tool_brush";
    static constexpr auto kActionSelectTool = "tool_select";
    static constexpr auto kActionCutTool = "tool_cut";
    static constexpr auto kActionEraseTool = "tool_erase";
    static constexpr auto kActionStretchResize = "stretch_resize";
    static constexpr auto kActionMicrotimingEdit = "microtiming_edit";
    static constexpr auto kActionDeleteNote = "delete_note";
    static constexpr auto kActionCopySelection = "copy_selection";
    static constexpr auto kActionCutSelection = "cut_selection";
    static constexpr auto kActionPasteSelection = "paste_selection";
    static constexpr auto kActionDuplicateSelection = "duplicate_selection";
    static constexpr auto kActionDeselectAll = "deselect_all";
    static constexpr auto kActionSelectAllNotes = "select_all_notes";
    static constexpr auto kActionUndo = "undo";
    static constexpr auto kActionRedo = "redo";
    static constexpr auto kActionRoleAnchor = "semantic_role_anchor";
    static constexpr auto kActionRoleSupport = "semantic_role_support";
    static constexpr auto kActionRoleFill = "semantic_role_fill";
    static constexpr auto kActionRoleAccent = "semantic_role_accent";
    static constexpr auto kActionRoleClear = "semantic_role_clear";
    static constexpr auto kActionZoomToSelection = "zoom_to_selection";
    static constexpr auto kActionZoomToPattern = "zoom_to_pattern";

    void setupDefaults()
    {
        bindings = {
            { kActionDrawMode, "Draw Mode", {}, true, defaultModifierForAction(kActionDrawMode), {}, defaultModifierForAction(kActionDrawMode) },
            { kActionVelocityEdit, "Velocity Edit", defaultKeyForAction(kActionVelocityEdit), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionVelocityEdit), juce::ModifierKeys::noModifiers },
            { kActionPanView, "Pan View", {}, true, defaultModifierForAction(kActionPanView), {}, defaultModifierForAction(kActionPanView) },
            { kActionHorizontalZoomModifier, "Horizontal Zoom modifier", {}, true, defaultModifierForAction(kActionHorizontalZoomModifier), {}, defaultModifierForAction(kActionHorizontalZoomModifier) },
            { kActionLaneHeightZoomModifier, "Lane Height Zoom modifier", {}, true, defaultModifierForAction(kActionLaneHeightZoomModifier), {}, defaultModifierForAction(kActionLaneHeightZoomModifier) },
            { kActionPencilTool, "Pencil Tool", defaultKeyForAction(kActionPencilTool), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionPencilTool), juce::ModifierKeys::noModifiers },
            { kActionBrushTool, "Brush Tool", defaultKeyForAction(kActionBrushTool), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionBrushTool), juce::ModifierKeys::noModifiers },
            { kActionSelectTool, "Select Tool", defaultKeyForAction(kActionSelectTool), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionSelectTool), juce::ModifierKeys::noModifiers },
            { kActionCutTool, "Cut Tool", defaultKeyForAction(kActionCutTool), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionCutTool), juce::ModifierKeys::noModifiers },
            { kActionEraseTool, "Erase Tool", defaultKeyForAction(kActionEraseTool), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionEraseTool), juce::ModifierKeys::noModifiers },
            { kActionStretchResize, "Resize notes", defaultKeyForAction(kActionStretchResize), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionStretchResize), juce::ModifierKeys::noModifiers },
            { kActionMicrotimingEdit, "Microtiming Edit modifier", {}, true, defaultModifierForAction(kActionMicrotimingEdit), {}, defaultModifierForAction(kActionMicrotimingEdit) },
            { kActionDeleteNote, "Delete Note", defaultKeyForAction(kActionDeleteNote), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionDeleteNote), juce::ModifierKeys::noModifiers },
            { kActionCopySelection, "Copy", defaultKeyForAction(kActionCopySelection), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionCopySelection), juce::ModifierKeys::noModifiers },
            { kActionCutSelection, "Cut selection", defaultKeyForAction(kActionCutSelection), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionCutSelection), juce::ModifierKeys::noModifiers },
            { kActionPasteSelection, "Paste", defaultKeyForAction(kActionPasteSelection), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionPasteSelection), juce::ModifierKeys::noModifiers },
            { kActionDuplicateSelection, "Duplicate", defaultKeyForAction(kActionDuplicateSelection), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionDuplicateSelection), juce::ModifierKeys::noModifiers },
            { kActionSelectAllNotes, "Select all notes", defaultKeyForAction(kActionSelectAllNotes), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionSelectAllNotes), juce::ModifierKeys::noModifiers },
            { kActionDeselectAll, "Deselect all", defaultKeyForAction(kActionDeselectAll), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionDeselectAll), juce::ModifierKeys::noModifiers },
            { kActionUndo, "Undo", defaultKeyForAction(kActionUndo), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionUndo), juce::ModifierKeys::noModifiers },
            { kActionRedo, "Redo", defaultKeyForAction(kActionRedo), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionRedo), juce::ModifierKeys::noModifiers },
            { kActionRoleAnchor, "Semantic role: Anchor", defaultKeyForAction(kActionRoleAnchor), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionRoleAnchor), juce::ModifierKeys::noModifiers },
            { kActionRoleSupport, "Semantic role: Support", defaultKeyForAction(kActionRoleSupport), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionRoleSupport), juce::ModifierKeys::noModifiers },
            { kActionRoleFill, "Semantic role: Fill", defaultKeyForAction(kActionRoleFill), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionRoleFill), juce::ModifierKeys::noModifiers },
            { kActionRoleAccent, "Semantic role: Accent", defaultKeyForAction(kActionRoleAccent), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionRoleAccent), juce::ModifierKeys::noModifiers },
            { kActionRoleClear, "Semantic role: Clear", defaultKeyForAction(kActionRoleClear), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionRoleClear), juce::ModifierKeys::noModifiers },
            { kActionZoomToSelection, "Zoom to selection", defaultKeyForAction(kActionZoomToSelection), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionZoomToSelection), juce::ModifierKeys::noModifiers },
            { kActionZoomToPattern, "Zoom to pattern", defaultKeyForAction(kActionZoomToPattern), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionZoomToPattern), juce::ModifierKeys::noModifiers }
        };
    }

    const std::vector<Binding>& getBindings() const
    {
        return bindings;
    }

    void load(juce::ValueTree& state)
    {
        for (auto& binding : bindings)
        {
            if (binding.usesMouseModifier)
            {
                const auto path = hotkeyPropertyPath(binding.actionId, "modifier");
                if (state.hasProperty(path))
                    binding.mouseModifier = static_cast<juce::ModifierKeys::Flags>(static_cast<int>(state.getProperty(path)));
                continue;
            }

            const auto keyPath = hotkeyPropertyPath(binding.actionId, "keyCode");
            const auto modsPath = hotkeyPropertyPath(binding.actionId, "modifiers");
            if (!state.hasProperty(keyPath) || !state.hasProperty(modsPath))
            {
                binding.keyPress = binding.defaultKeyPress;
                continue;
            }

            const int keyCode = static_cast<int>(state.getProperty(keyPath));
            const int mods = static_cast<int>(state.getProperty(modsPath));
            const bool migrateStretchResize = binding.actionId == kActionStretchResize
                && keyCode == binding.defaultKeyPress.getKeyCode()
                && mods == 0
                && binding.defaultKeyPress.getModifiers().getRawFlags() != 0;
            const bool migrateLegacyUndo = binding.actionId == kActionUndo
                && keyCode == 'U'
                && mods == juce::ModifierKeys::ctrlModifier;
            const bool migrateLegacyRedo = binding.actionId == kActionRedo
                && keyCode == 'R'
                && mods == juce::ModifierKeys::ctrlModifier;

            if (migrateStretchResize || migrateLegacyUndo || migrateLegacyRedo)
            {
                binding.keyPress = binding.defaultKeyPress;
                state.setProperty(keyPath, binding.keyPress.getKeyCode(), nullptr);
                state.setProperty(modsPath, binding.keyPress.getModifiers().getRawFlags(), nullptr);
            }
            else
            {
                binding.keyPress = keyCode > 0 ? juce::KeyPress(keyCode, juce::ModifierKeys(mods), 0) : juce::KeyPress();
            }
        }
    }

    void save(juce::ValueTree& state) const
    {
        for (const auto& binding : bindings)
        {
            if (binding.usesMouseModifier)
            {
                state.setProperty(hotkeyPropertyPath(binding.actionId, "modifier"), static_cast<int>(binding.mouseModifier), nullptr);
                continue;
            }

            state.setProperty(hotkeyPropertyPath(binding.actionId, "keyCode"), binding.keyPress.getKeyCode(), nullptr);
            state.setProperty(hotkeyPropertyPath(binding.actionId, "modifiers"), binding.keyPress.getModifiers().getRawFlags(), nullptr);
        }
    }

    void restoreDefaults()
    {
        for (auto& binding : bindings)
        {
            binding.keyPress = binding.defaultKeyPress;
            binding.mouseModifier = binding.defaultMouseModifier;
        }
    }

    GridEditorComponent::InputBindings makeGridInputBindings() const
    {
        GridEditorComponent::InputBindings inputBindings;
        inputBindings.drawModeModifier = mouseModifierFor(kActionDrawMode, inputBindings.drawModeModifier);
        inputBindings.velocityEditKeyCode = keyCodeFor(kActionVelocityEdit, inputBindings.velocityEditKeyCode);
        inputBindings.stretchNoteKeyCode = keyCodeFor(kActionStretchResize, inputBindings.stretchNoteKeyCode);
        inputBindings.stretchNoteModifiers = modifiersFor(kActionStretchResize, inputBindings.stretchNoteModifiers);
        inputBindings.panViewModifier = mouseModifierFor(kActionPanView, inputBindings.panViewModifier);
        inputBindings.horizontalZoomModifier = mouseModifierFor(kActionHorizontalZoomModifier, inputBindings.horizontalZoomModifier);
        inputBindings.laneHeightZoomModifier = mouseModifierFor(kActionLaneHeightZoomModifier, inputBindings.laneHeightZoomModifier);
        inputBindings.microtimingEditModifier = mouseModifierFor(kActionMicrotimingEdit, inputBindings.microtimingEditModifier);
        return inputBindings;
    }

    bool matchesKeyAction(const juce::String& actionId, const juce::KeyPress& keyPress) const
    {
        const auto* binding = findBinding(actionId);
        return binding != nullptr && !binding->usesMouseModifier && binding->keyPress.isValid() && binding->keyPress == keyPress;
    }

    juce::String hotkeyToDisplayText(const Binding& binding) const
    {
        if (binding.usesMouseModifier)
        {
            if (binding.mouseModifier == juce::ModifierKeys::shiftModifier)
                return "Shift + mouse";
            if (binding.mouseModifier == juce::ModifierKeys::ctrlModifier)
                return "Ctrl + mouse";
            if (binding.mouseModifier == juce::ModifierKeys::altModifier)
                return "Alt + mouse";
            if (binding.mouseModifier == juce::ModifierKeys::commandModifier)
                return "Cmd + mouse";
            return "(none)";
        }

        return binding.keyPress.isValid() ? binding.keyPress.getTextDescription() : "(none)";
    }

    bool tryRebindKeyAction(const juce::String& actionId, const juce::KeyPress& keyPress, const ConflictConfirmFn& confirmReplaceConflict)
    {
        auto* targetBinding = findBinding(actionId);
        if (targetBinding == nullptr || targetBinding->usesMouseModifier)
            return false;

        auto* conflictBinding = findConflictForKeyAction(actionId, keyPress);
        if (conflictBinding != nullptr)
        {
            if (!confirmReplaceConflict(targetBinding->displayName,
                                        conflictBinding->displayName,
                                        keyPress.getTextDescription()))
                return false;

            conflictBinding->keyPress = conflictBinding->defaultKeyPress;
        }

        targetBinding->keyPress = keyPress;
        return true;
    }

    bool tryRebindMouseModifierAction(const juce::String& actionId,
                                      juce::ModifierKeys::Flags modifier,
                                      const ConflictConfirmFn& confirmReplaceConflict)
    {
        auto* targetBinding = findBinding(actionId);
        if (targetBinding == nullptr || !targetBinding->usesMouseModifier)
            return false;

        auto* conflictBinding = findConflictForMouseModifierAction(actionId, modifier);
        if (conflictBinding != nullptr)
        {
            if (!confirmReplaceConflict(targetBinding->displayName,
                                        conflictBinding->displayName,
                                        hotkeyToDisplayText({ {}, {}, {}, true, modifier, {}, juce::ModifierKeys::noModifiers })))
                return false;

            conflictBinding->mouseModifier = conflictBinding->defaultMouseModifier;
        }

        targetBinding->mouseModifier = modifier;
        return true;
    }

    bool resetActionToDefault(const juce::String& actionId)
    {
        auto* binding = findBinding(actionId);
        if (binding == nullptr)
            return false;

        binding->keyPress = binding->defaultKeyPress;
        binding->mouseModifier = binding->defaultMouseModifier;
        return true;
    }

private:
    static juce::ModifierKeys::Flags defaultModifierForAction(const juce::String& actionId)
    {
        if (actionId == kActionDrawMode)
            return juce::ModifierKeys::shiftModifier;
        if (actionId == kActionPanView)
            return juce::ModifierKeys::ctrlModifier;
        if (actionId == kActionHorizontalZoomModifier)
            return juce::ModifierKeys::ctrlModifier;
        if (actionId == kActionLaneHeightZoomModifier)
            return juce::ModifierKeys::shiftModifier;
        if (actionId == kActionMicrotimingEdit)
            return juce::ModifierKeys::altModifier;
        return juce::ModifierKeys::noModifiers;
    }

    static juce::KeyPress defaultKeyForAction(const juce::String& actionId)
    {
        if (actionId == kActionVelocityEdit)
            return juce::KeyPress('V');
        if (actionId == kActionPencilTool)
            return juce::KeyPress('1');
        if (actionId == kActionBrushTool)
            return juce::KeyPress('2');
        if (actionId == kActionSelectTool)
            return juce::KeyPress('3');
        if (actionId == kActionCutTool)
            return juce::KeyPress('4');
        if (actionId == kActionEraseTool)
            return juce::KeyPress('5');
        if (actionId == kActionStretchResize)
            return juce::KeyPress('R', juce::ModifierKeys::shiftModifier, 0);
        if (actionId == kActionDeleteNote)
            return juce::KeyPress(juce::KeyPress::deleteKey);
        if (actionId == kActionCopySelection)
            return juce::KeyPress('C', juce::ModifierKeys::ctrlModifier, 0);
        if (actionId == kActionCutSelection)
            return juce::KeyPress('X', juce::ModifierKeys::ctrlModifier, 0);
        if (actionId == kActionPasteSelection)
            return juce::KeyPress('V', juce::ModifierKeys::ctrlModifier, 0);
        if (actionId == kActionDuplicateSelection)
            return juce::KeyPress('D', juce::ModifierKeys::ctrlModifier, 0);
        if (actionId == kActionSelectAllNotes)
            return juce::KeyPress('A', juce::ModifierKeys::ctrlModifier, 0);
        if (actionId == kActionDeselectAll)
            return juce::KeyPress(juce::KeyPress::escapeKey);
        if (actionId == kActionUndo)
            return juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0);
        if (actionId == kActionRedo)
            return juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0);
        if (actionId == kActionRoleAnchor)
            return juce::KeyPress('1', juce::ModifierKeys::altModifier, 0);
        if (actionId == kActionRoleSupport)
            return juce::KeyPress('2', juce::ModifierKeys::altModifier, 0);
        if (actionId == kActionRoleFill)
            return juce::KeyPress('3', juce::ModifierKeys::altModifier, 0);
        if (actionId == kActionRoleAccent)
            return juce::KeyPress('4', juce::ModifierKeys::altModifier, 0);
        if (actionId == kActionRoleClear)
            return juce::KeyPress('0', juce::ModifierKeys::altModifier, 0);
        if (actionId == kActionZoomToSelection)
            return juce::KeyPress('F', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0);
        if (actionId == kActionZoomToPattern)
            return juce::KeyPress('P', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0);
        return juce::KeyPress();
    }

    static juce::String hotkeyPropertyPath(const juce::String& actionId, const juce::String& suffix)
    {
        return juce::String(kHotkeysRootProp) + "." + actionId + "." + suffix;
    }

    const Binding* findBinding(const juce::String& actionId) const
    {
        const auto it = std::find_if(bindings.begin(), bindings.end(), [&actionId](const Binding& binding)
        {
            return binding.actionId == actionId;
        });

        return it != bindings.end() ? &*it : nullptr;
    }

    Binding* findBinding(const juce::String& actionId)
    {
        const auto it = std::find_if(bindings.begin(), bindings.end(), [&actionId](const Binding& binding)
        {
            return binding.actionId == actionId;
        });

        return it != bindings.end() ? &*it : nullptr;
    }

    Binding* findConflictForKeyAction(const juce::String& actionId, const juce::KeyPress& keyPress)
    {
        const auto it = std::find_if(bindings.begin(), bindings.end(), [&actionId, &keyPress](const Binding& binding)
        {
            return binding.actionId != actionId
                && !binding.usesMouseModifier
                && binding.keyPress.isValid()
                && binding.keyPress == keyPress;
        });

        return it != bindings.end() ? &*it : nullptr;
    }

    Binding* findConflictForMouseModifierAction(const juce::String& actionId, juce::ModifierKeys::Flags modifier)
    {
        const auto it = std::find_if(bindings.begin(), bindings.end(), [&actionId, modifier](const Binding& binding)
        {
            return binding.actionId != actionId
                && binding.usesMouseModifier
                && binding.mouseModifier == modifier;
        });

        return it != bindings.end() ? &*it : nullptr;
    }

    int keyCodeFor(const juce::String& actionId, int fallback) const
    {
        const auto* binding = findBinding(actionId);
        return binding != nullptr && binding->keyPress.isValid() ? binding->keyPress.getKeyCode() : fallback;
    }

    juce::ModifierKeys::Flags modifiersFor(const juce::String& actionId, juce::ModifierKeys::Flags fallback) const
    {
        const auto* binding = findBinding(actionId);
        return binding != nullptr && !binding->usesMouseModifier && binding->keyPress.isValid()
            ? static_cast<juce::ModifierKeys::Flags>(binding->keyPress.getModifiers().getRawFlags())
            : fallback;
    }

    juce::ModifierKeys::Flags mouseModifierFor(const juce::String& actionId, juce::ModifierKeys::Flags fallback) const
    {
        const auto* binding = findBinding(actionId);
        return binding != nullptr && binding->usesMouseModifier ? binding->mouseModifier : fallback;
    }

    std::vector<Binding> bindings;
};
} // namespace bbg