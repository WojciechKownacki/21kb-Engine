add_executable(kb_editor WIN32
    include/kb/editor/EditorApplication.hpp
    include/kb/editor/assets/EditorAssetBrowserTypes.hpp
    include/kb/editor/docking/DockTypes.hpp
    include/kb/editor/theme/EditorTheme.hpp
    src/app/EditorApplication.cpp
    src/app/EditorApplicationLifecycle.cpp
    src/app/EditorWorkspaceSession.cpp
    src/app/EditorCrashBreadcrumbs.cpp
    src/app/EditorApplicationMessageDispatcher.cpp
    src/app/EditorApplicationMessageLoop.cpp
    src/app/EditorApplicationWindowProc.cpp
    src/app/EditorAutomationScenarioRunner.cpp
    src/app/EditorSelfTest.cpp
    src/app/EditorHeadlessAutomation.cpp
    src/app/EditorEditCommandPolicy.cpp
    src/app/EditorEditCommandInputHandler.cpp
    src/app/EditorAssetBrowserContextCommandExecutor.cpp
    src/private/app/EditorAssetBrowserNativeCommandMap.hpp
    src/app/EditorAssetBrowserNativeCommandMap.cpp
    src/private/app/EditorAudioAssetPreview.hpp
    src/app/EditorAudioAssetPreview.cpp
    src/scene/audio/EditorAudioMixerRuntime.cpp
    src/inspection/InspectorAudioMixerRuntimeInteraction.cpp
    src/app/EditorAudioAssetInspectorDropHandler.cpp
    src/app/EditorAudioInspectorDropPolicy.cpp
    src/app/EditorAnimatorAssetInspectorDropHandler.cpp
    src/app/EditorAssetBrowserContextMenuPointerHandler.cpp
    src/app/EditorAssetBrowserDeleteConfirmPointerHandler.cpp
    src/app/EditorAssetBrowserDoubleClickHandler.cpp
    src/private/app/EditorExternalCodeLauncher.hpp
    src/app/EditorExternalCodeLauncher.cpp
    src/private/app/EditorScriptFileWatcher.hpp
    src/app/EditorAssetBrowserInputHandler.cpp
    src/app/EditorBuildGameInputHandler.cpp
    src/private/app/ParticleEditorPanelInteraction.hpp
    src/app/ParticleEditorPanelInteraction.cpp
    src/app/EditorAssetBrowserPointerHandler.cpp
    src/app/EditorAssetBrowserPointerPanelResolver.cpp
    src/app/EditorAssetBrowserPrimaryClickHandler.cpp
    src/app/EditorAssetBrowserThumbnailScaleDragHandler.cpp
    src/app/EditorAssetDropActionMenu.cpp
    src/app/EditorAssetProjectFilesDropActionExecutor.cpp
    src/app/EditorKeyState.cpp
    src/app/EditorTextInputShortcuts.cpp
    src/app/console/EditorConsolePointerController.cpp
    src/app/cursor/EditorInternalSplitterCursorController.cpp
    src/app/docking/EditorMainDockSplitterPointerController.cpp
    src/app/EditorAssetFolderProjectFilesDropHandler.cpp
    src/app/EditorBehaviourAssetHierarchyDropHandler.cpp
    src/app/EditorBehaviourAssetInspectorDropHandler.cpp
    src/app/EditorBehaviourAssetSceneDropHandler.cpp
    src/app/EditorDropPanelResolver.cpp
    src/app/EditorHierarchyEntityAssetDropHandler.cpp
    src/app/EditorHierarchyEntityHierarchyDropHandler.cpp
    src/app/EditorMaterialAssetInspectorDropHandler.cpp
    src/app/EditorMaterialAssetSceneDropHandler.cpp
    src/app/EditorMeshAssetInspectorDropHandler.cpp
    src/app/EditorMeshAssetSceneDropHandler.cpp
    src/app/EditorParticleAssetSceneDropHandler.cpp
    src/app/EditorPrefabAssetSceneDropHandler.cpp
    src/app/EditorHierarchySearchInputHandler.cpp
    src/app/EditorSkeletalMeshTreeSearchInputHandler.cpp
    src/app/EditorMouseWheelRouter.cpp
    src/settings/EditorBuildGameSettingsStore.cpp
    src/packaging/EditorProjectPackageService.cpp
    src/packaging/EditorAndroidSigningBroker.cpp
    src/packaging/EditorPackageProcessEnvironment.cpp
    src/packaging/EditorProjectIconTransaction.cpp
    src/packaging/EditorSigningProcess.cpp
    src/packaging/EditorWindowsSigningBroker.cpp
    src/app/inspector/EditorInspectorPointerController.cpp
    src/app/plugins/EditorPluginsPointerController.cpp
    src/app/EditorWindowLifecycleHandler.cpp
    src/app/EditorParticleDocumentLifecycle.cpp
    src/app/EditorWindowHitTestHandler.cpp
    src/app/EditorPaintDispatcher.cpp
    src/rendering/MaterialPreviewPresentSettings.cpp
    src/app/EditorPendingTextEditCommitter.cpp
    src/app/EditorPlayModeState.cpp
    src/app/EditorPlayModeSceneSession.cpp
    src/app/EditorSceneFileCommands.cpp
    src/app/EditorSceneLifecycleGuard.cpp
    src/app/EditorShellInteractionState.cpp
    src/app/EditorPointerDragInteraction.cpp
    src/app/EditorPointerDragSourceResolver.cpp
    src/app/EditorPointerDropHandler.cpp
    src/app/EditorPrefabAssetHierarchyDropHandler.cpp
    src/app/EditorPrefabAssetProjectFilesDropHandler.cpp
    src/app/panels/EditorPanelPointerHitContext.cpp
    src/app/pointer/EditorLeftButtonDoubleClickRouter.cpp
    src/app/pointer/EditorLeftButtonDownRouter.cpp
    src/app/pointer/EditorLeftButtonUpRouter.cpp
    src/app/pointer/EditorMouseMoveRouter.cpp
    src/app/pointer/EditorRightButtonDownRouter.cpp
    src/app/pointer/EditorSetCursorRouter.cpp
    src/app/project_files/EditorProjectFilesDeleteConfirmOverlayController.cpp
    src/app/project_files/EditorProjectFilesMouseWheelController.cpp
    src/app/project_files/EditorProjectFilesTransientUiController.cpp
    src/app/project_settings/EditorProjectSettingsPointerController.cpp
    src/app/editor_settings/EditorSettingsPointerController.cpp
    src/app/scene_viewport/EditorSceneViewportCameraController.cpp
    src/app/scene_viewport/EditorSceneViewportGizmoDragSolver.cpp
    src/app/scene_viewport/EditorTerrainViewportInteraction.cpp
    src/app/scene_viewport/EditorSceneViewportGizmoHitTester.cpp
    src/app/scene_viewport/EditorSceneViewportGizmoInteraction.cpp
    src/app/scene_viewport/gizmo/EditorSceneViewportGizmoAltDuplicate.cpp
    src/app/scene_viewport/gizmo/EditorSceneViewportGizmoDragState.cpp
    src/app/scene_viewport/gizmo/EditorSceneViewportGizmoDragUpdater.cpp
    src/app/scene_viewport/gizmo/EditorSceneViewportGizmoRotationDrag.cpp
    src/app/scene_viewport/gizmo/EditorSceneViewportGizmoTargetResolver.cpp
    src/app/scene_viewport/gizmo/EditorSceneViewportGizmoToolBehavior.cpp
    src/app/scene_viewport/EditorSceneViewportHitResolver.cpp
    src/app/scene_viewport/EditorSceneViewportMath.cpp
    src/app/scene_viewport/EditorSceneViewportAssetDragPreview.cpp
    src/app/scene_viewport/EditorSceneViewportMeshPicker.cpp
    src/app/scene_viewport/EditorUIRectInteraction.cpp
    src/private/app/scene_viewport/EditorUIRectInteraction.hpp
    src/private/scene/EditorUIRectDragState.hpp
    src/app/scene_viewport/EditorSceneViewportObjectInteraction.cpp
    src/app/scene_viewport/EditorSceneViewportSelectionController.cpp
    src/app/scene_viewport/EditorSceneViewportSelectionInteraction.cpp
    src/app/scene_viewport/EditorSceneViewportToolbarPointerController.cpp
    src/app/EditorWindowPointerMessageDispatcher.cpp
    src/app/EditorWindowPointerHandler.cpp
    src/app/EditorWindowToolbarPointerHandler.cpp
    src/app/EditorWindowInvalidator.cpp
    src/app/EditorWindowResizeInteraction.cpp
    src/app/EditorWindowResizeHandler.cpp
    src/app/EditorWindowMessageRouter.cpp
    src/app/main.cpp
    src/project/EditorProjectAssetIndex.cpp
    src/project/EditorProjectBootstrap.cpp
    src/project/EditorProjectPanelHitTester.cpp
    src/project/EditorProjectPaths.cpp
    src/assets/EditorAssetBrowserAssetRows.cpp
    src/assets/EditorAssetBrowserBreadcrumbHitTester.cpp
    src/assets/EditorAssetBrowserBottomBarLayout.cpp
    src/assets/EditorAssetBrowserChromeHitTester.cpp
    src/assets/EditorAssetBrowserContentLayout.cpp
    src/assets/EditorAssetBrowserContextMenuState.cpp
    src/assets/EditorAssetReimport.cpp
    src/assets/EditorAssetBrowserContextMenuLayout.cpp
    src/assets/EditorAssetBrowserContentHitTester.cpp
    src/assets/EditorAssetBrowserDeleteConfirmState.cpp
    src/assets/EditorAssetBrowserFolderExpansionState.cpp
    src/assets/EditorAssetBrowserFolderRowBuilder.cpp
    src/assets/EditorAssetBrowserGeometry.cpp
    src/assets/EditorAssetBrowserHitPayloadResolver.cpp
    src/assets/EditorAssetBrowserHitTester.cpp
    src/assets/EditorAssetBrowserLayout.cpp
    src/assets/EditorAssetBrowserOverlayHitTester.cpp
    src/assets/EditorAssetBrowserPanelLayoutBuilder.cpp
    src/assets/EditorAssetBrowserPathUtils.cpp
    src/assets/EditorAssetBrowserSelectionModel.cpp
    src/assets/EditorAssetBrowserSelectionState.cpp
    src/assets/EditorAssetBrowserState.cpp
    src/assets/EditorAssetBrowserStateContext.cpp
    src/assets/EditorAssetBrowserStateRows.cpp
    src/assets/EditorAssetBrowserStateSelection.cpp
    src/assets/EditorAssetBrowserStateTextEdit.cpp
    src/assets/EditorAssetBrowserStateView.cpp
    src/assets/EditorAssetBrowserTextEditState.cpp
    src/assets/EditorAssetBrowserToolbarLayout.cpp
    src/assets/EditorAssetBrowserTreeHitTester.cpp
    src/assets/EditorAssetBrowserViewState.cpp
    src/commands/EditorCommandStack.cpp
    src/commands/EditorSceneHistoryCommand.cpp
    src/docking/DefaultDockWorkspace.cpp
    src/docking/DockDropPreviewFactory.cpp
    src/docking/DockDropPlacementPolicy.cpp
    src/docking/DockDropPreviewResolver.cpp
    src/docking/DockDropPreviewState.cpp
    src/docking/DockDropEdgeBandClassifier.cpp
    src/docking/DockDropZoneClassifier.cpp
    src/docking/DockDropZoneDepthSelector.cpp
    src/docking/DockDragOperationHandler.cpp
    src/docking/DockDragCompletionHandler.cpp
    src/docking/DockFloatingDragOperation.cpp
    src/docking/DockGeometry.cpp
    src/docking/DockHitTester.cpp
    src/docking/DockHoverCursorUpdater.cpp
    src/docking/DockCenterPanelInserter.cpp
    src/docking/DockLayoutBuilder.cpp
    src/docking/DockLeafLayoutBuilder.cpp
    src/docking/DockLayoutSerializer.cpp
    src/docking/EditorFloatingWindowSync.cpp
    src/docking/EditorWorkspaceArrangement.cpp
    src/docking/DockLeafPanelOrder.cpp
    src/docking/DockModelQueries.cpp
    src/docking/DockMainLayoutResolver.cpp
    src/docking/DockNodeFactory.cpp
    src/docking/DockNodeFinder.cpp
    src/docking/DockNodeIdSource.cpp
    src/docking/DockNodeLayoutBuilder.cpp
    src/docking/DockNodeQuery.cpp
    src/docking/DockNodeSlotFinder.cpp
    src/docking/DockPanelCollection.cpp
    src/docking/DockPanelDocking.cpp
    src/docking/DockPanelRemover.cpp
    src/docking/DockPointerDragFactory.cpp
    src/docking/DockSplitterResizer.cpp
    src/docking/DockSplitterDragHandler.cpp
    src/docking/DockSplitPanelInserter.cpp
    src/docking/DockSplitLayoutBuilder.cpp
    src/docking/DockTabIndexResolver.cpp
    src/docking/DockDockedTabDragHandler.cpp
    src/docking/DockTreeMutation.cpp
    src/docking/DockTreePruner.cpp
    src/docking/DockTypes.cpp
    src/docking/EditorDockController.cpp
    src/docking/EditorDockPointerDownHandler.cpp
    src/docking/EditorDockModel.cpp
    src/docking/EditorDockModelCommands.cpp
    src/docking/EditorDockModelQueries.cpp
    src/docking/EditorFloatingWindowManager.cpp
    src/docking/EditorFloatingWindowCommands.cpp
    src/docking/EditorFloatingWindowLifecycle.cpp
    src/docking/EditorFloatingWindowQueries.cpp
    src/docking/FloatingWindowCreator.cpp
    src/docking/FloatingWindowDestroyer.cpp
    src/docking/FloatingWindowRectReader.cpp
    src/docking/FloatingWindowRegistry.cpp
    src/docking/FloatingWindowResizeEventResolver.cpp
    src/private/docking/DefaultDockWorkspace.hpp
    src/private/docking/DockCenterPanelInserter.hpp
    src/private/docking/DockDropPreviewFactory.hpp
    src/private/docking/DockDropPreviewMetrics.hpp
    src/private/docking/DockDropPlacementPolicy.hpp
    src/private/docking/DockDropPreviewResolver.hpp
    src/private/docking/DockDropPreviewState.hpp
    src/private/docking/DockDropEdgeBandClassifier.hpp
    src/private/docking/DockDropZoneClassifier.hpp
    src/private/docking/DockDropZoneDepths.hpp
    src/private/docking/DockDragOperationHandler.hpp
    src/private/docking/DockDragCompletionHandler.hpp
    src/private/docking/DockDockedTabDragHandler.hpp
    src/private/docking/DockFloatingDragOperation.hpp
    src/private/docking/DockGeometry.hpp
    src/private/docking/DockHitTester.hpp
    src/private/docking/DockHoverCursorUpdater.hpp
    src/private/docking/DockLayoutBuilder.hpp
    src/private/docking/DockLayoutBuildSettings.hpp
    src/private/docking/DockLeafLayoutBuilder.hpp
    src/private/docking/DockLeafPanelOrder.hpp
    src/private/docking/DockModelQueries.hpp
    src/private/docking/DockMainLayoutResolver.hpp
    src/private/docking/DockLayoutSerializer.hpp
    src/private/docking/EditorFloatingWindowSync.hpp
    src/private/docking/EditorWorkspaceArrangement.hpp
    src/private/docking/DockNode.hpp
    src/private/docking/DockNodeFactory.hpp
    src/private/docking/DockNodeFinder.hpp
    src/private/docking/DockNodeIdSource.hpp
    src/private/docking/DockNodeLayoutBuilder.hpp
    src/private/docking/DockNodeQuery.hpp
    src/private/docking/DockNodeSlotFinder.hpp
    src/private/docking/DockPanelCollection.hpp
    src/private/docking/DockPanelDocking.hpp
    src/private/docking/DockPanelRemover.hpp
    src/private/docking/DockPointerDrag.hpp
    src/private/docking/DockPointerDragFactory.hpp
    src/private/docking/DockSplitterResizer.hpp
    src/private/docking/DockSplitterDragHandler.hpp
    src/private/docking/DockSplitPanelInserter.hpp
    src/private/docking/DockSplitLayoutBuilder.hpp
    src/private/docking/DockTabIndexResolver.hpp
    src/private/docking/DockTreeMutation.hpp
    src/private/docking/DockTreePruner.hpp
    src/private/docking/EditorDockController.hpp
    src/private/docking/EditorDockPointerDownHandler.hpp
    src/private/docking/EditorDockModel.hpp
    src/private/docking/EditorDockModelCommands.hpp
    src/private/docking/EditorDockModelQueries.hpp
    src/private/docking/EditorFloatingWindowManager.hpp
    src/private/docking/EditorFloatingWindowCommands.hpp
    src/private/docking/EditorFloatingWindowLifecycle.hpp
    src/private/docking/EditorFloatingWindowQueries.hpp
    src/private/docking/FloatingWindowCreator.hpp
    src/private/docking/FloatingWindowDestroyer.hpp
    src/private/docking/FloatingWindowRectReader.hpp
    src/private/docking/FloatingWindowRegistry.hpp
    src/private/docking/FloatingWindowResizeEventResolver.hpp
    src/private/assets/EditorAssetBrowserAssetRows.hpp
    src/private/assets/EditorAssetReimport.hpp
    src/private/assets/EditorAssetBrowserBreadcrumbHitTester.hpp
    src/private/assets/EditorAssetBrowserBottomBarLayout.hpp
    src/private/assets/EditorAssetBrowserChromeHitTester.hpp
    src/private/assets/EditorAssetBrowserContentLayout.hpp
    src/private/assets/EditorAssetBrowserContextMenuState.hpp
    src/private/assets/EditorAssetBrowserContextMenuLayout.hpp
    src/private/assets/EditorAssetBrowserContentHitTester.hpp
    src/private/assets/EditorAssetBrowserDeleteConfirmState.hpp
    src/private/assets/EditorAssetBrowserFolderExpansionState.hpp
    src/private/assets/EditorAssetBrowserFolderRowBuilder.hpp
    src/private/assets/EditorAssetBrowserGeometry.hpp
    src/private/assets/EditorAssetBrowserHitPayloadResolver.hpp
    src/private/assets/EditorAssetBrowserHitTester.hpp
    src/private/assets/EditorAssetBrowserLayout.hpp
    src/private/assets/EditorAssetBrowserOverlayHitTester.hpp
    src/private/assets/EditorAssetBrowserPanelLayoutBuilder.hpp
    src/private/assets/EditorAssetBrowserPathUtils.hpp
    src/private/assets/EditorAssetBrowserSelectionModel.hpp
    src/private/assets/EditorAssetBrowserSelectionState.hpp
    src/private/assets/EditorAssetBrowserToolbarLayout.hpp
    src/private/assets/EditorAssetBrowserTreeHitTester.hpp
    src/private/assets/EditorAssetBrowserState.hpp
    src/private/assets/EditorAssetBrowserTextEditState.hpp
    src/private/assets/EditorAssetBrowserViewState.hpp
    src/private/commands/EditorCommandStack.hpp
    src/private/commands/EditorSceneHistoryCommand.hpp
    src/private/commands/IEditorCommand.hpp
    src/console/EditorConsoleState.cpp
    src/private/console/EditorConsoleLayout.hpp
    src/private/console/EditorConsoleState.hpp
    src/private/app/EditorApplicationLifecycle.hpp
    src/private/app/EditorWorkspaceSession.hpp
    src/private/app/EditorApplicationMessageDispatcher.hpp
    src/private/app/EditorApplicationMessageLoop.hpp
    src/private/app/EditorApplicationState.hpp
    src/private/app/EditorApplicationWindowProc.hpp
    src/private/app/EditorCrashBreadcrumbs.hpp
    src/private/app/EditorEditCommandPolicy.hpp
    src/private/app/EditorEditCommandInputHandler.hpp
    src/private/app/EditorAssetBrowserContextCommandExecutor.hpp
    src/private/app/EditorAssetBrowserContextMenuPointerHandler.hpp
    src/private/app/EditorAssetBrowserDeleteConfirmPointerHandler.hpp
    src/private/app/EditorAssetBrowserDoubleClickHandler.hpp
    src/private/app/EditorAssetBrowserInputHandler.hpp
    src/private/app/EditorAssetBrowserPointerHandler.hpp
    src/private/app/EditorAssetBrowserPointerPanelResolver.hpp
    src/private/app/EditorAssetBrowserPrimaryClickHandler.hpp
    src/private/app/EditorAssetBrowserThumbnailScaleDragHandler.hpp
    src/private/app/EditorAssetDropActionMenu.hpp
    src/private/app/EditorAssetProjectFilesDropActionExecutor.hpp
    src/private/app/EditorKeyState.hpp
    src/private/app/EditorTextInputShortcuts.hpp
    src/private/app/console/EditorConsolePointerController.hpp
    src/private/app/cursor/EditorInternalSplitterCursorController.hpp
    src/private/app/docking/EditorMainDockSplitterPointerController.hpp
    src/private/app/EditorAssetFolderProjectFilesDropHandler.hpp
    src/private/app/EditorAudioAssetInspectorDropHandler.hpp
    src/private/app/EditorAudioInspectorDropPolicy.hpp
    src/private/app/EditorAnimatorAssetInspectorDropHandler.hpp
    src/private/app/EditorBehaviourAssetHierarchyDropHandler.hpp
    src/private/app/EditorBehaviourAssetInspectorDropHandler.hpp
    src/private/app/EditorBehaviourAssetSceneDropHandler.hpp
    src/private/app/EditorDropPanelResolver.hpp
    src/private/app/EditorHierarchyEntityAssetDropHandler.hpp
    src/private/app/EditorHierarchyEntityHierarchyDropHandler.hpp
    src/private/app/EditorMaterialAssetInspectorDropHandler.hpp
    src/private/app/EditorMaterialAssetSceneDropHandler.hpp
    src/private/app/EditorMeshAssetInspectorDropHandler.hpp
    src/private/app/EditorMeshAssetSceneDropHandler.hpp
    src/private/app/EditorParticleAssetSceneDropHandler.hpp
    src/private/app/EditorPrefabAssetSceneDropHandler.hpp
    src/private/app/EditorHierarchySearchInputHandler.hpp
    src/private/app/EditorMouseWheelRouter.hpp
    src/private/app/inspector/EditorInspectorPointerController.hpp
    src/private/app/plugins/EditorPluginsPointerController.hpp
    src/private/app/EditorPaintDispatcher.hpp
    src/private/app/EditorPendingTextEditCommitter.hpp
    src/private/app/EditorPlayModeState.hpp
    src/private/app/EditorPlayModeSceneSession.hpp
    src/private/app/EditorAutomationScenarioRunner.hpp
    src/private/app/EditorSceneFileCommands.hpp
    src/private/app/EditorSceneLifecycleGuard.hpp
    src/private/app/EditorSelfTest.hpp
    src/private/app/EditorHeadlessAutomation.hpp
    src/private/app/EditorShellInteractionState.hpp
    src/private/app/EditorPointerDragInteraction.hpp
    src/private/app/EditorPointerDragState.hpp
    src/private/app/EditorPointerDragSourceResolver.hpp
    src/private/app/EditorPointerDropHandler.hpp
    src/private/app/EditorPrefabAssetHierarchyDropHandler.hpp
    src/private/app/EditorPrefabAssetProjectFilesDropHandler.hpp
    src/private/app/panels/EditorPanelPointerHitContext.hpp
    src/private/app/project_settings/EditorProjectSettingsPointerController.hpp
    src/private/app/editor_settings/EditorSettingsPointerController.hpp
    src/private/app/pointer/EditorLeftButtonDoubleClickRouter.hpp
    src/private/app/pointer/EditorLeftButtonDownRouter.hpp
    src/private/app/pointer/EditorLeftButtonUpRouter.hpp
    src/private/app/pointer/EditorMouseMoveRouter.hpp
    src/private/app/pointer/EditorRightButtonDownRouter.hpp
    src/private/app/pointer/EditorSetCursorRouter.hpp
    src/private/app/project_files/EditorProjectFilesDeleteConfirmOverlayController.hpp
    src/private/app/project_files/EditorProjectFilesMouseWheelController.hpp
    src/private/app/project_files/EditorProjectFilesTransientUiController.hpp
    src/private/app/scene_viewport/EditorSceneViewportCameraController.hpp
    src/private/app/scene_viewport/EditorSceneViewportGizmoDragSolver.hpp
    src/private/app/scene_viewport/EditorTerrainViewportInteraction.hpp
    src/private/app/scene_viewport/EditorSceneViewportGizmoHitTester.hpp
    src/private/app/scene_viewport/EditorSceneViewportGizmoInteraction.hpp
    src/private/app/scene_viewport/gizmo/EditorSceneViewportGizmoAltDuplicate.hpp
    src/private/app/scene_viewport/gizmo/EditorSceneViewportGizmoDragState.hpp
    src/private/app/scene_viewport/gizmo/EditorSceneViewportGizmoDragUpdater.hpp
    src/private/app/scene_viewport/gizmo/EditorSceneViewportGizmoRotationDrag.hpp
    src/private/app/scene_viewport/gizmo/EditorSceneViewportGizmoTargetResolver.hpp
    src/private/app/scene_viewport/gizmo/EditorSceneViewportGizmoToolBehavior.hpp
    src/private/app/scene_viewport/EditorSceneViewportHitResolver.hpp
    src/private/app/scene_viewport/EditorSceneViewportAssetDragPreview.hpp
    src/private/app/scene_viewport/EditorSceneViewportMeshPicker.hpp
    src/private/app/scene_viewport/EditorSceneViewportObjectInteraction.hpp
    src/private/app/scene_viewport/EditorSceneViewportSelectionController.hpp
    src/private/app/scene_viewport/EditorSceneViewportSelectionInteraction.hpp
    src/private/app/scene_viewport/EditorSceneViewportSelectionTypes.hpp
    src/private/app/scene_viewport/EditorSceneViewportToolbarPointerController.hpp
    src/private/app/scene_viewport/EditorSceneViewportTypes.hpp
    src/private/app/EditorWindowLifecycleHandler.hpp
    src/private/app/EditorWindowHitTestHandler.hpp
    src/private/app/EditorWindowMessageContext.hpp
    src/private/app/EditorWindowMessageRouter.hpp
    src/private/app/EditorWindowPointerMessageDispatcher.hpp
    src/private/app/EditorWindowPointerHandler.hpp
    src/private/app/EditorWindowToolbarPointerHandler.hpp
    src/private/app/EditorWindowInvalidator.hpp
    src/private/app/EditorWindowResizeInteraction.hpp
    src/private/app/EditorWindowResizeHandler.hpp
    src/private/inspection/InspectorInputInteraction.hpp
    src/private/inspection/InspectorAudioComponentModel.hpp
    src/private/inspection/InspectorAudioMixerAssetInteraction.hpp
    src/private/inspection/InspectorAudioMixerAssetModel.hpp
    src/private/inspection/InspectorSceneAudioInteraction.hpp
    src/private/inspection/InspectorSceneAudioModel.hpp
    src/private/inspection/InspectorAudioScrubController.hpp
    src/private/inspection/InspectorAudioTextBuilder.hpp
    src/private/inspection/InspectorCameraTextBuilder.hpp
    src/private/inspection/InspectorColliderTextBuilder.hpp
    src/private/inspection/InspectorComponentCatalog.hpp
    src/private/inspection/InspectorComponentLabelFormatter.hpp
    src/private/inspection/InspectorPhysicsModel.hpp
    src/private/inspection/InspectorAddComponentBrowserModel.hpp
    src/private/inspection/InspectorEntitySummaryTextBuilder.hpp
    src/private/inspection/InspectorLightTextBuilder.hpp
    src/private/inspection/EditorValueFormatter.hpp
    src/private/inspection/InspectorMaterialTextureSlotFormatter.hpp
    src/private/inspection/MaterialAssetFormatter.hpp
    src/private/inspection/InspectorMeshRendererTextBuilder.hpp
    src/private/inspection/InspectorMultiSelectionTextBuilder.hpp
    src/private/inspection/InspectorPanelTextBuilder.hpp
    src/private/inspection/InspectorRigidbodyTextBuilder.hpp
    src/private/platform/win32/EditorMainWindow.hpp
    src/private/platform/win32/EditorAssetImportDialog.hpp
    src/private/platform/win32/EditorMaterialAssetPickerDialog.hpp
    src/private/scene/material/MaterialEditorCanonicalCompare.hpp
    src/private/scene/material/EditorTextureAssetMetadataResolver.hpp
    src/private/platform/win32/EditorMaterialColorPickerDialog.hpp
    src/private/platform/win32/EditorMaterialParameterValueDialog.hpp
    src/private/platform/win32/EditorTagNameDialog.hpp
    src/private/platform/win32/EditorMeshAssetPickerDialog.hpp
    src/private/platform/win32/EditorAudioMixerAssetPickerDialog.hpp
    src/private/platform/win32/EditorChoiceDialog.hpp
    src/private/platform/win32/EditorDebugLogGate.hpp
    src/private/platform/win32/EditorModalMessageLoop.hpp
    src/private/platform/win32/EditorNewSceneDialog.hpp
    src/private/platform/win32/EditorTextEntryDialog.hpp
    src/private/platform/win32/EditorMap2DForm.hpp
    src/private/platform/win32/EditorMap2DWindow.hpp
    src/private/platform/win32/EditorModalWindowScope.hpp
    src/private/rendering/EditorMaterialThumbnailService.hpp
    src/private/rendering/EditorParticleThumbnailService.hpp
    src/private/rendering/ParticleThumbnailTimeline.hpp
    src/private/rendering/MaterialPreviewAppearanceResolver.hpp
    src/private/rendering/MaterialPreviewTextureAverageColor.hpp
    src/private/platform/win32/EditorSceneDirtyPrompt.hpp
    src/private/platform/win32/EditorSceneFileDialog.hpp
    src/private/platform/win32/EditorWindowClassRegistry.hpp
    src/private/platform/win32/Win32ErrorReporter.hpp
    src/private/platform/win32/Win32InputDebugLogger.hpp
    src/private/project/EditorProjectAssetIndex.hpp
    src/private/project/EditorProjectBootstrap.hpp
    src/private/project/EditorProjectPanelHitTester.hpp
    src/private/settings/EditorConfigurationStore.hpp
    src/private/settings/EditorLayoutLibrary.hpp
    src/private/settings/EditorLayoutMenuModel.hpp
    src/private/settings/EditorPanelSessionText.hpp
    src/private/project/EditorProjectPaths.hpp
    src/private/rendering/DockDropPreviewRenderer.hpp
    src/private/rendering/DockDropPreviewOverlayWindow.hpp
    src/private/rendering/DockPanelChromeRenderer.hpp
    src/private/rendering/DockPanelFramePainter.hpp
    src/private/rendering/DockWorkspaceChromeRenderer.hpp
    src/private/rendering/DockWorkspaceContentRenderer.hpp
    src/private/rendering/DockWorkspaceRenderer.hpp
    src/private/rendering/DockWorkspaceTabStripRenderer.hpp
    src/private/rendering/EditorDragOverlayRenderer.hpp
    src/private/rendering/EditorBgfxBackendSelector.hpp
    src/private/rendering/ConsoleDetailTextOverlay.hpp
    src/private/rendering/EditorToolbarRenderer.hpp
    src/private/rendering/EditorHostSurfaceLayoutResolver.hpp
    src/private/rendering/EditorPanelContentResolver.hpp
    src/private/rendering/EditorRenderBackendSettings.hpp
    src/private/rendering/EditorSurfaceKind.hpp
    src/private/rendering/EditorSurfacePainter.hpp
    src/private/rendering/EditorSceneBgfxViewport.hpp
    src/private/rendering/EditorTabIndicatorPainter.hpp
    src/private/rendering/EditorToolbarLayout.hpp
    src/private/rendering/FloatingEditorWindowRenderer.hpp
    src/private/rendering/FloatingWindowBackBufferPainter.hpp
    src/private/rendering/FloatingPanelGeometry.hpp
    src/private/rendering/FloatingWindowControlRenderer.hpp
    src/private/rendering/GdiBackBufferRenderer.hpp
    src/private/rendering/EditorGdiRenderer.hpp
    src/private/rendering/EditorMeshPreviewRasterizer.hpp
    src/private/rendering/EditorMeshPreviewService.hpp
    src/private/rendering/EditorMeshPreviewTypes.hpp
    src/private/rendering/EditorMeshThumbnailDiskCache.hpp
    src/private/rendering/EditorMeshThumbnailService.hpp
    src/private/rendering/GdiDrawing.hpp
    src/private/rendering/GdiResources.hpp
    src/private/rendering/EditorSurfaceStyle.hpp
    src/private/rendering/EditorTexturePreviewService.hpp
    src/private/rendering/MainWindowBackBufferPainter.hpp
    src/private/rendering/gdi/GdiAlphaBlender.hpp
    src/private/rendering/gdi/GdiColor.hpp
    src/private/rendering/gdi/GdiFramePainter.hpp
    src/private/rendering/gdi/GdiRect.hpp
    src/private/rendering/gdi/GdiRectPainter.hpp
    src/private/rendering/gdi/EditorFontCache.hpp
    src/private/rendering/gdi/GdiTextPainter.hpp
    src/private/rendering/gdi/ScopedBitmap.hpp
    src/private/rendering/gdi/ScopedBrush.hpp
    src/private/rendering/gdi/ScopedCompatibleDc.hpp
    src/private/rendering/gdi/ScopedFont.hpp
    src/private/rendering/gdi/ScopedGdiObject.hpp
    src/private/rendering/gdi/ScopedPaint.hpp
    src/private/rendering/gdi/ScopedPen.hpp
    src/private/rendering/HierarchyPanelRenderer.hpp
    src/private/rendering/HierarchyPanelStyle.hpp
    src/private/rendering/HierarchyPanelToolbarRenderer.hpp
    src/private/rendering/HierarchyRowRenderer.hpp
    src/private/rendering/components/CategoryHeader.hpp
    src/private/rendering/components/DenseListRow.hpp
    src/private/rendering/components/EditorDialogStyle.hpp
    src/private/rendering/components/PropertyRow.hpp
    src/private/rendering/HeroIconCatalog.hpp
    src/private/rendering/HeroIconAssets.hpp
    src/private/rendering/HeroIconDrawFrame.hpp
    src/private/rendering/HeroIconGdiplusRuntime.hpp
    src/private/rendering/HeroIconKind.hpp
    src/private/rendering/HeroIconPathPainter.hpp
    src/private/rendering/HeroIconPainter.hpp
    src/private/rendering/InspectorPanelRenderer.hpp
    src/private/rendering/InspectorAudioMixerAssetView.hpp
    src/private/rendering/InspectorSceneAudioView.hpp
    src/private/rendering/EditorOverlayPopupWindow.hpp
    src/private/app/EditorHeadlessPopupChecks.hpp
    src/private/rendering/EditorToolbarMenuOverlayWindow.hpp
    src/private/rendering/InspectorAddComponentOverlayWindow.hpp
    src/private/rendering/MaterialEditorPanelRenderer.hpp
    src/private/rendering/material_graph/EditorMaterialGraphPinPresentation.hpp
    src/private/rendering/material_graph/MaterialGraphCanvasDocumentAdapter.hpp
    src/private/rendering/material_graph/MaterialGraphCanvas.hpp
    src/private/rendering/PanelContentRenderer.hpp
    src/private/rendering/AnimationClipEditorPanelRenderer.hpp
    src/private/rendering/ProjectFilesAssetListRenderer.hpp
    src/private/rendering/ProjectFilesAssetIconResolver.hpp
    src/private/rendering/ProjectFilesAssetTileFrameRenderer.hpp
    src/private/rendering/ProjectFilesAssetTileMetrics.hpp
    src/private/rendering/ProjectFilesAssetTileRenderer.hpp
    src/private/rendering/ProjectFilesAssetViewRenderer.hpp
    src/private/rendering/ProjectFilesBottomBarRenderer.hpp
    src/private/rendering/ProjectFilesDeleteConfirmOverlayWindow.hpp
    src/private/rendering/ProjectFilesFilterMenuOverlayWindow.hpp
    src/private/rendering/ProjectFilesOverlayRenderer.hpp
    src/private/rendering/ProjectFilesPanelDrawing.hpp
    src/private/rendering/ProjectFilesPanelRenderer.hpp
    src/private/rendering/ProjectFilesTileTextRenderer.hpp
    src/private/rendering/ProjectFilesToolbarRenderer.hpp
    src/private/rendering/ProjectFilesTreeRenderer.hpp
    src/private/rendering/PluginsPanelRenderer.hpp
    src/private/rendering/ProjectSettingsPanelLayout.hpp
    src/private/rendering/ProjectSettingsPanelRenderer.hpp
    src/private/rendering/EditorScriptEditorOverlay.hpp
    src/private/rendering/script_editor/LuaSyntaxHighlighter.hpp
    src/private/rendering/script_editor/ScriptEditorClipboard.hpp
    src/private/rendering/script_editor/ScriptEditorDocument.hpp
    src/private/rendering/script_editor/ScriptEditorInput.hpp
    src/private/rendering/script_editor/ScriptEditorLayout.hpp
    src/private/rendering/script_editor/ScriptEditorRenderer.hpp
    src/private/rendering/script_editor/ScriptEditorTextEncoding.hpp
    src/private/rendering/script_editor/ScriptEditorTheme.hpp
    src/private/rendering/script_editor/ScriptEditorWindow.hpp
    src/private/rendering/script_editor/ScriptSourceFile.hpp
    src/private/rendering/ScriptEditorPanelRenderer.hpp
    src/private/rendering/SceneViewportToolbarRenderer.hpp
    src/private/rendering/scene_viewport_toolbar/SceneViewportToolbarDrawing.hpp
    src/private/rendering/scene_viewport_toolbar/SceneViewportToolbarInfoRenderer.hpp
    src/private/rendering/scene_viewport_toolbar/SceneViewportToolbarLabelFormat.hpp
    src/private/rendering/scene_viewport_toolbar/SceneViewportToolbarLayout.hpp
    src/private/rendering/scene_viewport_toolbar/SceneViewportToolbarMetrics.hpp
    src/private/rendering/scene_viewport_toolbar/SceneViewportToolbarState.hpp
    src/private/rendering/SceneViewportToolbarDropdownOverlayWindow.hpp
    src/private/rendering/ScenePanelContentRenderer.hpp
    src/private/rendering/SceneViewportPresentationPolicy.hpp
    src/private/rendering/HierarchyRowLayout.hpp
    src/private/rendering/HierarchyToolbarLayout.hpp
    src/private/rendering/SvgGraphicsPathBuilder.hpp
    src/private/rendering/SvgPathArcConverter.hpp
    src/private/rendering/SvgPathArcCenter.hpp
    src/private/rendering/SvgPathArcEndpoint.hpp
    src/private/rendering/SvgPathArcMath.hpp
    src/private/rendering/SvgPathArcSegment.hpp
    src/private/rendering/SvgPathCursor.hpp
    src/private/rendering/SvgPathFigureBuilder.hpp
    src/private/rendering/SvgPathGdiplusPoint.hpp
    src/private/rendering/SvgPathPoint.hpp
    src/private/scene/EditorHierarchyContentResolver.hpp
    src/private/scene/EditorHierarchyFloatingContentResolver.hpp
    src/private/scene/EditorHierarchyMainContentResolver.hpp
    src/private/scene/EditorHierarchyMetrics.hpp
    src/private/scene/EditorHierarchyExpansionState.hpp
    src/private/scene/EditorHierarchySelectionNormalizer.hpp
    src/private/scene/EditorPlayModeSelectionSnapshot.hpp
    src/private/scene/EditorHierarchyObjectFactory.hpp
    src/private/scene/EditorHierarchyRow.hpp
    src/private/scene/EditorHierarchyRowBuilder.hpp
    src/private/scene/EditorHierarchyRowFactory.hpp
    src/private/scene/EditorHierarchySearchState.hpp
    src/private/scene/EditorHierarchySearchMatcher.hpp
    src/private/scene/EditorHierarchySelectionState.hpp
    src/private/scene/EditorHierarchyRowPicker.hpp
    src/private/scene/EditorHierarchySelectionController.hpp
    src/private/scene/EditorPluginCatalog.hpp
    src/private/scene/EditorPluginsState.hpp
    src/private/scene/EditorProjectSettingsState.hpp
    src/private/app/EditorBuildGameInputHandler.hpp
    src/private/settings/EditorBuildGameSettingsStore.hpp
    src/private/packaging/EditorAndroidSigningBroker.hpp
    src/private/packaging/EditorPackageInputValidation.hpp
    src/private/packaging/EditorPackageProcessEnvironment.hpp
    src/private/packaging/EditorProjectIconTransaction.hpp
    src/private/packaging/EditorProjectPackageService.hpp
    src/private/packaging/EditorSigningProcess.hpp
    src/private/packaging/EditorWindowsSigningBroker.hpp
    src/private/platform/win32/EditorBuildGameFileDialog.hpp
    src/private/rendering/BuildGamePanelLayout.hpp
    src/private/rendering/BuildGamePanelModel.hpp
    src/private/rendering/BuildGamePanelRenderer.hpp
    src/private/scene/EditorScriptAssetGateway.hpp
    src/private/scene/EditorScriptEditorState.hpp
    src/private/scene/EditorSceneAssetBrowserCommands.hpp
    src/private/localization/EditorLocalization.hpp
    src/private/scene/EditorSceneAudioAssetActions.hpp
    src/private/scene/EditorSceneCommandController.hpp
    src/private/scene/EditorAssetErrorMessage.hpp
    src/private/scene/EditorSceneContext.hpp
    src/private/scene/AnimationClipTimelineState.hpp
    src/private/scene/AnimationClipEditorDocumentState.hpp
    src/private/scene/EditorSceneDocumentAssetLoaders.hpp
    src/private/scene/EditorSceneHierarchyActions.hpp
    src/private/scene/EditorSceneMaterialAssetActions.hpp
    src/private/scene/EditorSceneMeshAssetActions.hpp
    src/private/scene/EditorTerrainService.hpp
    src/private/scene/EditorMap2DService.hpp
    src/private/scene/EditorSceneObjectEditCommands.hpp
    src/private/scene/EditorSceneObjectEditTypes.hpp
    src/private/scene/EditorScenePrefabActions.hpp
    src/private/scene/EditorSceneSelectionPivot.hpp
    src/private/scene/EditorSceneViewportStateStore.hpp
    src/private/scene/EditorViewportCameraState.hpp
    src/private/scene/EditorViewportPreviewState.hpp
    src/private/scene/material/EditorMaterialAssetAuthoring.hpp
    src/private/scene/material/EditorMaterialAssetEditCommand.hpp
    src/private/scene/material/EditorMaterialAssetGateway.hpp
    src/private/scene/material/EditorMaterialGraphDebugLog.hpp
    src/private/scene/material/EditorEmbeddedMaterialAssetWriter.hpp
    src/private/scene/material/EditorEmbeddedMaterialExtractionTypes.hpp
    src/private/scene/material/EditorEmbeddedMaterialExtractor.hpp
    src/private/scene/material/EditorEmbeddedMaterialTextureResolver.hpp
    src/private/scene/material/MaterialEditorState.hpp
    src/private/scene/material/MaterialGraphInteractionState.hpp
    src/private/scene/material_preview/EditorMaterialGraphCookService.hpp
    src/private/scene/material_preview/EditorMaterialNodePreviewBuilder.hpp
    src/private/scene/material_preview/EditorMaterialPreviewMeshFactory.hpp
    src/private/scene/material_preview/EditorMaterialPreviewMeshLoader.hpp
    src/private/scene/material_preview/EditorMaterialPreviewScene.hpp
    src/private/scene/material_preview/EditorMaterialPreviewSettings.hpp
    src/private/scene/material_preview/EditorMaterialPreviewTelemetry.hpp
    src/private/scene/transform_edit/EditorSceneTransformChange.hpp
    src/private/scene/transform_edit/EditorSceneTransformCommitBuilder.hpp
    src/private/scene/transform_edit/EditorSceneTransformEditApplier.hpp
    src/private/scene/transform_edit/EditorSceneTransformEditController.hpp
    src/private/scene/transform_edit/EditorSceneTransformEditSession.hpp
    src/private/scene/transform_edit/EditorSceneTransformEquality.hpp
    src/private/scene/transform_edit/EditorSceneTransformMath.hpp
    src/private/scene/transform_edit/EditorSceneTransformSnapshotBuilder.hpp
    src/private/scene/transform_edit/EditorTransformProperty.hpp
    src/private/scene/input/EditorInputAssetGateway.hpp
    src/private/scene/input/EditorInputAssetCatalog.hpp
    src/private/scene/input/EditorInputActionAuthoring.hpp
    src/private/scene/input/EditorInputMappingContextAuthoring.hpp
    src/private/scene/audio/EditorAudioMixerAssetGateway.hpp
    src/private/scene/audio/EditorAudioMixerAuthoring.hpp
    src/private/scene/audio/EditorSceneAudioSettingsService.hpp
    src/private/scene/EditorSceneDocumentIdentity.hpp
    src/private/windowing/EditorFloatingWindowFrame.hpp
    src/private/windowing/FloatingWindowControlActions.hpp
    src/private/windowing/FloatingWindowControlHitTester.hpp
    src/private/windowing/FloatingWindowControlInteractor.hpp
    src/private/windowing/FloatingWindowControlKind.hpp
    src/private/windowing/FloatingWindowControlLayout.hpp
    src/private/windowing/FloatingWindowFactory.hpp
    src/private/windowing/FloatingWindowHitTestResolver.hpp
    src/platform/win32/EditorMainWindow.cpp
    src/platform/win32/EditorAssetImportDialog.cpp
    src/platform/win32/EditorMaterialColorPickerDialog.cpp
    src/platform/win32/EditorChoiceDialog.cpp
    src/platform/win32/EditorMaterialParameterValueDialog.cpp
    src/platform/win32/EditorTagNameDialog.cpp
    src/platform/win32/EditorNewSceneDialog.cpp
    src/platform/win32/EditorTextEntryDialog.cpp
    src/platform/win32/EditorMap2DForm.cpp
    src/platform/win32/EditorMap2DWindow.cpp
    src/platform/win32/EditorMeshAssetPickerDialog.cpp
    src/platform/win32/EditorSceneDirtyPrompt.cpp
    src/platform/win32/EditorSceneFileDialog.cpp
    src/platform/win32/EditorBuildGameFileDialog.cpp
    src/platform/win32/EditorWindowClassRegistry.cpp
    src/platform/win32/Win32ErrorReporter.cpp
    src/platform/win32/Win32InputDebugLogger.cpp
    src/rendering/DockDropPreviewRenderer.cpp
    src/rendering/DockDropPreviewOverlayWindow.cpp
    src/rendering/DockPanelChromeRenderer.cpp
    src/rendering/DockPanelFramePainter.cpp
    src/rendering/DockWorkspaceChromeRenderer.cpp
    src/rendering/DockWorkspaceContentRenderer.cpp
    src/rendering/DockWorkspaceRenderer.cpp
    src/rendering/DockWorkspaceTabStripRenderer.cpp
    src/rendering/EditorDragOverlayRenderer.cpp
    src/rendering/EditorBgfxBackendSelector.cpp
    src/rendering/EditorGdiRenderer.cpp
    src/rendering/EditorHostSurfaceLayoutResolver.cpp
    src/rendering/EditorPanelContentResolver.cpp
    src/rendering/EditorRenderBackendSettings.cpp
    src/rendering/EditorSceneBgfxViewport.cpp
    src/rendering/EditorSceneViewportGeometry.cpp
    src/rendering/EditorSceneViewportHostSurfaceStore.cpp
    src/rendering/EditorSceneViewportPendingSubmissionBuilder.cpp
    src/rendering/EditorSceneViewportPendingPresentBatchBuilder.cpp
    src/rendering/EditorSceneViewportPendingPaintSubmitter.cpp
    src/rendering/EditorSceneViewportTextOverlay.cpp
    src/rendering/EditorSceneViewportRegionBuilder.cpp
    src/rendering/EditorSceneViewportSessionStore.cpp
    src/rendering/EditorSurfacePainter.cpp
    src/rendering/EditorSurfaceStyle.cpp
    src/rendering/EditorTexturePreviewService.cpp
    src/rendering/EditorTabIndicatorPainter.cpp
    src/rendering/EditorToolbarLayout.cpp
    src/rendering/EditorToolbarRenderer.cpp
    src/rendering/EditorMeshPreviewRasterizer.cpp
    src/rendering/EditorMeshPreviewService.cpp
    src/rendering/EditorMeshThumbnailDiskCache.cpp
    src/rendering/EditorMeshThumbnailService.cpp
    src/rendering/ConsoleDetailTextOverlay.cpp
    src/rendering/FloatingEditorWindowRenderer.cpp
    src/rendering/FloatingWindowBackBufferPainter.cpp
    src/rendering/FloatingPanelGeometry.cpp
    src/rendering/FloatingWindowControlRenderer.cpp
    src/rendering/GdiBackBufferRenderer.cpp
    src/rendering/GdiDrawing.cpp
    src/rendering/gdi/GdiAlphaBlender.cpp
    src/rendering/gdi/GdiFramePainter.cpp
    src/rendering/gdi/GdiRect.cpp
    src/rendering/gdi/GdiRectPainter.cpp
    src/rendering/gdi/EditorFontCache.cpp
    src/rendering/gdi/GdiTextPainter.cpp
    src/rendering/HierarchyPanelRenderer.cpp
    src/rendering/HierarchyPanelStyle.cpp
    src/rendering/HierarchyPanelToolbarRenderer.cpp
    src/rendering/HierarchyRowRenderer.cpp
    src/rendering/EditorRowComponents.cpp
    src/rendering/EditorDialogStyle.cpp
    src/rendering/HierarchyRowLayout.cpp
    src/rendering/HierarchyToolbarLayout.cpp
    src/rendering/HeroIconAssets.cpp
    src/rendering/HeroIconCatalog.cpp
    src/rendering/HeroIconDrawFrame.cpp
    src/rendering/HeroIconGdiplusRuntime.cpp
    src/rendering/HeroIconPathPainter.cpp
    src/rendering/HeroIconPainter.cpp
    src/rendering/InspectorPanelRenderer.cpp
    src/rendering/InspectorAudioMixerAssetView.cpp
    src/rendering/InspectorSceneAudioView.cpp
    src/rendering/EditorOverlayPopupWindow.cpp
    src/app/EditorHeadlessPopupChecks.cpp
    src/rendering/EditorToolbarMenuOverlayWindow.cpp
    src/rendering/InspectorAddComponentOverlayWindow.cpp
    src/rendering/MaterialEditorPanelRenderer.cpp
    src/rendering/EditorMaterialThumbnailService.cpp
    src/rendering/EditorParticleThumbnailService.cpp
    src/rendering/MaterialPreviewAppearanceResolver.cpp
    src/rendering/MaterialPreviewTextureAverageColor.cpp
    src/rendering/MaterialEditorPanelLayout.cpp
    src/rendering/material_graph/EditorMaterialGraphPinPresentation.cpp
    src/rendering/material_graph/MaterialGraphCanvasDocumentAdapter.cpp
    src/rendering/material_graph/MaterialGraphCanvas.cpp
    src/inspection/InspectorInputInteraction.cpp
    src/inspection/InspectorPanelInteraction.cpp
    src/inspection/InspectorPanelState.cpp
    src/rendering/MainWindowBackBufferPainter.cpp
    src/rendering/PanelContentRenderer.cpp
    src/rendering/ParticleEditorPanelRenderer.cpp
    src/private/rendering/ParticleEditorPanelLayout.hpp
    src/rendering/ParticleEditorPanelLayout.cpp
    src/rendering/AnimationClipEditorPanelRenderer.cpp
    src/rendering/AnimatorEditorPanelRenderer.cpp
    src/rendering/TimelineEditorPanel.cpp
    src/rendering/BlendSpace2DEditorPanel.cpp
    src/rendering/GameplayGraphEditorPanel.cpp
    src/rendering/SkeletalMeshEditorPanelRenderer.cpp
    src/rendering/ProjectFilesAssetListRenderer.cpp
    src/assets/EditorAssetOpenPolicy.cpp
    src/rendering/ProjectFilesAssetIconResolver.cpp
    src/rendering/ProjectFilesAssetTileFrameRenderer.cpp
    src/rendering/ProjectFilesAssetTileMetrics.cpp
    src/rendering/ProjectFilesAssetTileRenderer.cpp
    src/rendering/ProjectFilesMaterialPreviewThumbnailModel.cpp
    src/rendering/ProjectFilesAssetViewRenderer.cpp
    src/rendering/ProjectFilesBottomBarRenderer.cpp
    src/rendering/ProjectFilesDeleteConfirmOverlayWindow.cpp
    src/rendering/ProjectFilesFilterMenuOverlayWindow.cpp
    src/rendering/ProjectFilesOverlayRenderer.cpp
    src/rendering/ProjectFilesPanelDrawing.cpp
    src/rendering/ProjectFilesPanelRenderer.cpp
    src/rendering/ProjectFilesTileTextRenderer.cpp
    src/rendering/ProjectFilesToolbarRenderer.cpp
    src/rendering/ProjectFilesTreeRenderer.cpp
    src/rendering/BuildGamePanelLayout.cpp
    src/rendering/BuildGamePanelModel.cpp
    src/rendering/BuildGamePanelRenderer.cpp
    src/rendering/PluginsPanelRenderer.cpp
    src/rendering/ProjectSettingsPanelLayout.cpp
    src/rendering/ProjectSettingsPanelRenderer.cpp
    src/rendering/EditorSettingsPanelRenderer.cpp
    src/private/rendering/EditorSettingsPanelRenderer.hpp
    src/rendering/EditorScriptEditorOverlay.cpp
    src/rendering/script_editor/LuaSyntaxHighlighter.cpp
    src/rendering/script_editor/ScriptEditorClipboard.cpp
    src/rendering/script_editor/ScriptEditorDocument.cpp
    src/rendering/script_editor/ScriptEditorInput.cpp
    src/rendering/script_editor/ScriptEditorLayout.cpp
    src/rendering/script_editor/ScriptEditorRenderer.cpp
    src/rendering/script_editor/ScriptEditorTextEncoding.cpp
    src/rendering/script_editor/ScriptEditorWindow.cpp
    src/rendering/script_editor/ScriptSourceFile.cpp
    src/rendering/ScriptEditorPanelRenderer.cpp
    src/rendering/SceneViewportToolbarRenderer.cpp
    src/rendering/scene_viewport_toolbar/SceneViewportToolbarDrawing.cpp
    src/rendering/scene_viewport_toolbar/SceneViewportToolbarInfoRenderer.cpp
    src/rendering/scene_viewport_toolbar/SceneViewportToolbarLayout.cpp
    src/rendering/scene_viewport_toolbar/SceneViewportToolbarState.cpp
    src/rendering/SceneViewportToolbarDropdownOverlayWindow.cpp
    src/rendering/ScenePanelContentRenderer.cpp
    src/rendering/SvgGraphicsPathBuilder.cpp
    src/rendering/SvgPathArcConverter.cpp
    src/rendering/SvgPathArcMath.cpp
    src/rendering/SvgPathArcSegment.cpp
    src/rendering/SvgPathCursor.cpp
    src/rendering/SvgPathFigureBuilder.cpp
    src/inspection/InspectorCameraTextBuilder.cpp
    src/inspection/InspectorAudioTextBuilder.cpp
    src/inspection/InspectorAudioMixerAssetInteraction.cpp
    src/inspection/InspectorAudioMixerAssetModel.cpp
    src/inspection/InspectorPostProcessProfileModel.cpp
    src/private/inspection/InspectorPostProcessProfileModel.hpp
    src/inspection/InspectorSceneAudioInteraction.cpp
    src/inspection/InspectorSceneAudioModel.cpp
    src/inspection/InspectorAudioComponentModel.cpp
    src/inspection/InspectorAudioScrubController.cpp
    src/inspection/InspectorColliderTextBuilder.cpp
    src/inspection/InspectorComponentCatalog.cpp
    src/inspection/InspectorSceneColor.cpp
    src/localization/EditorLocalization.cpp
    src/inspection/InspectorSceneColorEdit.cpp
    src/inspection/InspectorComponentLabelFormatter.cpp
    src/inspection/InspectorPhysicsModel.cpp
    src/inspection/ui/InspectorUIComponentModel.cpp
    src/inspection/InspectorAddComponentBrowserModel.cpp
    src/inspection/InspectorEntitySummaryTextBuilder.cpp
    src/inspection/InspectorGenericComponentModel.cpp
    src/inspection/InspectorLightTextBuilder.cpp
    src/inspection/InspectorMeshRendererMaterialSlotModel.cpp
    src/inspection/InspectorMaterialTextureSlotFormatter.cpp
    src/inspection/EditorValueFormatter.cpp
    src/inspection/MaterialAssetFormatter.cpp
    src/inspection/InspectorMeshRendererTextBuilder.cpp
    src/inspection/InspectorMultiSelectionTextBuilder.cpp
    src/inspection/InspectorPanelTextBuilder.cpp
    src/inspection/InspectorRigidbodyTextBuilder.cpp
    src/scene/EditorAnimationPreviewScene.cpp
    src/scene/EditorTimelinePreviewSession.cpp
    src/scene/EditorBlendSpace2DPreviewSession.cpp
    src/scene/EditorSceneContextBlendSpace2D.cpp
    src/scene/GameplayGraphEditorState.cpp
    src/scene/EditorSceneContextGameplayGraph.cpp
    src/scene/EditorSceneContextTimeline.cpp
    src/scene/TimelineEditorModel.cpp
    src/scene/EditorHierarchyContentResolver.cpp
    src/scene/EditorHierarchyFloatingContentResolver.cpp
    src/scene/EditorHierarchyMainContentResolver.cpp
    src/scene/EditorHierarchyExpansionState.cpp
    src/scene/EditorHierarchyObjectFactory.cpp
    src/scene/EditorHierarchyRowBuilder.cpp
    src/scene/EditorHierarchyRowFactory.cpp
    src/scene/EditorHierarchySearchState.cpp
    src/scene/EditorHierarchySearchMatcher.cpp
    src/scene/EditorHierarchySelectionState.cpp
    src/scene/EditorHierarchySelectionNormalizer.cpp
    src/scene/EditorHierarchyRowPicker.cpp
    src/scene/EditorHierarchySelectionController.cpp
    src/scene/EditorPluginCatalog.cpp
    src/scene/EditorScriptAssetGateway.cpp
    src/scene/EditorSceneAssetBrowserCommands.cpp
    src/scene/EditorSceneAudioAssetActions.cpp
    src/scene/EditorSceneCommandController.cpp
    src/scene/EditorSceneContext.cpp
    src/scene/ui/EditorUIComponentAuthoring.cpp
    src/private/platform/win32/EditorBundledFontPath.hpp
    src/scene/EditorSceneContextSkeletalMesh.cpp
    src/scene/EditorSceneContextPostProcess.cpp
    src/scene/ui/EditorUIThemeAssets.cpp
    src/private/scene/ui/EditorUIThemeAssets.hpp
    src/scene/ui/EditorSceneContextUITheme.cpp
    src/inspection/ui/InspectorUIThemeModel.cpp
    src/private/inspection/ui/InspectorUIThemeModel.hpp
    src/scene/EditorSceneBuildGame.cpp
    src/scene/EditorParticleEditorHost.cpp
    src/private/scene/ParticleEditorBakeHostCommand.hpp
    src/scene/ParticleEditorBakeHostCommand.cpp
    src/scene/EditorSceneDocumentLifecycle.cpp
    src/private/scene/EditorWorldPartition.hpp
    src/scene/EditorWorldPartition.cpp
    src/scene/EditorSceneWorldPartition.cpp
    src/scene/EditorNavigation.cpp
    src/scene/EditorSceneNavigation.cpp
    src/scene/EditorSceneHierarchyActions.cpp
    src/scene/EditorSceneMaterialAssetActions.cpp
    src/scene/EditorSceneMeshAssetActions.cpp
    src/scene/EditorTerrainService.cpp
    src/scene/EditorMap2DService.cpp
    src/scene/EditorMap2DPreview.cpp
    src/scene/EditorSceneObjectEditCommands.cpp
    src/scene/EditorScenePrefabActions.cpp
    src/scene/EditorSceneSelectionPivot.cpp
    src/scene/EditorSceneViewportStateStore.cpp
    src/settings/EditorConfigurationStore.cpp
    src/settings/EditorLayoutLibrary.cpp
    src/settings/EditorLayoutMenuModel.cpp
    src/settings/EditorPanelSessionText.cpp
    src/scene/EditorViewportCameraState.cpp
    src/scene/EditorViewportPreviewState.cpp
    src/scene/material/EditorMaterialAssetAuthoring.cpp
    src/scene/material/EditorMaterialAssetEditCommand.cpp
    src/scene/material/EditorMaterialAssetGateway.cpp
    src/scene/material/EditorMaterialGraphDebugLog.cpp
    src/scene/material/EditorMaterialReferenceFinder.cpp
    src/scene/material/MaterialEditorState.cpp
    src/scene/material/EditorSceneContextMaterialGraph.cpp
    src/scene/particle/EditorParticleEffectReferenceFinder.cpp
    src/scene/material/EditorEmbeddedMaterialAssetWriter.cpp
    src/scene/material/EditorEmbeddedMaterialExtractor.cpp
    src/scene/material/EditorEmbeddedMaterialTextureResolver.cpp
    src/scene/material_preview/EditorMaterialGraphCookService.cpp
    src/scene/material_preview/EditorMaterialPreviewMeshFactory.cpp
    src/scene/material_preview/EditorMaterialPreviewMeshLoader.cpp
    src/scene/material_preview/EditorMaterialPreviewScene.cpp
    src/scene/material_preview/EditorMaterialPreviewTelemetry.cpp
    src/scene/transform_edit/EditorSceneTransformCommitBuilder.cpp
    src/scene/transform_edit/EditorSceneTransformEditApplier.cpp
    src/scene/transform_edit/EditorSceneTransformEditController.cpp
    src/scene/transform_edit/EditorSceneTransformEditSession.cpp
    src/scene/transform_edit/EditorSceneTransformEquality.cpp
    src/scene/transform_edit/EditorSceneTransformMath.cpp
    src/scene/transform_edit/EditorSceneTransformSnapshotBuilder.cpp
    src/scene/transform_edit/EditorTransformProperty.cpp
    src/scene/input/EditorInputAssetGateway.cpp
    src/scene/input/EditorInputAssetCatalog.cpp
    src/scene/input/EditorInputActionAuthoring.cpp
    src/scene/input/EditorInputMappingContextAuthoring.cpp
    src/scene/audio/EditorAudioMixerAssetGateway.cpp
    src/scene/postprocess/EditorPostProcessProfileAssets.cpp
    src/private/scene/postprocess/EditorPostProcessProfileAssets.hpp
    src/private/platform/win32/EditorPostProcessProfileAssetPickerDialog.hpp
    src/scene/audio/EditorAudioMixerAuthoring.cpp
    src/scene/audio/EditorSceneAudioSettingsService.cpp
    src/windowing/EditorFloatingWindowFrame.cpp
    src/windowing/FloatingWindowControlActions.cpp
    src/windowing/FloatingWindowFactory.cpp
    src/windowing/FloatingWindowHitTestResolver.cpp
    src/windowing/FloatingWindowControlInteractor.cpp
    ../../third_party/heroicons/LICENSE
    ../../third_party/heroicons/README.third_party.md
    ../../third_party/heroicons/optimized/24/outline/minus.svg
    ../../third_party/heroicons/optimized/24/outline/stop.svg
    ../../third_party/heroicons/optimized/24/outline/x-mark.svg
    ../../third_party/heroicons/optimized/24/outline/chevron-down.svg
    ../../third_party/heroicons/optimized/24/outline/chevron-right.svg
    ../../third_party/heroicons/optimized/24/outline/cube.svg
    ../../third_party/heroicons/optimized/24/outline/ellipsis-horizontal.svg
    ../../third_party/heroicons/optimized/24/outline/eye.svg
    ../../third_party/heroicons/optimized/24/outline/magnifying-glass.svg
    ../../third_party/heroicons/optimized/24/outline/plus.svg
    ../../third_party/heroicons/optimized/24/outline/speaker-wave.svg
    ../../third_party/heroicons/optimized/16/solid/adjustments-horizontal.svg
    ../../third_party/heroicons/optimized/16/solid/command-line.svg
    ../../third_party/heroicons/optimized/16/solid/eye.svg
    ../../third_party/heroicons/optimized/16/solid/list-bullet.svg
    ../../third_party/heroicons/optimized/24/solid/cube.svg
    ../../third_party/heroicons/optimized/24/solid/folder.svg
    ../../third_party/heroicons/optimized/24/solid/sun.svg
    content/EditorShell/Icons/SceneView/HEROICONS-LICENSE.txt
    content/EditorShell/Icons/SceneView/sun-solid.svg
    ../../third_party/lucide/LICENSE
    ../../third_party/lucide/README.third_party.md
    ../../third_party/lucide/icons/gamepad-2.svg
    ../../third_party/lucide/icons/pause.svg
    ../../third_party/lucide/icons/play.svg
    ../../third_party/lucide/icons/square.svg
    ../../third_party/lucide/icons/step-forward.svg
)

target_include_directories(kb_editor
    PRIVATE
        include
        src/private
)

set_target_properties(kb_editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# The language catalogs are content: a changed translation reaches the editor's folders on the next
# build whenever a catalog changes, not only when the editor relinks.
file(GLOB KB_EDITOR_LOCALIZATION_CATALOGS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/content/EditorShell/Localization/*.kbloc")
get_property(KB_EDITOR_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(KB_EDITOR_MULTI_CONFIG)
    set(KB_EDITOR_RUNTIME_DIR "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
else()
    set(KB_EDITOR_RUNTIME_DIR "${CMAKE_BINARY_DIR}/bin")
endif()
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/kb_editor_localization.stamp"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/content/EditorShell/Localization"
        "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/Content/EditorShell/Localization"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/content/EditorShell/Localization"
        "${KB_EDITOR_RUNTIME_DIR}/Content/EditorShell/Localization"
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/kb_editor_localization.stamp"
    DEPENDS ${KB_EDITOR_LOCALIZATION_CATALOGS}
    VERBATIM)
add_custom_target(kb_editor_localization DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/kb_editor_localization.stamp")
add_dependencies(kb_editor kb_editor_localization)

# The World 2D Map Editor's tutorial pictures, copied like the language catalogs.
file(GLOB KB_EDITOR_MAP2D_CONTENT CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/content/EditorShell/Map2D/*")
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/kb_editor_map2d_content.stamp"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/content/EditorShell/Map2D"
        "${KB_EDITOR_RUNTIME_DIR}/Content/EditorShell/Map2D"
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/kb_editor_map2d_content.stamp"
    DEPENDS ${KB_EDITOR_MAP2D_CONTENT}
    VERBATIM)
add_custom_target(kb_editor_map2d_content DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/kb_editor_map2d_content.stamp")
add_dependencies(kb_editor kb_editor_map2d_content)

add_custom_command(TARGET kb_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:kb_editor>" "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/kb_editor.exe"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/Content/EditorShell/Fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/content/EditorShell/Fonts/DejaVuSans.ttf"
        "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/Content/EditorShell/Fonts/DejaVuSans.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/content/EditorShell/Fonts/DejaVu-LICENSE.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/Content/EditorShell/Fonts/DejaVu-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/jolt/Assets/Fonts/Roboto-Regular.ttf"
        "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/Content/EditorShell/Fonts/Roboto-Regular.ttf"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/Content/EditorShell/Icons/SceneView"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/content/EditorShell/Icons/SceneView/sun-solid.svg"
        "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/Content/EditorShell/Icons/SceneView/sun-solid.svg"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/content/EditorShell/Icons/SceneView/HEROICONS-LICENSE.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/Content/EditorShell/Icons/SceneView/HEROICONS-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/sources/plugins/21kb_particle/content"
        "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/Content/21kbParticle"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:kb_editor>/Content/EditorShell/Fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/content/EditorShell/Fonts/DejaVuSans.ttf"
        "$<TARGET_FILE_DIR:kb_editor>/Content/EditorShell/Fonts/DejaVuSans.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/content/EditorShell/Fonts/DejaVu-LICENSE.txt"
        "$<TARGET_FILE_DIR:kb_editor>/Content/EditorShell/Fonts/DejaVu-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/jolt/Assets/Fonts/Roboto-Regular.ttf"
        "$<TARGET_FILE_DIR:kb_editor>/Content/EditorShell/Fonts/Roboto-Regular.ttf"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:kb_editor>/Content/EditorShell/Icons/SceneView"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/content/EditorShell/Icons/SceneView/sun-solid.svg"
        "$<TARGET_FILE_DIR:kb_editor>/Content/EditorShell/Icons/SceneView/sun-solid.svg"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/content/EditorShell/Icons/SceneView/HEROICONS-LICENSE.txt"
        "$<TARGET_FILE_DIR:kb_editor>/Content/EditorShell/Icons/SceneView/HEROICONS-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/sources/plugins/21kb_particle/content"
        "$<TARGET_FILE_DIR:kb_editor>/Content/21kbParticle"
    VERBATIM
)

target_link_libraries(kb_editor
    PRIVATE
        kb_engine
        kb_renderer
        kb_terrain_editor_core
        kb_world2d_core
        kb_21kb_particle_editor_core
)

if(TARGET kb_physics_jolt_plugin)
    add_dependencies(kb_editor kb_physics_jolt_plugin)
    if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
        set(KB_PHYSICS_JOLT_EDITOR_PLUGIN_PATH "$<TARGET_FILE:kb_physics_jolt_plugin>")
    else()
        set(KB_PHYSICS_JOLT_EDITOR_PLUGIN_PATH "")
    endif()
    target_compile_definitions(kb_editor
        PRIVATE
            "KB_PHYSICS_JOLT_PLUGIN_PATH=\"${KB_PHYSICS_JOLT_EDITOR_PLUGIN_PATH}\""
    )
endif()

if(TARGET kb_audio_miniaudio_plugin)
    add_dependencies(kb_editor kb_audio_miniaudio_plugin)
    if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
        set(KB_AUDIO_MINIAUDIO_EDITOR_PLUGIN_PATH "$<TARGET_FILE:kb_audio_miniaudio_plugin>")
    else()
        set(KB_AUDIO_MINIAUDIO_EDITOR_PLUGIN_PATH "")
    endif()
    target_compile_definitions(kb_editor
        PRIVATE
            "KB_AUDIO_MINIAUDIO_PLUGIN_PATH=\"${KB_AUDIO_MINIAUDIO_EDITOR_PLUGIN_PATH}\""
    )
endif()

if(TARGET kb_basic_lighting_plugin)
    add_dependencies(kb_editor kb_basic_lighting_plugin)
    if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
        set(KB_BASIC_LIGHTING_EDITOR_PLUGIN_PATH "$<TARGET_FILE:kb_basic_lighting_plugin>")
    else()
        set(KB_BASIC_LIGHTING_EDITOR_PLUGIN_PATH "")
    endif()
    target_compile_definitions(kb_editor
        PRIVATE
            "KB_BASIC_LIGHTING_PLUGIN_PATH=\"${KB_BASIC_LIGHTING_EDITOR_PLUGIN_PATH}\""
    )
endif()

if(TARGET kb_ai_plugin)
    add_dependencies(kb_editor kb_ai_plugin)
    if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
        set(KB_AI_EDITOR_PLUGIN_PATH "$<TARGET_FILE:kb_ai_plugin>")
    else()
        set(KB_AI_EDITOR_PLUGIN_PATH "")
    endif()
    target_compile_definitions(kb_editor
        PRIVATE
            "KB_AI_PLUGIN_PATH=\"${KB_AI_EDITOR_PLUGIN_PATH}\""
    )
endif()

if(TARGET kb_terrain_editor_plugin)
    add_dependencies(kb_editor kb_terrain_editor_plugin)
endif()

if(TARGET kb_world2d_plugin)
    add_dependencies(kb_editor kb_world2d_plugin)
    if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
        set(KB_WORLD2D_EDITOR_PLUGIN_PATH "$<TARGET_FILE:kb_world2d_plugin>")
    else()
        set(KB_WORLD2D_EDITOR_PLUGIN_PATH "")
    endif()
    target_compile_definitions(kb_editor
        PRIVATE
            "KB_WORLD2D_PLUGIN_PATH=\"${KB_WORLD2D_EDITOR_PLUGIN_PATH}\""
    )
endif()

target_link_libraries(kb_editor PRIVATE user32 gdi32 gdiplus ole32 dwmapi msimg32 comdlg32 shell32 xinput)
target_link_libraries(kb_editor PRIVATE kb_crash_reporting)
# Build Game signs Windows packages with the signer shipped beside the editor.
add_dependencies(kb_editor kb_authenticode_signer)
target_compile_definitions(kb_editor PRIVATE
    "KB_EDITOR_ENGINE_ROOT=\"${CMAKE_SOURCE_DIR}\""
    "KB_EDITOR_BUILD_ROOT=\"${CMAKE_BINARY_DIR}\""
)

# MAT-30: graph shader cook toolchain (shaderc + engine shader sources) wired into the editor so
# EditorMaterialGraphCookService can cook authored graphs into per-backend binaries at runtime.
set(KB_GRAPH_SHADERC_TOOL "" CACHE FILEPATH "Path to bgfx shaderc used by the editor graph shader cook service")
set(_kb_editor_graph_shaderc_tool "")
if(TARGET bgfx::shaderc)
    set(_kb_editor_graph_shaderc_tool "$<PATH:CMAKE_PATH,$<TARGET_FILE:bgfx::shaderc>>")
    add_dependencies(kb_editor bgfx::shaderc)
elseif(KB_GRAPH_SHADERC_TOOL)
    cmake_path(CONVERT "${KB_GRAPH_SHADERC_TOOL}" TO_CMAKE_PATH_LIST _kb_editor_graph_shaderc_tool NORMALIZE)
endif()

function(kb_apply_editor_graph_cook_defs _target)
    # The engine shader-source paths are always known at configure time, so bake them unconditionally.
    # (Previously they were gated behind the shaderc tool, so when shaderc was not a CMake target NONE of
    # them were defined and the cook wrote an empty varying def -> "No input/output semantic".)
    target_compile_definitions(${_target}
        PRIVATE
            KB_EDITOR_GRAPH_SHADER_VARYING_DEF="${CMAKE_SOURCE_DIR}/sources/renderer/shaders/varying.def.sc"
            KB_EDITOR_GRAPH_SHADER_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/sources/renderer/shaders"
            KB_EDITOR_GRAPH_BGFX_SHADER_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake/bgfx/src"
    )
    if(_kb_editor_graph_shaderc_tool)
        target_compile_definitions(${_target} PRIVATE KB_EDITOR_GRAPH_SHADERC_PATH="${_kb_editor_graph_shaderc_tool}")
    endif()
endfunction()

kb_apply_editor_graph_cook_defs(kb_editor)

# EditorSceneContext is an intentionally centralized Win32 orchestration translation unit. Its
# generated debug object exceeds COFF's default section count once all production editor surfaces
# are enabled; /bigobj changes only the object-file index width and has no runtime cost.
if(MSVC)
    set_source_files_properties(src/scene/EditorSceneContext.cpp PROPERTIES COMPILE_OPTIONS "/bigobj")
    set_source_files_properties(src/scene/material/EditorSceneContextMaterialGraph.cpp PROPERTIES COMPILE_OPTIONS "/bigobj")
endif()

if(TARGET kb_21kb_particle_plugin)
    add_dependencies(kb_editor kb_21kb_particle_plugin)
    if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
        set(KB_21KB_PARTICLE_EDITOR_PLUGIN_PATH "$<TARGET_FILE:kb_21kb_particle_plugin>")
        # Staged on every build, so a rebuilt provider replaces the copy even when kb_editor does not relink.
        add_custom_target(kb_editor_stage_particle_plugin
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:kb_editor>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:kb_21kb_particle_plugin>"
                "$<TARGET_FILE_DIR:kb_editor>"
            VERBATIM
        )
        add_dependencies(kb_editor_stage_particle_plugin kb_21kb_particle_plugin)
        include("${CMAKE_SOURCE_DIR}/CMake/SerializeParticleRuntimeStaging.cmake")
        kb_serialize_particle_runtime_staging(kb_editor_stage_particle_plugin)
        add_dependencies(kb_editor kb_editor_stage_particle_plugin)
    else()
        set(KB_21KB_PARTICLE_EDITOR_PLUGIN_PATH "")
    endif()
    target_compile_definitions(kb_editor PRIVATE
        "KB_21KB_PARTICLE_PLUGIN_PATH=\"${KB_21KB_PARTICLE_EDITOR_PLUGIN_PATH}\""
        "KB_21KB_PARTICLE_CONTENT_ROOT=\"$<TARGET_FILE_DIR:kb_editor>/Content/21kbParticle\""
    )
endif()

if(WIN32)
    # UTF-8 process code page: the interface in any of its 33 languages and names in any letters.
    target_sources(kb_editor PRIVATE kb_editor.manifest)
endif()

kb_target_copy_bgfx_runtime_shaders(kb_editor)
kb_apply_quality_gates(kb_editor)

if(BUILD_TESTING)
    add_executable(kb_editor_build_game_tests
        tests/EditorBuildGameTests.cpp
        src/rendering/BuildGamePanelLayout.cpp
        src/rendering/BuildGamePanelModel.cpp
        src/localization/EditorLocalization.cpp
        src/settings/EditorBuildGameSettingsStore.cpp
        src/packaging/EditorProjectPackageService.cpp
        src/packaging/EditorAndroidSigningBroker.cpp
        src/packaging/EditorPackageProcessEnvironment.cpp
        src/packaging/EditorProjectIconTransaction.cpp
        src/packaging/EditorSigningProcess.cpp
        src/packaging/EditorWindowsSigningBroker.cpp
    )
    target_include_directories(kb_editor_build_game_tests PRIVATE include src/private)
    add_dependencies(kb_editor_build_game_tests kb_authenticode_signer)
    target_compile_definitions(kb_editor_build_game_tests PRIVATE
        "KB_EDITOR_BUILD_GAME_TEST_ROOT=\"${CMAKE_BINARY_DIR}/editor-build-game-test-scratch\""
        "KB_AUTHENTICODE_SIGNER=\"$<TARGET_FILE:kb_authenticode_signer>\""
    )
    target_link_libraries(kb_editor_build_game_tests PRIVATE kb_engine kb_crash_reporting ole32 shell32 wintrust crypt32)
    add_test(NAME kb_editor_build_game_tests COMMAND $<TARGET_FILE:kb_editor_build_game_tests>)
    set_tests_properties(kb_editor_build_game_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    kb_apply_quality_gates(kb_editor_build_game_tests)

    add_executable(kb_editor_particle_authoring_tests
        tests/ParticleEditorAuthoringTests.cpp
        src/rendering/ParticleEditorPanelLayout.cpp
    )
    target_include_directories(kb_editor_particle_authoring_tests PRIVATE include src/private)
    target_link_libraries(kb_editor_particle_authoring_tests PRIVATE kb_21kb_particle_editor_core)
    add_test(NAME kb_editor_particle_authoring_tests
        COMMAND $<TARGET_FILE:kb_editor_particle_authoring_tests>)
    set_tests_properties(kb_editor_particle_authoring_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    kb_apply_quality_gates(kb_editor_particle_authoring_tests)

    add_executable(kb_editor_particle_bake_host_tests
        tests/EditorParticleBakeHostTests.cpp
        src/console/EditorConsoleState.cpp
        src/scene/ParticleEditorBakeHostCommand.cpp
    )
    target_include_directories(kb_editor_particle_bake_host_tests PRIVATE src/private)
    target_link_libraries(kb_editor_particle_bake_host_tests PRIVATE kb_21kb_particle_editor_core)
    add_test(NAME kb_editor_particle_bake_host_tests
        COMMAND $<TARGET_FILE:kb_editor_particle_bake_host_tests>)
    set_tests_properties(kb_editor_particle_bake_host_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    kb_apply_quality_gates(kb_editor_particle_bake_host_tests)

    add_executable(kb_editor_tests
        tests/EditorHierarchyTests.cpp
        tests/EditorAssetBrowserTests.cpp
        tests/EditorAudioMixerAuthoringTests.cpp
        tests/EditorAudioMixerInspectorTests.cpp
        tests/EditorAudioMixerRuntimeTests.cpp
        tests/EditorSceneAudioInspectorTests.cpp
        tests/EditorViewportPreviewTests.cpp
        tests/EditorMaterialGraphCookServiceTests.cpp
        tests/EditorDockingTests.cpp
        tests/EditorProjectTests.cpp
        tests/EditorSettingsTests.cpp
        tests/EditorLocalizationTests.cpp
        tests/EditorInspectorTests.cpp
        tests/EditorMaterialAssetAuthoringTests.cpp
        tests/EditorMaterialGraphCanvasTests.cpp
        tests/EditorRuntimeTests.cpp
        tests/EditorPlayModeSceneSessionTests.cpp
        tests/EditorCommandStackTests.cpp
        tests/EditorTestSuites.hpp
        tests/EditorTestSupport.hpp
        tests/ScriptEditorTests.cpp
        tests/SvgPathTests.cpp
        src/rendering/MaterialEditorPanelLayout.cpp
        src/rendering/material_graph/EditorMaterialGraphPinPresentation.cpp
        src/rendering/EditorTexturePreviewService.cpp
        src/rendering/InspectorAudioMixerAssetView.cpp
        src/rendering/InspectorSceneAudioView.cpp
        src/rendering/EditorBgfxBackendSelector.cpp
        src/rendering/HeroIconGdiplusRuntime.cpp
        src/rendering/material_graph/MaterialGraphCanvasDocumentAdapter.cpp
        src/rendering/material_graph/MaterialGraphCanvas.cpp
        src/rendering/script_editor/LuaSyntaxHighlighter.cpp
        src/rendering/script_editor/ScriptEditorDocument.cpp
        src/docking/DefaultDockWorkspace.cpp
        src/docking/DockCenterPanelInserter.cpp
        src/docking/DockDropEdgeBandClassifier.cpp
        src/docking/DockDropPlacementPolicy.cpp
        src/docking/DockDropPreviewFactory.cpp
        src/docking/DockDropPreviewResolver.cpp
        src/docking/DockDropZoneClassifier.cpp
        src/docking/DockDropZoneDepthSelector.cpp
        src/docking/DockGeometry.cpp
        src/docking/DockHitTester.cpp
        src/docking/DockLayoutBuilder.cpp
        src/docking/DockLeafLayoutBuilder.cpp
        src/docking/DockLayoutSerializer.cpp
    src/docking/DockLeafPanelOrder.cpp
        src/docking/DockMainLayoutResolver.cpp
        src/docking/DockModelQueries.cpp
        src/docking/DockNodeFactory.cpp
        src/docking/DockNodeFinder.cpp
        src/docking/DockNodeIdSource.cpp
        src/docking/DockNodeLayoutBuilder.cpp
        src/docking/DockNodeQuery.cpp
        src/docking/DockNodeSlotFinder.cpp
        src/docking/DockPanelCollection.cpp
        src/docking/DockPanelDocking.cpp
        src/docking/DockPanelRemover.cpp
        src/docking/DockSplitLayoutBuilder.cpp
        src/docking/DockSplitPanelInserter.cpp
        src/docking/DockSplitterResizer.cpp
        src/docking/DockTabIndexResolver.cpp
        src/docking/DockTreeMutation.cpp
        src/docking/DockTreePruner.cpp
        src/docking/DockTypes.cpp
        src/docking/EditorDockModel.cpp
        src/docking/EditorDockModelCommands.cpp
        src/docking/EditorDockModelQueries.cpp
            src/settings/EditorConfigurationStore.cpp
            src/docking/EditorWorkspaceArrangement.cpp
            src/rendering/FloatingPanelGeometry.cpp
            src/rendering/gdi/EditorFontCache.cpp
            src/settings/EditorLayoutLibrary.cpp
            src/settings/EditorLayoutMenuModel.cpp
            src/settings/EditorPanelSessionText.cpp
        src/rendering/SvgPathArcMath.cpp
        src/rendering/SvgPathCursor.cpp
        src/assets/EditorAssetBrowserAssetRows.cpp
        src/assets/EditorAssetBrowserBreadcrumbHitTester.cpp
        src/assets/EditorAssetBrowserBottomBarLayout.cpp
        src/assets/EditorAssetBrowserChromeHitTester.cpp
        src/assets/EditorAssetBrowserContentLayout.cpp
        src/assets/EditorAssetBrowserContextMenuState.cpp
        src/app/EditorAssetBrowserNativeCommandMap.cpp
        src/app/EditorAudioInspectorDropPolicy.cpp
        src/assets/EditorAssetBrowserContextMenuLayout.cpp
        src/assets/EditorAssetBrowserContentHitTester.cpp
        src/assets/EditorAssetBrowserDeleteConfirmState.cpp
        src/assets/EditorAssetBrowserFolderExpansionState.cpp
        src/assets/EditorAssetBrowserFolderRowBuilder.cpp
        src/assets/EditorAssetBrowserGeometry.cpp
        src/assets/EditorAssetBrowserHitPayloadResolver.cpp
        src/assets/EditorAssetBrowserHitTester.cpp
        src/assets/EditorAssetBrowserLayout.cpp
        src/assets/EditorAssetBrowserOverlayHitTester.cpp
        src/assets/EditorAssetBrowserPanelLayoutBuilder.cpp
        src/assets/EditorAssetBrowserPathUtils.cpp
        src/assets/EditorAssetBrowserSelectionModel.cpp
        src/assets/EditorAssetBrowserSelectionState.cpp
        src/assets/EditorAssetBrowserState.cpp
        src/assets/EditorAssetBrowserStateContext.cpp
        src/assets/EditorAssetBrowserStateRows.cpp
        src/assets/EditorAssetBrowserStateSelection.cpp
        src/assets/EditorAssetBrowserStateTextEdit.cpp
        src/assets/EditorAssetBrowserStateView.cpp
        src/assets/EditorAssetBrowserTextEditState.cpp
        src/assets/EditorAssetBrowserToolbarLayout.cpp
        src/assets/EditorAssetBrowserTreeHitTester.cpp
        src/assets/EditorAssetBrowserViewState.cpp
        src/commands/EditorCommandStack.cpp
        src/commands/EditorSceneHistoryCommand.cpp
            src/scene/EditorHierarchyExpansionState.cpp
        src/scene/EditorHierarchyRowBuilder.cpp
        src/scene/EditorHierarchyRowFactory.cpp
        src/scene/EditorHierarchySearchMatcher.cpp
        src/scene/EditorHierarchySearchState.cpp
        src/scene/EditorHierarchySelectionNormalizer.cpp
        src/scene/EditorHierarchySelectionState.cpp
        src/scene/EditorPluginCatalog.cpp
        src/scene/EditorSceneAssetBrowserCommands.cpp
        src/scene/audio/EditorAudioMixerAssetGateway.cpp
        src/scene/audio/EditorAudioMixerAuthoring.cpp
        src/scene/audio/EditorAudioMixerRuntime.cpp
        src/app/EditorAudioAssetPreview.cpp
        src/inspection/InspectorAudioMixerRuntimeInteraction.cpp
        src/scene/audio/EditorSceneAudioSettingsService.cpp
        src/scene/EditorSceneSelectionPivot.cpp
        src/scene/EditorSceneHierarchyActions.cpp
        src/scene/EditorHierarchyObjectFactory.cpp
        src/scene/EditorSceneAudioAssetActions.cpp
        src/scene/EditorSceneCommandController.cpp
        src/scene/EditorSceneViewportStateStore.cpp
          src/scene/EditorSceneMaterialAssetActions.cpp
          src/scene/EditorSceneMeshAssetActions.cpp
          src/scene/EditorScenePrefabActions.cpp
          src/scene/EditorViewportCameraState.cpp
          src/scene/EditorViewportPreviewState.cpp
          src/scene/EditorAnimationPreviewScene.cpp
          src/scene/EditorTimelinePreviewSession.cpp
          src/scene/EditorBlendSpace2DPreviewSession.cpp
          src/scene/TimelineEditorModel.cpp
          tests/EditorTimelineTests.cpp
          tests/EditorBlendSpace2DTests.cpp
          src/scene/GameplayGraphEditorState.cpp
          tests/EditorGameplayGraphTests.cpp
          src/app/scene_viewport/EditorSceneViewportMath.cpp
          src/app/scene_viewport/EditorSceneViewportMeshPicker.cpp
      src/scene/material/EditorMaterialAssetAuthoring.cpp
    src/scene/material/EditorMaterialAssetEditCommand.cpp
    src/scene/material/EditorMaterialAssetGateway.cpp
    src/scene/material/EditorMaterialReferenceFinder.cpp
    src/scene/material/MaterialEditorState.cpp
    src/scene/particle/EditorParticleEffectReferenceFinder.cpp
    src/scene/material/EditorEmbeddedMaterialAssetWriter.cpp
    src/scene/material/EditorEmbeddedMaterialExtractor.cpp
    src/scene/material/EditorEmbeddedMaterialTextureResolver.cpp
    src/scene/material_preview/EditorMaterialGraphCookService.cpp
    src/scene/material_preview/EditorMaterialPreviewMeshFactory.cpp
        src/scene/material_preview/EditorMaterialPreviewMeshLoader.cpp
        src/scene/material_preview/EditorMaterialPreviewScene.cpp
        src/scene/material_preview/EditorMaterialPreviewTelemetry.cpp
        src/app/EditorPlayModeState.cpp
        src/app/EditorPlayModeSceneSession.cpp
        src/app/EditorShellInteractionState.cpp
        src/project/EditorProjectBootstrap.cpp
        src/project/EditorProjectPaths.cpp
        src/rendering/EditorRenderBackendSettings.cpp
        src/rendering/EditorToolbarLayout.cpp
        src/rendering/scene_viewport_toolbar/SceneViewportToolbarLayout.cpp
        src/rendering/scene_viewport_toolbar/SceneViewportToolbarState.cpp
        src/rendering/SvgGraphicsPathBuilder.cpp
        src/rendering/SvgPathCursor.cpp
        src/rendering/SvgPathFigureBuilder.cpp
        src/rendering/SvgPathArcConverter.cpp
        src/rendering/SvgPathArcMath.cpp
        src/rendering/SvgPathArcSegment.cpp
        src/assets/EditorAssetOpenPolicy.cpp
        src/rendering/ProjectFilesAssetIconResolver.cpp
        src/rendering/ProjectFilesMaterialPreviewThumbnailModel.cpp
        src/rendering/EditorMeshPreviewRasterizer.cpp
        src/rendering/EditorMeshPreviewService.cpp
        src/rendering/EditorMeshThumbnailDiskCache.cpp
        src/rendering/EditorMeshThumbnailService.cpp
        src/rendering/MaterialPreviewAppearanceResolver.cpp
        src/inspection/InspectorAudioTextBuilder.cpp
        src/inspection/InspectorAudioMixerAssetInteraction.cpp
        src/inspection/InspectorAudioMixerAssetModel.cpp
        src/inspection/InspectorSceneAudioInteraction.cpp
        src/inspection/InspectorSceneAudioModel.cpp
        src/inspection/InspectorAudioComponentModel.cpp
        src/inspection/InspectorAudioScrubController.cpp
        src/inspection/InspectorComponentCatalog.cpp
        src/inspection/InspectorSceneColor.cpp
        src/localization/EditorLocalization.cpp
        src/inspection/InspectorComponentLabelFormatter.cpp
        src/inspection/InspectorPhysicsModel.cpp
        src/inspection/ui/InspectorUIComponentModel.cpp
        src/inspection/ui/InspectorUIThemeModel.cpp
        src/scene/ui/EditorUIThemeAssets.cpp
        src/inspection/InspectorAddComponentBrowserModel.cpp
        src/inspection/InspectorMeshRendererMaterialSlotModel.cpp
        src/inspection/InspectorMaterialTextureSlotFormatter.cpp
    src/inspection/EditorValueFormatter.cpp
    src/inspection/MaterialAssetFormatter.cpp
        src/inspection/InspectorPanelState.cpp
        src/scene/ui/EditorUIComponentAuthoring.cpp
        src/console/EditorConsoleState.cpp
    )

    target_include_directories(kb_editor_tests
        PRIVATE
            include
            src/private
            tests
    )
    if(KB_BUILD_PROVIDER_MODULES_AS_DLL AND TARGET kb_audio_miniaudio_plugin)
        set_property(SOURCE tests/EditorAudioMixerRuntimeTests.cpp APPEND PROPERTY
            COMPILE_DEFINITIONS KB_EDITOR_TEST_AUDIO_PLUGIN="$<TARGET_FILE:kb_audio_miniaudio_plugin>")
        add_dependencies(kb_editor_tests kb_audio_miniaudio_plugin)
        add_test(NAME kb_editor_audio_mixer_runtime COMMAND $<TARGET_FILE:kb_editor_tests> audio-mixer-runtime)
        set_tests_properties(kb_editor_audio_mixer_runtime PROPERTIES LABELS "editor;audio" TIMEOUT 30)
    endif()

    target_link_libraries(kb_editor_tests
        PRIVATE
            kb_engine
            kb_renderer
            kb_21kb_particle_editor_core
            gdiplus
            ole32
    )

    kb_apply_editor_graph_cook_defs(kb_editor_tests)
    if(TARGET bgfx::shaderc)
        add_dependencies(kb_editor_tests bgfx::shaderc)
    endif()
    target_compile_definitions(kb_editor_tests
        PRIVATE
            KB_EDITOR_GRAPH_SHADER_CACHE_DIR="${CMAKE_BINARY_DIR}/editor_graph_cook_tests"
            KB_EDITOR_LOCALIZATION_DIR="${CMAKE_CURRENT_SOURCE_DIR}/content/EditorShell/Localization"
    )

    add_test(NAME kb_editor_tests COMMAND $<TARGET_FILE:kb_editor_tests>)
    add_test(NAME kb_editor_timeline_tests COMMAND $<TARGET_FILE:kb_editor_tests> timeline)
    add_test(NAME kb_editor_gameplay_graph_tests COMMAND $<TARGET_FILE:kb_editor_tests> gameplay-graph)
    set_tests_properties(kb_editor_gameplay_graph_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} LABELS "editor;gameplay;graph" TIMEOUT 60)
    set_tests_properties(kb_editor_timeline_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} LABELS "editor;timeline" TIMEOUT 60)
    if(TARGET kb_cooker AND TARGET kb_game AND TARGET kb_engine_tests
        AND TARGET kb_physics_jolt_plugin AND TARGET kb_audio_miniaudio_plugin
        AND TARGET kb_basic_lighting_plugin AND TARGET kb_ai_plugin AND TARGET kb_21kb_particle_plugin AND TARGET kb_world2d_plugin)
        find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
        set(_kb_timeline_shaderc "${KB_COOKER_SHADERC}")
        if(TARGET bgfx::shaderc)
            set(_kb_timeline_shaderc "$<TARGET_FILE:bgfx::shaderc>")
        endif()
        set(_kb_timeline_runtime_args)
        if(MSVC AND KB_ENABLE_SANITIZERS)
            list(APPEND _kb_timeline_runtime_args --runtime-dll "${KB_MSVC_ASAN_DYNAMIC_RUNTIME}")
        endif()
        add_test(NAME kb_editor_timeline_workflow_package
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/TimelineWorkflowTests.py"
                --editor $<TARGET_FILE:kb_editor> --engine-tests $<TARGET_FILE:kb_engine_tests>
                --cooker $<TARGET_FILE:kb_cooker> --player $<TARGET_FILE:kb_game>
                --engine-root "${CMAKE_SOURCE_DIR}" --shaderc "${_kb_timeline_shaderc}" ${_kb_timeline_runtime_args}
                --test-root "${CMAKE_BINARY_DIR}/timeline_workflow_tests"
                --plugin $<TARGET_FILE:kb_physics_jolt_plugin> --plugin $<TARGET_FILE:kb_audio_miniaudio_plugin>
                --plugin $<TARGET_FILE:kb_basic_lighting_plugin> --plugin $<TARGET_FILE:kb_ai_plugin>
                --plugin $<TARGET_FILE:kb_21kb_particle_plugin> --plugin $<TARGET_FILE:kb_world2d_plugin>)
        set_tests_properties(kb_editor_timeline_workflow_package PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;timeline;packaging" TIMEOUT 1200)
        if(TARGET kb_cli)
            add_test(NAME kb_editor_blend_space_2d_workflow_package
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/BlendSpace2DWorkflowTests.py"
                    --editor $<TARGET_FILE:kb_editor> --cli $<TARGET_FILE:kb_cli>
                    --cooker $<TARGET_FILE:kb_cooker> --player $<TARGET_FILE:kb_game>
                    --engine-root "${CMAKE_SOURCE_DIR}" --shaderc "${_kb_timeline_shaderc}" ${_kb_timeline_runtime_args}
                    --test-root "${CMAKE_BINARY_DIR}/blend_space_workflow_tests"
                    --plugin $<TARGET_FILE:kb_physics_jolt_plugin> --plugin $<TARGET_FILE:kb_audio_miniaudio_plugin>
                    --plugin $<TARGET_FILE:kb_basic_lighting_plugin> --plugin $<TARGET_FILE:kb_ai_plugin>
                    --plugin $<TARGET_FILE:kb_21kb_particle_plugin> --plugin $<TARGET_FILE:kb_world2d_plugin>)
            set_tests_properties(kb_editor_blend_space_2d_workflow_package PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;animation;packaging" TIMEOUT 1200)
            add_test(NAME kb_editor_gameplay_graph_workflow_package
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/GameplayGraphWorkflowTests.py"
                    --editor $<TARGET_FILE:kb_editor> --cli $<TARGET_FILE:kb_cli>
                    --cooker $<TARGET_FILE:kb_cooker> --player $<TARGET_FILE:kb_game>
                    --engine-root "${CMAKE_SOURCE_DIR}" --shaderc "${_kb_timeline_shaderc}" ${_kb_timeline_runtime_args}
                    --test-root "${CMAKE_BINARY_DIR}/gameplay_graph_workflow_tests"
                    --plugin $<TARGET_FILE:kb_physics_jolt_plugin> --plugin $<TARGET_FILE:kb_audio_miniaudio_plugin>
                    --plugin $<TARGET_FILE:kb_basic_lighting_plugin> --plugin $<TARGET_FILE:kb_ai_plugin>
                    --plugin $<TARGET_FILE:kb_21kb_particle_plugin> --plugin $<TARGET_FILE:kb_world2d_plugin>)
            set_tests_properties(kb_editor_gameplay_graph_workflow_package PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;gameplay;graph;packaging" TIMEOUT 1200)
            if(TARGET kb_ecs_graph_module_fixture AND TARGET kb_ecs_graph_project_fixture)
                add_test(NAME kb_editor_ecs_graph_workflow_package
                    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/EcsGraphWorkflowTests.py"
                        --editor $<TARGET_FILE:kb_editor> --cli $<TARGET_FILE:kb_cli>
                        --cooker $<TARGET_FILE:kb_cooker> --player $<TARGET_FILE:kb_game>
                        --module $<TARGET_FILE:kb_ecs_graph_module_fixture>
                        --project-tool $<TARGET_FILE:kb_ecs_graph_project_fixture>
                        --engine-root "${CMAKE_SOURCE_DIR}" --shaderc "${_kb_timeline_shaderc}" ${_kb_timeline_runtime_args}
                        --test-root "${CMAKE_BINARY_DIR}/ecs_graph_workflow_tests"
                        --plugin $<TARGET_FILE:kb_physics_jolt_plugin> --plugin $<TARGET_FILE:kb_audio_miniaudio_plugin>
                        --plugin $<TARGET_FILE:kb_basic_lighting_plugin> --plugin $<TARGET_FILE:kb_ai_plugin>
                        --plugin $<TARGET_FILE:kb_21kb_particle_plugin> --plugin $<TARGET_FILE:kb_world2d_plugin>)
                set_tests_properties(kb_editor_ecs_graph_workflow_package PROPERTIES
                    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;ecs;modules;packaging" TIMEOUT 1200)
            endif()
        endif()
    endif()
    set_tests_properties(kb_editor_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

    if(TARGET kb_cli AND TARGET kb_cooker AND TARGET kb_game AND KB_BUILD_PROVIDER_MODULES_AS_DLL
        AND TARGET kb_physics_jolt_plugin AND TARGET kb_audio_miniaudio_plugin
        AND TARGET kb_basic_lighting_plugin AND TARGET kb_21kb_particle_plugin AND TARGET kb_ai_plugin
        AND TARGET kb_world2d_plugin)
        find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
        set(_kb_editor_dsp_shaderc "${KB_COOKER_SHADERC}")
        if(TARGET bgfx::shaderc)
            set(_kb_editor_dsp_shaderc "$<TARGET_FILE:bgfx::shaderc>")
        endif()
        set(_kb_editor_dsp_runtime_args)
        if(MSVC AND KB_ENABLE_SANITIZERS)
            list(APPEND _kb_editor_dsp_runtime_args "--runtime-dll" "${KB_MSVC_ASAN_DYNAMIC_RUNTIME}")
        endif()
        add_test(NAME kb_editor_audio_dsp_package COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/sources/game/tests/AudioDspPackagedTests.py"
            "--editor-tests" "$<TARGET_FILE:kb_editor_tests>"
            "--cli" "$<TARGET_FILE:kb_cli>" "--cooker" "$<TARGET_FILE:kb_cooker>"
            "--player" "$<TARGET_FILE:kb_game>" "--engine-root" "${PROJECT_SOURCE_DIR}"
            "--test-root" "${CMAKE_BINARY_DIR}/editor_audio_dsp_package_tests"
            "--shaderc" "${_kb_editor_dsp_shaderc}" ${_kb_editor_dsp_runtime_args}
            "--plugin" "$<TARGET_FILE:kb_physics_jolt_plugin>"
            "--plugin" "$<TARGET_FILE:kb_audio_miniaudio_plugin>"
            "--plugin" "$<TARGET_FILE:kb_basic_lighting_plugin>"
            "--plugin" "$<TARGET_FILE:kb_21kb_particle_plugin>"
            "--plugin" "$<TARGET_FILE:kb_ai_plugin>"
            "--plugin" "$<TARGET_FILE:kb_world2d_plugin>")
        set_tests_properties(kb_editor_audio_dsp_package PROPERTIES
            LABELS "editor;audio;runtime;packaging" TIMEOUT 180)
    endif()

    add_test(NAME kb_editor_script_editor_tests COMMAND $<TARGET_FILE:kb_editor_tests> script-editor)
    set_tests_properties(kb_editor_script_editor_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

    add_test(NAME kb_editor_user_widget_headless
        COMMAND $<TARGET_FILE:kb_editor>
            --selftest-scenario "${CMAKE_CURRENT_SOURCE_DIR}/tests/HeadlessUserWidgetScenario.json"
            --selftest-root "${CMAKE_BINARY_DIR}/SelfTest"
            --selftest-task "SELFTEST-UI-UserWidget")
    set_tests_properties(kb_editor_user_widget_headless PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;runtime;headless;ui")
    add_test(NAME kb_editor_play_history_headless
        COMMAND $<TARGET_FILE:kb_editor>
            --selftest-scenario "${CMAKE_CURRENT_SOURCE_DIR}/tests/HeadlessPlayHistoryScenario.json"
            --selftest-root "${CMAKE_BINARY_DIR}/SelfTest"
            --selftest-task "SELFTEST-Play-History")
    set_tests_properties(kb_editor_play_history_headless PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;runtime;headless;scene" TIMEOUT 180)
    add_test(NAME kb_editor_ui_component_catalog_headless
        COMMAND $<TARGET_FILE:kb_editor>
            --selftest-scenario "${CMAKE_CURRENT_SOURCE_DIR}/tests/HeadlessUIComponentCatalogScenario.json"
            --selftest-root "${CMAKE_BINARY_DIR}/SelfTest"
            --selftest-task "SELFTEST-UI-Catalog")
    set_tests_properties(kb_editor_ui_component_catalog_headless PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;runtime;headless;ui")
    add_test(NAME kb_editor_ui_theme_headless
        COMMAND $<TARGET_FILE:kb_editor>
            --selftest-scenario "${CMAKE_CURRENT_SOURCE_DIR}/tests/UIThemeWorkflowScenario.json"
            --selftest-root "${CMAKE_BINARY_DIR}/SelfTest"
            --selftest-task "SELFTEST-UI-Theme")
    set_tests_properties(kb_editor_ui_theme_headless PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;runtime;headless;ui" TIMEOUT 180)
    add_test(NAME kb_editor_localization_headless
        COMMAND $<TARGET_FILE:kb_editor>
            --selftest-scenario "${CMAKE_CURRENT_SOURCE_DIR}/tests/HeadlessLocalizationScenario.json"
            --selftest-root "${CMAKE_BINARY_DIR}/SelfTest"
            --selftest-task "SELFTEST-Localization")
    set_tests_properties(kb_editor_localization_headless PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;runtime;headless;ui" TIMEOUT 180)
    add_test(NAME kb_editor_ui_anchors_headless
        COMMAND $<TARGET_FILE:kb_editor>
            --selftest-scenario "${CMAKE_CURRENT_SOURCE_DIR}/tests/HeadlessUIAnchorsScenario.json"
            --selftest-root "${CMAKE_BINARY_DIR}/SelfTest"
            --selftest-task "SELFTEST-UI-Anchors")
    set_tests_properties(kb_editor_ui_anchors_headless PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;runtime;headless;ui")
    add_test(NAME kb_editor_world_partition_headless
        COMMAND $<TARGET_FILE:kb_editor>
            --selftest-scenario "${CMAKE_CURRENT_SOURCE_DIR}/tests/HeadlessWorldPartitionScenario.json"
            --selftest-root "${CMAKE_BINARY_DIR}/SelfTest"
            --selftest-task "SELFTEST-World-Partition")
    set_tests_properties(kb_editor_world_partition_headless PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;runtime;headless;world" TIMEOUT 180)
    add_test(NAME kb_editor_post_process_headless
        COMMAND $<TARGET_FILE:kb_editor>
            --selftest-scenario "${CMAKE_CURRENT_SOURCE_DIR}/tests/HeadlessPostProcessScenario.json"
            --selftest-root "${CMAKE_BINARY_DIR}/SelfTest"
            --selftest-task "SELFTEST-Post-Process")
    set_tests_properties(kb_editor_post_process_headless PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;runtime;headless;rendering" TIMEOUT 180)
    add_test(NAME kb_editor_material_preview_headless
        COMMAND $<TARGET_FILE:kb_editor>
            --selftest-scenario "${CMAKE_CURRENT_SOURCE_DIR}/tests/HeadlessMaterialPreviewScenario.json"
            --selftest-root "${CMAKE_BINARY_DIR}/SelfTest"
            --selftest-task "SELFTEST-Material-Preview")
    set_tests_properties(kb_editor_material_preview_headless PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;headless;rendering;material" TIMEOUT 300)
    add_test(NAME kb_editor_navigation_headless
        COMMAND $<TARGET_FILE:kb_editor>
            --selftest-scenario "${CMAKE_CURRENT_SOURCE_DIR}/tests/HeadlessNavigationScenario.json"
            --selftest-root "${CMAKE_BINARY_DIR}/SelfTest"
            --selftest-task "SELFTEST-Navigation")
    set_tests_properties(kb_editor_navigation_headless PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;runtime;headless;navigation" TIMEOUT 180)
    # File > New Scene: templates, refused names, startup scene, reopening the last scene, Play without saving.
    add_test(NAME kb_editor_new_scene_headless
        COMMAND $<TARGET_FILE:kb_editor>
            --selftest-scenario "${CMAKE_CURRENT_SOURCE_DIR}/tests/HeadlessNewSceneScenario.json"
            --selftest-root "${CMAKE_BINARY_DIR}/SelfTest"
            --selftest-task "SELFTEST-New-Scene")
    set_tests_properties(kb_editor_new_scene_headless PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;headless;scene" TIMEOUT 180)
    # The dock workspace from the command line: float and close panels, save the arrangement to a file, reset to the default,
    # restore the file, and check at several window sizes that no panel is lost.
    add_test(NAME kb_editor_workspace_headless
        COMMAND $<TARGET_FILE:kb_editor>
            --selftest-scenario "${CMAKE_CURRENT_SOURCE_DIR}/tests/HeadlessWorkspaceScenario.json"
            --selftest-root "${CMAKE_BINARY_DIR}/SelfTest"
            --selftest-task "SELFTEST-Workspace")
    set_tests_properties(kb_editor_workspace_headless PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;headless;docking" TIMEOUT 180)
    # A changed source replaces an imported model in place; an edited material is kept or replaced as told, and the reimport can be undone.
    add_test(NAME kb_editor_asset_reimport_headless
        COMMAND $<TARGET_FILE:kb_editor>
            --selftest-scenario "${CMAKE_CURRENT_SOURCE_DIR}/tests/AssetReimportScenario.json"
            --selftest-root "${CMAKE_BINARY_DIR}/SelfTest"
            --selftest-task "SELFTEST-AssetReimport")
    set_tests_properties(kb_editor_asset_reimport_headless PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;headless;assets" TIMEOUT 180)

    # Every editor language, one after another: each panel's texts fit where they are drawn.
    add_test(NAME kb_editor_languages_headless
        COMMAND $<TARGET_FILE:kb_editor>
            --selftest-scenario "${CMAKE_CURRENT_SOURCE_DIR}/tests/HeadlessLanguagesScenario.json"
            --selftest-root "${CMAKE_BINARY_DIR}/SelfTest"
            --selftest-task "SELFTEST-Languages")
    set_tests_properties(kb_editor_languages_headless PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;headless;localization" TIMEOUT 600)

    add_test(
        NAME kb_editor_headless_automation_scenario
        COMMAND $<TARGET_FILE:kb_editor>
            --selftest-scenario
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/HeadlessAutomationScenario.json"
            --selftest-root
            "${CMAKE_BINARY_DIR}/SelfTest"
            --selftest-task
            "SELFTEST-002-External-Scenario"
    )
    add_test(
        NAME kb_editor_map2d_headless
        COMMAND $<TARGET_FILE:kb_editor>
            --selftest-scenario "${CMAKE_CURRENT_SOURCE_DIR}/tests/Map2DAutomationScenario.json"
            --selftest-root "${CMAKE_BINARY_DIR}/SelfTest"
            --selftest-task "SELFTEST-Map2D")
    set_tests_properties(kb_editor_map2d_headless PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;headless;map2d" TIMEOUT 180)
    add_test(NAME kb_editor_map2d_component
        COMMAND $<TARGET_FILE:kb_editor>
            --selftest-scenario "${CMAKE_CURRENT_SOURCE_DIR}/tests/Map2DComponentScenario.json"
            --selftest-root "${CMAKE_BINARY_DIR}/SelfTest"
            --selftest-task "SELFTEST-Map2D-Component")
    set_tests_properties(kb_editor_map2d_component PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;runtime;headless;map2d" TIMEOUT 180)
    if(Python3_EXECUTABLE AND TARGET kb_game AND TARGET kb_cooker)
            add_test(NAME kb_editor_map2d_visual_package
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/Map2DVisualTests.py"
                    --editor $<TARGET_FILE:kb_editor> --cli $<TARGET_FILE:kb_cli>
                    --cooker $<TARGET_FILE:kb_cooker> --player $<TARGET_FILE:kb_game>
                    --plugin $<TARGET_FILE:kb_physics_jolt_plugin> --plugin $<TARGET_FILE:kb_audio_miniaudio_plugin>
                    --plugin $<TARGET_FILE:kb_basic_lighting_plugin> --plugin $<TARGET_FILE:kb_ai_plugin>
                    --plugin $<TARGET_FILE:kb_21kb_particle_plugin> --plugin $<TARGET_FILE:kb_world2d_plugin>
                    --engine-root "${CMAKE_SOURCE_DIR}" --test-root "${CMAKE_BINARY_DIR}/map2d_visual_tests")
            set_tests_properties(kb_editor_map2d_visual_package PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;headless;map2d;packaging" TIMEOUT 240)
            add_test(NAME kb_editor_camera2d_workflow_package
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/Camera2DWorkflowTests.py"
                    --editor $<TARGET_FILE:kb_editor> --cli $<TARGET_FILE:kb_cli>
                    --cooker $<TARGET_FILE:kb_cooker> --player $<TARGET_FILE:kb_game>
                    --plugin $<TARGET_FILE:kb_physics_jolt_plugin> --plugin $<TARGET_FILE:kb_audio_miniaudio_plugin>
                    --plugin $<TARGET_FILE:kb_basic_lighting_plugin> --plugin $<TARGET_FILE:kb_ai_plugin>
                    --plugin $<TARGET_FILE:kb_21kb_particle_plugin> --plugin $<TARGET_FILE:kb_world2d_plugin>
                    --engine-root "${CMAKE_SOURCE_DIR}" --test-root "${CMAKE_BINARY_DIR}/camera2d_workflow_tests")
            set_tests_properties(kb_editor_camera2d_workflow_package PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;headless;map2d;packaging" TIMEOUT 240)
            add_test(NAME kb_editor_platformer_navigation_package
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/PlatformerNavigationTests.py"
                    --editor $<TARGET_FILE:kb_editor> --cli $<TARGET_FILE:kb_cli>
                    --cooker $<TARGET_FILE:kb_cooker> --player $<TARGET_FILE:kb_game>
                    --plugin $<TARGET_FILE:kb_physics_jolt_plugin> --plugin $<TARGET_FILE:kb_audio_miniaudio_plugin>
                    --plugin $<TARGET_FILE:kb_basic_lighting_plugin> --plugin $<TARGET_FILE:kb_ai_plugin>
                    --plugin $<TARGET_FILE:kb_21kb_particle_plugin> --plugin $<TARGET_FILE:kb_world2d_plugin>
                    --engine-root "${CMAKE_SOURCE_DIR}" --test-root "${CMAKE_BINARY_DIR}/platformer_navigation_tests")
            set_tests_properties(kb_editor_platformer_navigation_package PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;headless;map2d;packaging" TIMEOUT 240)
            add_test(NAME kb_editor_tile_data_package
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/TileDataWorkflowTests.py"
                    --editor $<TARGET_FILE:kb_editor> --cli $<TARGET_FILE:kb_cli>
                    --cooker $<TARGET_FILE:kb_cooker> --player $<TARGET_FILE:kb_game>
                    --plugin $<TARGET_FILE:kb_physics_jolt_plugin> --plugin $<TARGET_FILE:kb_audio_miniaudio_plugin>
                    --plugin $<TARGET_FILE:kb_basic_lighting_plugin> --plugin $<TARGET_FILE:kb_ai_plugin>
                    --plugin $<TARGET_FILE:kb_21kb_particle_plugin> --plugin $<TARGET_FILE:kb_world2d_plugin>
                    --engine-root "${CMAKE_SOURCE_DIR}" --test-root "${CMAKE_BINARY_DIR}/tile_data_tests")
            set_tests_properties(kb_editor_tile_data_package PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;headless;map2d;packaging" TIMEOUT 240)
            add_test(NAME kb_editor_beginner_workflow_package
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/Player2DWorkflowTests.py"
                    --beginner --editor $<TARGET_FILE:kb_editor> --cli $<TARGET_FILE:kb_cli>
                    --cooker $<TARGET_FILE:kb_cooker> --player $<TARGET_FILE:kb_game>
                    --plugin $<TARGET_FILE:kb_physics_jolt_plugin> --plugin $<TARGET_FILE:kb_audio_miniaudio_plugin>
                    --plugin $<TARGET_FILE:kb_basic_lighting_plugin> --plugin $<TARGET_FILE:kb_ai_plugin>
                    --plugin $<TARGET_FILE:kb_21kb_particle_plugin> --plugin $<TARGET_FILE:kb_world2d_plugin>
                    --engine-root "${CMAKE_SOURCE_DIR}" --test-root "${CMAKE_BINARY_DIR}/beginner_workflow_tests")
            set_tests_properties(kb_editor_beginner_workflow_package PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;headless;map2d;packaging" TIMEOUT 240)
            add_test(NAME kb_editor_platformer_surfaces_package
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/PlatformerSurfaceTests.py"
                    --editor $<TARGET_FILE:kb_editor> --cli $<TARGET_FILE:kb_cli>
                    --cooker $<TARGET_FILE:kb_cooker> --player $<TARGET_FILE:kb_game>
                    --plugin $<TARGET_FILE:kb_physics_jolt_plugin> --plugin $<TARGET_FILE:kb_audio_miniaudio_plugin>
                    --plugin $<TARGET_FILE:kb_basic_lighting_plugin> --plugin $<TARGET_FILE:kb_ai_plugin>
                    --plugin $<TARGET_FILE:kb_21kb_particle_plugin> --plugin $<TARGET_FILE:kb_world2d_plugin>
                    --engine-root "${CMAKE_SOURCE_DIR}" --test-root "${CMAKE_BINARY_DIR}/platformer_surface_tests")
            set_tests_properties(kb_editor_platformer_surfaces_package PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;headless;map2d;packaging" TIMEOUT 240)
            add_test(NAME kb_editor_player2d_workflow_package
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/Player2DWorkflowTests.py"
                    --editor $<TARGET_FILE:kb_editor> --cli $<TARGET_FILE:kb_cli>
                    --cooker $<TARGET_FILE:kb_cooker> --player $<TARGET_FILE:kb_game>
                    --plugin $<TARGET_FILE:kb_physics_jolt_plugin> --plugin $<TARGET_FILE:kb_audio_miniaudio_plugin>
                    --plugin $<TARGET_FILE:kb_basic_lighting_plugin> --plugin $<TARGET_FILE:kb_ai_plugin>
                    --plugin $<TARGET_FILE:kb_21kb_particle_plugin> --plugin $<TARGET_FILE:kb_world2d_plugin>
                    --engine-root "${CMAKE_SOURCE_DIR}" --test-root "${CMAKE_BINARY_DIR}/player2d_workflow_tests")
            set_tests_properties(kb_editor_player2d_workflow_package PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;headless;map2d;packaging" TIMEOUT 240)
            add_test(NAME kb_editor_tilephysics_workflow_package
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/Player2DWorkflowTests.py"
                    --tile-collisions --editor $<TARGET_FILE:kb_editor> --cli $<TARGET_FILE:kb_cli>
                    --cooker $<TARGET_FILE:kb_cooker> --player $<TARGET_FILE:kb_game>
                    --plugin $<TARGET_FILE:kb_physics_jolt_plugin> --plugin $<TARGET_FILE:kb_audio_miniaudio_plugin>
                    --plugin $<TARGET_FILE:kb_basic_lighting_plugin> --plugin $<TARGET_FILE:kb_ai_plugin>
                    --plugin $<TARGET_FILE:kb_21kb_particle_plugin> --plugin $<TARGET_FILE:kb_world2d_plugin>
                    --engine-root "${CMAKE_SOURCE_DIR}" --test-root "${CMAKE_BINARY_DIR}/tilephysics_workflow_tests")
            set_tests_properties(kb_editor_tilephysics_workflow_package PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;headless;map2d;packaging" TIMEOUT 240)
        add_test(NAME kb_cli_map2d_authoring
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/sources/tools/tests/Map2DAuthoringTests.py"
                --cli "$<TARGET_FILE:kb_cli>"
                --image "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/map2d/Tiles.png"
                --test-root "${CMAKE_BINARY_DIR}/map2d_authoring_tests")
        set_tests_properties(kb_cli_map2d_authoring PROPERTIES LABELS "cli;map2d;authoring" TIMEOUT 120)
    endif()
    add_test(NAME kb_editor_map2d_lighting
        COMMAND $<TARGET_FILE:kb_editor>
            --selftest-scenario "${CMAKE_CURRENT_SOURCE_DIR}/tests/Map2DLightingScenario.json"
            --selftest-root "${CMAKE_BINARY_DIR}/SelfTest"
            --selftest-task "SELFTEST-Map2D-Lighting")
    set_tests_properties(kb_editor_map2d_lighting PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;headless;map2d;rendering" TIMEOUT 180)
    if(TARGET kb_game AND TARGET kb_cooker AND KB_BUILD_PROVIDER_MODULES_AS_DLL)
        add_test(NAME kb_editor_map2d_packaged
            COMMAND ${CMAKE_COMMAND}
                "-DEDITOR=$<TARGET_FILE:kb_editor>"
                "-DCOOKER=$<TARGET_FILE:kb_cooker>"
                "-DPLAYER=$<TARGET_FILE:kb_game>"
                "-DPROVIDERS=$<TARGET_FILE:kb_physics_jolt_plugin>;$<TARGET_FILE:kb_audio_miniaudio_plugin>;$<TARGET_FILE:kb_basic_lighting_plugin>;$<TARGET_FILE:kb_ai_plugin>;$<TARGET_FILE:kb_21kb_particle_plugin>;$<TARGET_FILE:kb_world2d_plugin>"
                "-DENGINE_ROOT=${CMAKE_SOURCE_DIR}"
                "-DTEST_ROOT=${CMAKE_BINARY_DIR}/SelfTest"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/Map2DPackagedSmoke.cmake")
        set_tests_properties(kb_editor_map2d_packaged PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;runtime;packaging;map2d" TIMEOUT 240)
        add_test(NAME kb_editor_map2d_component_packaged
            COMMAND ${CMAKE_COMMAND}
                "-DEDITOR=$<TARGET_FILE:kb_editor>"
                "-DCOOKER=$<TARGET_FILE:kb_cooker>"
                "-DPLAYER=$<TARGET_FILE:kb_game>"
                "-DPROVIDERS=$<TARGET_FILE:kb_physics_jolt_plugin>;$<TARGET_FILE:kb_audio_miniaudio_plugin>;$<TARGET_FILE:kb_basic_lighting_plugin>;$<TARGET_FILE:kb_ai_plugin>;$<TARGET_FILE:kb_21kb_particle_plugin>;$<TARGET_FILE:kb_world2d_plugin>"
                "-DENGINE_ROOT=${CMAKE_SOURCE_DIR}"
                "-DTEST_ROOT=${CMAKE_BINARY_DIR}/SelfTest"
                "-DSCENARIO=Map2DComponentScenario.json"
                "-DTASK=SELFTEST-Map2D-Component-Package"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/Map2DPackagedSmoke.cmake")
        set_tests_properties(kb_editor_map2d_component_packaged PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;runtime;packaging;map2d" TIMEOUT 240)
        # A Sprite 2D and an Animated Sprite made in the editor, cooked, and drawn by the packaged player.
        add_test(NAME kb_editor_sprite2d_packaged
            COMMAND ${CMAKE_COMMAND}
                "-DEDITOR=$<TARGET_FILE:kb_editor>"
                "-DCOOKER=$<TARGET_FILE:kb_cooker>"
                "-DPLAYER=$<TARGET_FILE:kb_game>"
                "-DPROVIDERS=$<TARGET_FILE:kb_physics_jolt_plugin>;$<TARGET_FILE:kb_audio_miniaudio_plugin>;$<TARGET_FILE:kb_basic_lighting_plugin>;$<TARGET_FILE:kb_ai_plugin>;$<TARGET_FILE:kb_21kb_particle_plugin>;$<TARGET_FILE:kb_world2d_plugin>"
                "-DENGINE_ROOT=${CMAKE_SOURCE_DIR}"
                "-DTEST_ROOT=${CMAKE_BINARY_DIR}/SelfTest"
                "-DSCENARIO=Sprite2DScenario.json"
                "-DTASK=SELFTEST-Sprite2D-Package"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/Map2DPackagedSmoke.cmake")
        set_tests_properties(kb_editor_sprite2d_packaged PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;runtime;packaging;map2d" TIMEOUT 240)
        # One representative 3D scene (lights and shadows, materials, sky, fog, ambient occlusion, vignette, auto exposure, a particle effect
        # and a terrain) drawn by the editor's Play view and by the packaged player at the same size, and compared pixel by pixel.
        find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
        add_test(NAME kb_editor_scene3d_parity_packaged
            COMMAND ${CMAKE_COMMAND}
                "-DEDITOR=$<TARGET_FILE:kb_editor>"
                "-DCOOKER=$<TARGET_FILE:kb_cooker>"
                "-DPLAYER=$<TARGET_FILE:kb_game>"
                "-DPROVIDERS=$<TARGET_FILE:kb_physics_jolt_plugin>;$<TARGET_FILE:kb_audio_miniaudio_plugin>;$<TARGET_FILE:kb_basic_lighting_plugin>;$<TARGET_FILE:kb_ai_plugin>;$<TARGET_FILE:kb_21kb_particle_plugin>;$<TARGET_FILE:kb_world2d_plugin>"
                "-DENGINE_ROOT=${CMAKE_SOURCE_DIR}"
                "-DTEST_ROOT=${CMAKE_BINARY_DIR}/SelfTest"
                "-DSCENARIO=Scene3DParityScenario.json"
                "-DTASK=SELFTEST-Scene3D-Package"
                "-DWIDTH=960" "-DHEIGHT=540" "-DSCREENSHOT_FRAME=240"
                "-DPYTHON=${Python3_EXECUTABLE}"
                "-DPARITY_CHECKPOINT=scene3d-editor"
                "-DPARITY_PIXEL_TOLERANCE=12" "-DPARITY_MAX_MEAN=2.0" "-DPARITY_MAX_DIFFERING=0.002"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/Map2DPackagedSmoke.cmake")
        set_tests_properties(kb_editor_scene3d_parity_packaged PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS "editor;runtime;packaging;rendering" TIMEOUT 600)
    endif()
    set_tests_properties(
        kb_editor_headless_automation_scenario
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            LABELS "editor;runtime;headless"
    )

    add_test(
        NAME kb_editor_particle_authoring_headless
        COMMAND $<TARGET_FILE:kb_editor>
            --selftest-scenario
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/ParticleEditorAuthoringScenario.json"
            --selftest-root
            "${CMAKE_BINARY_DIR}/ParticleAuthoringSelfTest"
            --selftest-task
            "PARTICLE-EDITOR-7A"
    )
    set_tests_properties(
        kb_editor_particle_authoring_headless
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            LABELS "editor;particle;headless"
            TIMEOUT 180
            # The scenario asserts save and play-transition time budgets, so it runs alone like the other performance gates.
            RUN_SERIAL TRUE
    )

    kb_apply_quality_gates(kb_editor_tests)

    add_executable(kb_editor_material_graph_canvas_tests
        tests/EditorMaterialGraphCanvasRuntimeTests.cpp
        tests/EditorMaterialGraphCanvasTests.cpp
        tests/EditorTestSuites.hpp
        tests/EditorTestSupport.hpp
        src/rendering/material_graph/EditorMaterialGraphPinPresentation.cpp
        src/rendering/material_graph/MaterialGraphCanvasDocumentAdapter.cpp
        src/rendering/material_graph/MaterialGraphCanvas.cpp
        # The canvas tests check node rects against the panel layout, which owns ResolveLayout and
        # measures its labels with the editor's fonts.
        src/rendering/MaterialEditorPanelLayout.cpp
        src/rendering/gdi/EditorFontCache.cpp
        # The panel's labels are in the editor language.
        src/localization/EditorLocalization.cpp
    )

    target_include_directories(kb_editor_material_graph_canvas_tests
        PRIVATE
            include
            src/private
            tests
    )

    target_link_libraries(kb_editor_material_graph_canvas_tests
        PRIVATE
            kb_renderer
            kb_21kb_particle_editor_core
    )

    add_test(NAME kb_editor_material_graph_canvas_tests COMMAND $<TARGET_FILE:kb_editor_material_graph_canvas_tests>)
    set_tests_properties(kb_editor_material_graph_canvas_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

    kb_apply_quality_gates(kb_editor_material_graph_canvas_tests)
endif()
