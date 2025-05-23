#pragma once

#include "ScreenModules.h"
#include "MenuSystem.h"
#include <vector>
#include <memory>
#include <string>
#include <map>

/**
 * A screen module that displays a submenu of other screen modules
 * Supports nested menu navigation
 */
class MenuScreenModule : public ScreenModule, public ScreenCallback {
public:
    MenuScreenModule(std::shared_ptr<Display> display, std::shared_ptr<InputDevice> input,
                    const std::string& id, const std::string& title);
    ~MenuScreenModule();

    // Override ScreenModule methods
    void enter() override;
    void update() override;
    void exit() override;
    bool handleInput() override;
    std::string getModuleId() const override { return m_id; }

    // MenuScreenModule specific methods
    void addSubmenuItem(const std::string& moduleId, const std::string& title);
    void setModuleRegistry(const std::map<std::string, std::shared_ptr<ScreenModule>>* registry);
    void setParentMenu(MenuScreenModule* parent) { m_parentMenu = parent; }
    bool hasSubmenuItems() const { return !m_submenuItems.empty(); }
    void navigateToMainMenu();
    void setAsTopLevelMenu(bool isTopLevel) { m_isTopLevelMenu = isTopLevel; }
    bool isExitingToMainMenu() const { return m_exitToMainMenu; }
    void clearMainMenuFlag() { m_exitToMainMenu = false; }
    void onScreenAction(const std::string& screenId,
                      const std::string& action,
                      const std::string& value) override;

private:
    struct SubmenuItem {
        std::string moduleId;
        std::string title;
    };

    std::string m_id;
    std::string m_title;
    std::shared_ptr<Menu> m_menu;
    std::vector<SubmenuItem> m_submenuItems;
    const std::map<std::string, std::shared_ptr<ScreenModule>>* m_moduleRegistry = nullptr;
    MenuScreenModule* m_parentMenu = nullptr;
    bool m_exitToParent = false;
    bool m_exitToMainMenu = false;
    bool m_isTopLevelMenu = false;  // New flag to identify top level menu

    void buildSubmenu();
    void executeSubmenuAction(const std::string& moduleId);
    std::map<std::string, std::string> m_callbackValues;
};
