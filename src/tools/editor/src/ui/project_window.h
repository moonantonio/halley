#pragma once

#include "console_window.h"
#include "halley/tools/dll/dynamic_library.h"
#include "halley/ui/ui_widget.h"
#include "src/editor_root_stage.h"
#include "halley/tools/project/project.h"
#include "src/assets/assets_browser.h"
#include "src/scene/entity_editor.h"
#include "src/scene/choose_window.h"

namespace Halley {
	class Preferences;
	class UIFactory;
    class HalleyEditor;
	class Toolbar;
	class Project;
	class AssetFileHandler;
        
    class ProjectWindow final : public UIWidget, public IProjectDLLListener, public Project::IAssetLoadListener, public IProjectWindow
    {
    public:
        ProjectWindow(EditorUIFactory& factory, HalleyEditor& editor, Project& project, Resources& resources, const HalleyAPI& api);
    	~ProjectWindow() override;

        void makeUI();

    	void onRemovedFromRoot(UIRoot& root) override;
    	
        void setPage(EditorTabs tab);
        LocalisedString setCustomPage(const String& pageId);
    	void openFile(const String& assetId);
    	void showFile(const String& assetId);
    	void openAsset(AssetType type, const String& assetId, bool inEditor);
        void replaceAssetTab(AssetType oldType, const String& oldId, AssetType newType, const String& newId);

    	const HalleyAPI& getAPI() const;
        Project& getProject();

    	TaskSet& getTasks() const;

    	void reloadProject();

    	void addTask(std::unique_ptr<Task> task) override;

    	const ConfigNode& getSetting(EditorSettingType type, std::string_view id) const override;
        void setSetting(EditorSettingType type, std::string_view id, ConfigNode data) override;
        const ConfigNode& getAssetSetting(std::string_view assetKey, std::string_view id);
    	void setAssetSetting(std::string_view assetKey, std::string_view id, ConfigNode data);

		void openFileExternally(const Path& path);
		void showFileExternally(const Path& path);

    	bool requestQuit(std::function<void()> callback);
        bool onQuitRequested();
        void closeProject();

        AssetPreviewGenerator& getAssetPreviewGenerator();
        Future<AssetPreviewData> getAssetPreviewData(AssetType assetType, const String& id, Vector2i size);

        void render(RenderContext& rc) const override;

    	Vector2f getChoosePrefabWindowSize() const;

    	EntityEditorFactoryRoot& getEntityEditorFactoryRoot();
    	EntityEditorFactoryRoot& getHalleyEntityEditorFactoryRoot();
		std::shared_ptr<ScriptNodeTypeCollection> getScriptNodeTypes();

        void buildGame() override;
        void updateEditor() override;

    	void openAssetFinder(std::optional<String> initialQuery);

        UIDebugConsoleController* getDebugConsoleController();

        Preferences& getPreferences() const;
        const AssetFileHandler& getAssetFileHandler() const;

    	Vector<String> getLaunchArguments() const override;

        std::shared_ptr<UIWidget> makeMoveFilesTool(UIFactory& factory) override;

    	int getPaletteWindowResultsLimit() const;

    protected:
        void onProjectDLLStatusChange(ProjectDLL::Status status) override;

        void onAssetsLoaded() override;

        void update(Time t, bool moved) override;
        bool onKeyPress(KeyboardKeyPress key) override;
    	
    private:
		class SettingsStorage {
		public:
			SettingsStorage(std::shared_ptr<ISaveData> saveData, String path);
			~SettingsStorage();

			bool save() const;
			void load();

			void setData(std::string_view key, ConfigNode data);
			const ConfigNode& getData(std::string_view key) const;
			ConfigNode& getMutableData(std::string_view key);
		
		private:
			ConfigFile data;
			std::shared_ptr<ISaveData> saveData;
			String path;
			mutable bool dirty = false;
		};
    	
		constexpr static int numOfStandardTools = 8;

    	EditorUIFactory& factory;
        HalleyEditor& editor;
    	Project& project;
    	Resources& resources;
    	const HalleyAPI& api;
		std::unique_ptr<AssetFileHandler> assetFileHandler;

		std::shared_ptr<UIWidget> uiTop;
		std::shared_ptr<UIWidget> uiMid;
		std::shared_ptr<UIWidget> uiBottom;
		std::shared_ptr<Toolbar> toolbar;
		std::shared_ptr<UIPagedPane> pagedPane;

		std::unique_ptr<TaskSet> tasks;

		Vector<IEditorCustomTools::ToolData> customTools;
    	bool waitingToLoadCustomUI = true;
    	bool hasAssets = false;
    	bool hasDLL = false;
        bool firstDLLLoad = true;

    	std::shared_ptr<AssetsBrowser> assetEditorWindow;
        std::shared_ptr<ConsoleWindow> consoleWindow;
        std::shared_ptr<PaletteWindow> assetFinder;

        std::shared_ptr<EntityEditorFactoryRoot> entityEditorFactoryRoot;
        std::shared_ptr<EntityEditorFactoryRoot> halleyEntityEditorFactoryRoot;
        
        std::unique_ptr<AssetPreviewGenerator> assetPreviewGenerator;

    	std::shared_ptr<UIDebugConsoleController> debugConsoleController;
    	std::shared_ptr<UIDebugConsoleCommands> debugConsoleCommands;
        std::shared_ptr<UIDebugConsole> debugConsole;
        
    	std::map<EditorSettingType, std::unique_ptr<SettingsStorage>> settings;
    	Time timeSinceSettingsSaved = 0;

    	void makeToolbar();
    	void makePagedPane();

    	void tryLoadCustomUI();
    	bool loadCustomUI();
		void destroyCustomUI();

        void toggleDebugConsole();
    	void updateDLLStatus(ProjectDLL::Status status);
        void reloadDLL();

        void setupConsole(Game& game);
        void destroyConsole();

    	void doUpdateEditor();
	};
}
