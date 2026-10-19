#include "UpdateButtonComponent.h"

#include "SketchTheme.h"

namespace bbg
{
namespace
{
constexpr auto kPageUrl = "https://hamloprod.org/en/vst";
constexpr auto kSupportEmail = "hello@hamloprod.org";
constexpr float kFontHeight = 10.0f;
constexpr int kMaxWidth = 150;

juce::String failureText(update::UpdateService::Failure failure)
{
    using Failure = update::UpdateService::Failure;
    switch (failure)
    {
        case Failure::InvalidKey: return "The license key was not accepted.";
        case Failure::Limit: return "The download limit for today is reached. Try again tomorrow.";
        case Failure::NoRelease: return "The new version is not available for download yet. Try again later.";
        case Failure::Untrusted: return "The download link points outside hamloprod.org, so HPDG did not use it.";
        case Failure::Checksum: return "The downloaded file did not match its checksum and was deleted. Try again.";
        case Failure::Disk: return "The installer could not be saved to the temporary folder.";
        case Failure::Network: return "hamloprod.org did not answer. Check the connection and try again.";
        case Failure::None:
        default: return {};
    }
}
} // namespace

void UpdateButtonComponent::Button::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const auto accent = sketch::Theme::blue();
    g.setColour(accent.withAlpha(down ? 0.38f : (highlighted ? 0.30f : 0.20f)));
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(accent.withAlpha(0.65f));
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
    g.setColour(isEnabled() ? accent.brighter(0.25f).withAlpha(0.95f) : accent.withAlpha(0.6f));
    g.setFont(juce::Font(juce::FontOptions(kFontHeight)));
    g.drawFittedText(getButtonText(), getLocalBounds().reduced(3, 0), juce::Justification::centred, 1, 0.8f);
}

UpdateButtonComponent::UpdateButtonComponent()
{
    button.onAnyClick = [this](bool popupMenu)
    {
        if (popupMenu)
            showMenu();
        else
            clicked();
    };
    addAndMakeVisible(button);
    refresh();
}

UpdateButtonComponent::~UpdateButtonComponent()
{
    if (service.has_value())
        (*service)->removeChangeListener(this);
}

void UpdateButtonComponent::enable()
{
    if (service.has_value())
        return;
    service.emplace();
    (*service)->addChangeListener(this);
    seenDownloads = (*service)->snapshot().finishedDownloads;
    (*service)->checkOnce();
    refresh();
}

juce::String UpdateButtonComponent::getMenuItemText() const
{
    if (!service.has_value())
        return "Update";
    const auto snapshot = (*service)->snapshot();
    const auto version = snapshot.latest.version.toDisplayString();
    switch (snapshot.state)
    {
        case State::Checking: return "Update (checking...)";
        case State::UpToDate: return "Update (up to date: " + update::currentDisplayVersion() + ")";
        case State::Offline: return "Update (offline - try again)";
        case State::UpdateAvailable:
        case State::Failed: return "Update to " + version;
        case State::Downloading: return "Update (downloading " + juce::String(juce::roundToInt(snapshot.progress * 100.0f)) + "%)";
        case State::ReadyToInstall: return "Install " + version;
        case State::Idle:
        default: return "Update (check for a new version)";
    }
}

bool UpdateButtonComponent::isMenuItemEnabled() const
{
    return button.isEnabled();
}

int UpdateButtonComponent::getPreferredWidth() const
{
    const auto font = juce::Font(juce::FontOptions(kFontHeight));
    return juce::jmin(kMaxWidth, juce::GlyphArrangement::getStringWidthInt(font, button.getButtonText()) + 10);
}

void UpdateButtonComponent::resized()
{
    button.setBounds(getLocalBounds());
}

void UpdateButtonComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refresh();
}

void UpdateButtonComponent::refresh()
{
    const auto snapshot = service.has_value() ? (*service)->snapshot() : update::UpdateService::Snapshot {};
    const auto version = snapshot.latest.version.toDisplayString();

    juce::String text, tooltip;
    switch (snapshot.state)
    {
        case State::Idle:
            text = "Update";
            tooltip = "Check hamloprod.org for a new HPDG version. Right-click: license key.";
            break;
        case State::Checking:
            text = "...";
            tooltip = "Asking hamloprod.org for the newest version.";
            break;
        case State::UpToDate:
            text = "Latest";
            tooltip = "HPDG " + update::currentDisplayVersion() + " is the newest version. Click to check again.";
            break;
        case State::Offline:
            text = "Offline";
            tooltip = "hamloprod.org did not answer. HPDG works as usual; click to try again.";
            break;
        case State::UpdateAvailable:
        case State::Failed:
            text = "Update: " + version;
            tooltip = "Installed: HPDG " + update::currentDisplayVersion() + "\n";
            if (snapshot.state == State::Failed)
                tooltip << failureText(snapshot.failure) << "\nClick to try again.";
            else if (snapshot.latest.notes.isNotEmpty())
                tooltip << snapshot.latest.notes;
            else
                tooltip << "Click to download and install " << snapshot.latest.title << ".";
            break;
        case State::Downloading:
            text = juce::String(juce::roundToInt(snapshot.progress * 100.0f)) + "%";
            tooltip = "Downloading HPDG " + version + ". HPDG keeps working meanwhile.";
            break;
        case State::ReadyToInstall:
            text = "Install: " + version;
            tooltip = "Installed: HPDG " + update::currentDisplayVersion() + "\nDownloaded and verified. Click to start the installer.";
            break;
    }

    const bool widthChanged = text != button.getButtonText();
    button.setButtonText(text);
    button.setTooltip(tooltip);
    button.setEnabled(snapshot.state != State::Checking && snapshot.state != State::Downloading);
    if (widthChanged && onPreferredWidthChanged)
        onPreferredWidthChanged();

    // React once to each finished download.
    if (snapshot.finishedDownloads != seenDownloads)
    {
        seenDownloads = snapshot.finishedDownloads;
        if (snapshot.state == State::ReadyToInstall)
            confirmInstall();
        else if (snapshot.state == State::Failed)
            reportFailure(snapshot.failure);
    }
}

void UpdateButtonComponent::clicked()
{
    if (!service.has_value())
        return;

    const auto snapshot = (*service)->snapshot();
    switch (snapshot.state)
    {
        case State::Idle:
        case State::UpToDate:
        case State::Offline:
            (*service)->checkNow();
            break;
        case State::UpdateAvailable:
        case State::Failed:
        {
            const auto key = licenseStore.load();
            if (key.isEmpty())
                askForKey("HPDG " + snapshot.latest.version.toDisplayString()
                              + " is available.\n\nEnter the license key from your purchase email to download and install it.",
                          {});
            else
                (*service)->download(key);
            break;
        }
        case State::ReadyToInstall:
            confirmInstall();
            break;
        case State::Checking:
        case State::Downloading:
            break;
    }
}

void UpdateButtonComponent::showMenu()
{
    const bool hasKey = licenseStore.load().isNotEmpty();
    const bool connected = service.has_value();

    juce::PopupMenu menu;
    menu.addItem(1, "Check for updates", connected);
    menu.addItem(2, hasKey ? "Change license key..." : "Enter license key...");
    menu.addItem(3, "Remove license key", hasKey);
    menu.addSeparator();
    menu.addItem(4, "Open the HPDG page");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&button),
                       [safeThis = juce::Component::SafePointer<UpdateButtonComponent>(this)](int result)
                       {
                           if (safeThis == nullptr)
                               return;
                           if (result == 1 && safeThis->service.has_value())
                               (*safeThis->service)->checkNow();
                           else if (result == 2)
                               safeThis->askForKey("Enter the license key from your HPDG purchase email.",
                                                   safeThis->licenseStore.load());
                           else if (result == 3)
                               safeThis->licenseStore.clear();
                           else if (result == 4)
                               safeThis->pageUrl().launchInDefaultBrowser();
                       });
}

juce::AlertWindow* UpdateButtonComponent::makeWindow(const juce::String& title, const juce::String& message)
{
    auto* window = new juce::AlertWindow(title, message, juce::MessageBoxIconType::NoIcon, this);
    window->setLookAndFeel(&getLookAndFeel());
    window->setColour(juce::AlertWindow::backgroundColourId, sketch::Theme::paperLight());
    window->setColour(juce::AlertWindow::textColourId, sketch::Theme::graphite());
    window->setColour(juce::AlertWindow::outlineColourId, sketch::Theme::graphiteSoft());
    return window;
}

void UpdateButtonComponent::askForKey(const juce::String& message, const juce::String& prefill)
{
    if (dialogOpen)
        return;
    dialogOpen = true;

    auto* window = makeWindow("HPDG license key", message + "\nNo key yet? HPDG is sold on hamloprod.org.");
    window->addTextEditor("key", prefill, "HPDG-XXXX-XXXX-XXXX-XXXX");
    window->addButton("Download", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Buy HPDG", 2);
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [safeThis = juce::Component::SafePointer<UpdateButtonComponent>(this), window](int result)
        {
            if (safeThis == nullptr)
                return;
            safeThis->dialogOpen = false;

            if (result == 2)
            {
                safeThis->pageUrl().launchInDefaultBrowser();
                return;
            }
            if (result != 1)
                return;

            const auto typed = window->getTextEditorContents("key");
            const auto key = update::normalizeLicenseKey(typed);
            if (key.isEmpty())
            {
                juce::MessageManager::callAsync([safeThis, typed]
                {
                    if (safeThis != nullptr)
                        safeThis->askForKey("That is not an HPDG license key. It looks like HPDG-XXXX-XXXX-XXXX-XXXX.", typed);
                });
                return;
            }
            if (!safeThis->licenseStore.save(key))
            {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "HPDG update",
                                                       "The key could not be saved in "
                                                           + update::LicenseStore::defaultDirectory().getFullPathName() + ".");
                return;
            }
            if (!safeThis->service.has_value())
                return;
            const auto state = (*safeThis->service)->snapshot().state;
            if (state == State::UpdateAvailable || state == State::Failed)
                (*safeThis->service)->download(key);
        }),
        true);
}

void UpdateButtonComponent::confirmInstall()
{
    if (dialogOpen || !service.has_value())
        return;
    dialogOpen = true;

    const auto version = (*service)->snapshot().latest.version.toDisplayString();
    auto* window = makeWindow("Install HPDG " + version,
                              "The installer is downloaded and verified.\n\n"
                              "Save your project first. The installer asks to close your DAW; after the installation "
                              "open the DAW again and the header shows HamloProd " + version + ".");
    window->addButton("Run installer", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Later", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [safeThis = juce::Component::SafePointer<UpdateButtonComponent>(this)](int result)
        {
            if (safeThis == nullptr)
                return;
            safeThis->dialogOpen = false;
            if (result == 1 && safeThis->service.has_value() && !(*safeThis->service)->launchInstaller())
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "HPDG update",
                                                       "The installer could not be started. It is in "
                                                           + (*safeThis->service)->snapshot().installer.getFullPathName() + ".");
        }),
        true);
}

void UpdateButtonComponent::reportFailure(Failure failure)
{
    if (failure == Failure::InvalidKey)
    {
        const auto oldKey = licenseStore.load();
        licenseStore.clear(); // a refused key is not tried again on the next click
        askForKey(failureText(failure) + " Check it in your purchase email or write to " + kSupportEmail + ".", oldKey);
        return;
    }

    auto message = failureText(failure);
    if (failure == Failure::Limit || failure == Failure::NoRelease)
        message << "\nQuestions: " << kSupportEmail;
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "HPDG update", message, "OK", this);
}

juce::URL UpdateButtonComponent::pageUrl() const
{
    if (service.has_value())
    {
        const auto snapshot = (*service)->snapshot();
        if (snapshot.latest.pageUrl.isWellFormed())
            return snapshot.latest.pageUrl;
    }
    return juce::URL(kPageUrl);
}
} // namespace bbg
