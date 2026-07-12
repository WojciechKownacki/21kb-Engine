fn main() {
    tauri::Builder::default()
        .manage(kb_editor_host::ViewportHost::new())
        .invoke_handler(tauri::generate_handler![
            kb_editor_host::viewport_open_screen,
            kb_editor_host::editor_open_panel_window,
        ])
        .run(tauri::generate_context!())
        .expect("the 21kb Editor host failed to run");
}
