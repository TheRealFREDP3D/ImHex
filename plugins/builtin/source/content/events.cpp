#include <hex/api/event.hpp>

#include <hex/api/localization.hpp>
#include <hex/api/project_file_manager.hpp>
#include <hex/api/achievement_manager.hpp>

#include <hex/providers/provider.hpp>
#include <hex/ui/view.hpp>
#include <hex/helpers/logger.hpp>

#include <imgui.h>

#include <content/providers/file_provider.hpp>

#include <wolv/io/fs.hpp>
#include <wolv/utils/string.hpp>

#include <content/popups/popup_notification.hpp>
#include <content/popups/popup_question.hpp>
#include <content/popups/popup_tasks_waiting.hpp>
#include <content/popups/popup_unsaved_changes.hpp>

namespace hex::plugin::builtin {

    static void openFile(const std::fs::path &path) {
        auto provider = ImHexApi::Provider::createProvider("hex.builtin.provider.file", true);
        if (auto *fileProvider = dynamic_cast<FileProvider*>(provider); fileProvider != nullptr) {
            fileProvider->setPath(path);
            if (!provider->open() || !provider->isAvailable()) {
                PopupError::open(hex::format("hex.builtin.provider.error.open"_lang, provider->getErrorMessage()));
                TaskManager::doLater([provider] { ImHexApi::Provider::remove(provider); });
            } else {
                EventManager::post<EventProviderOpened>(fileProvider);
            }
        }
    }

    void registerEventHandlers() {

        static bool imhexClosing = false;
        EventManager::subscribe<EventWindowClosing>([](GLFWwindow *window) {
            if (ImHexApi::Provider::isDirty() && !imhexClosing) {
                glfwSetWindowShouldClose(window, GLFW_FALSE);
                PopupQuestion::open("hex.builtin.popup.exit_application.desc"_lang,
                    [] {
                        imhexClosing = true;
                        for (const auto &provider : auto(ImHexApi::Provider::getProviders()))
                            ImHexApi::Provider::remove(provider);
                    },
                    [] { }
                );
            } else if (TaskManager::getRunningTaskCount() > 0 || TaskManager::getRunningBackgroundTaskCount() > 0) {
                glfwSetWindowShouldClose(window, GLFW_FALSE);
                TaskManager::doLater([] {
                    for (auto &task : TaskManager::getRunningTasks())
                        task->interrupt();
                    PopupTasksWaiting::open();
                });
            }
        });

        EventManager::subscribe<EventProviderClosing>([](hex::prv::Provider *provider, bool *shouldClose) {
            if (provider->isDirty()) {
                *shouldClose = false;
                PopupUnsavedChanges::open("hex.builtin.popup.close_provider.desc"_lang,
                                    []{
                                        for (auto &provider : ImHexApi::Provider::impl::getClosingProviders())
                                            ImHexApi::Provider::remove(provider, true);
                                    },
                                    [] {
                                        ImHexApi::Provider::impl::resetClosingProvider();
                                        imhexClosing = false;
                                    }
                );
            }
        });

        EventManager::subscribe<EventProviderChanged>([](hex::prv::Provider *oldProvider, hex::prv::Provider *newProvider) {
            hex::unused(oldProvider);
            hex::unused(newProvider);

            EventManager::post<RequestUpdateWindowTitle>();
        });

        EventManager::subscribe<EventProviderOpened>([](hex::prv::Provider *provider) {
            if (provider != nullptr && ImHexApi::Provider::get() == provider)
                EventManager::post<RequestUpdateWindowTitle>();
            EventManager::post<EventProviderChanged>(nullptr, provider);
        });

        EventManager::subscribe<RequestOpenFile>(openFile);

        EventManager::subscribe<RequestOpenWindow>([](const std::string &name) {
            if (name == "Create File") {
                auto newProvider = hex::ImHexApi::Provider::createProvider("hex.builtin.provider.mem_file", true);
                if (newProvider != nullptr && !newProvider->open())
                    hex::ImHexApi::Provider::remove(newProvider);
                else
                    EventManager::post<EventProviderOpened>(newProvider);
            } else if (name == "Open File") {
                fs::openFileBrowser(fs::DialogMode::Open, { }, [](const auto &path) {
                    if (path.extension() == ".hexproj") {
                        if (!ProjectFile::load(path)) {
                            PopupError::open(hex::format("hex.builtin.popup.error.project.load"_lang, wolv::util::toUTF8String(path)));
                        }
                    } else {
                        FileProvider* newProvider = static_cast<FileProvider*>(
                            ImHexApi::Provider::createProvider("hex.builtin.provider.file", true)
                        );

                        if (newProvider == nullptr)
                            return;

                        newProvider->setPath(path);
                        if (!newProvider->open())
                            hex::ImHexApi::Provider::remove(newProvider);
                        else {
                            EventManager::post<EventProviderOpened>(newProvider);
                            AchievementManager::unlockAchievement("hex.builtin.achievement.starting_out", "hex.builtin.achievement.starting_out.open_file.name");
                        }

                    }
                });
            } else if (name == "Open Project") {
                fs::openFileBrowser(fs::DialogMode::Open, { {"Project File", "hexproj"} },
                    [](const auto &path) {
                        if (!ProjectFile::load(path)) {
                            PopupError::open(hex::format("hex.builtin.popup.error.project.load"_lang, wolv::util::toUTF8String(path)));
                        }
                    });
            }
        });

        EventManager::subscribe<EventProviderChanged>([](auto, auto) {
            EventManager::post<EventHighlightingChanged>();
        });

        // Handles the provider initialization, and calls EventProviderOpened if successful
        EventManager::subscribe<EventProviderCreated>([](hex::prv::Provider *provider) {    
            if (provider->shouldSkipLoadInterface())
                return;

            if (provider->hasFilePicker()) {
                if (!provider->handleFilePicker()) {
                    TaskManager::doLater([provider] { ImHexApi::Provider::remove(provider); });
                    return;
                }
                if (!provider->open()) {
                    PopupError::open(hex::format("hex.builtin.provider.error.open"_lang, provider->getErrorMessage()));
                    TaskManager::doLater([provider] { ImHexApi::Provider::remove(provider); });
                    return;
                }

                EventManager::post<EventProviderOpened>(provider);
            }
            else if (provider->hasLoadInterface())
                EventManager::post<RequestOpenPopup>(View::toWindowName("hex.builtin.view.provider_settings.load_popup"));
            else {
                if (!provider->open() || !provider->isAvailable()) {
                    PopupError::open(hex::format("hex.builtin.provider.error.open"_lang, provider->getErrorMessage()));
                    TaskManager::doLater([provider] { ImHexApi::Provider::remove(provider); });
                    return;
                }

                EventManager::post<EventProviderOpened>(provider);
            }
        });

        EventManager::subscribe<EventRegionSelected>([](const ImHexApi::HexEditor::ProviderRegion &region) {
           ImHexApi::HexEditor::impl::setCurrentSelection(region);
        });

        EventManager::subscribe<RequestOpenInfoPopup>([](const std::string &message) {
            PopupInfo::open(message);
        });

        EventManager::subscribe<RequestOpenErrorPopup>([](const std::string &message) {
            PopupError::open(message);
        });

        EventManager::subscribe<RequestOpenFatalPopup>([](const std::string &message) {
            PopupFatal::open(message);
        });

        fs::setFileBrowserErrorCallback([](const std::string& errMsg){
            #if defined(NFD_PORTAL)
                PopupError::open(hex::format("hex.builtin.popup.error.file_dialog.portal"_lang, errMsg));
            #else
                PopupError::open(hex::format("hex.builtin.popup.error.file_dialog.common"_lang, errMsg));
            #endif
        });

    }

}
