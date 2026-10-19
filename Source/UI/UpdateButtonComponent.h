#pragma once

#include <functional>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Services/UpdateCheckService.h"

namespace bbg
{
// The small permanent "Update" button next to "HamloProd <version>" in the header.
// Click: check hamloprod.org -> "Latest" / "Update: <version>" / "Offline". With an update
// known, a click downloads it with the stored license key (asks for the key once), verifies
// the SHA-256 and, after a confirmation, starts the installer. Right-click: check, key, page.
// The same action is the first item of the HPDG menu (click on "HPDG" in the header).
class UpdateButtonComponent : public juce::Component,
                              private juce::ChangeListener
{
public:
    UpdateButtonComponent();
    ~UpdateButtonComponent() override;

    // Connects to the site (only inside a real host; snapshot and demo tools stay offline) and
    // runs the one automatic check of this plugin process.
    void enable();

    // The "Update" item of the HPDG menu: its text follows the state, choosing it acts like a click.
    juce::String getMenuItemText() const;
    bool isMenuItemEnabled() const;
    void activate() { clicked(); }

    int getPreferredWidth() const;
    void resized() override;

    std::function<void()> onPreferredWidthChanged;

private:
    using State = update::UpdateService::State;
    using Failure = update::UpdateService::Failure;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refresh();
    void clicked();
    void showMenu();
    void askForKey(const juce::String& message, const juce::String& prefill);
    void confirmInstall();
    void reportFailure(Failure failure);
    juce::AlertWindow* makeWindow(const juce::String& title, const juce::String& message);
    juce::URL pageUrl() const;

    // Tells a right-click (the menu) from a left-click.
    struct Button : juce::TextButton
    {
        std::function<void(bool popupMenu)> onAnyClick;
        void clicked(const juce::ModifierKeys& modifiers) override
        {
            if (onAnyClick)
                onAnyClick(modifiers.isPopupMenu());
        }
        // The row is 12 px high: the look-and-feel's button font would be unreadable there.
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    };

    Button button;
    std::optional<juce::SharedResourcePointer<update::UpdateService>> service;
    update::LicenseStore licenseStore;
    int seenDownloads = 0;
    bool dialogOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpdateButtonComponent)
};
} // namespace bbg
