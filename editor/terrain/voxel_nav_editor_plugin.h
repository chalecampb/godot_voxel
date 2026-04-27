#ifndef VOXEL_NAV_EDITOR_PLUGIN_H
#define VOXEL_NAV_EDITOR_PLUGIN_H

#include "../../util/godot/classes/editor_plugin.h"
#include "../../util/godot/macros.h"

ZN_GODOT_FORWARD_DECLARE(class Button)
ZN_GODOT_FORWARD_DECLARE(class HBoxContainer)

namespace zylann::voxel {

class VoxelNavEditorPlugin : public zylann::godot::ZN_EditorPlugin {
	GDCLASS(VoxelNavEditorPlugin, zylann::godot::ZN_EditorPlugin)
public:
	VoxelNavEditorPlugin();

protected:
	bool _zn_handles(const Object *p_object) const override;
	void _zn_edit(Object *p_object) override;
	void _zn_make_visible(bool visible) override;

private:
	void init();
	void _notification(int what);
	void _on_bake_button_pressed();
	void _on_clear_button_pressed();

	Object *get_selected_object() const;
	void update_button_icons();

	static void _bind_methods();

	HBoxContainer *_button_bar = nullptr;
	Button *_bake_button = nullptr;
	Button *_clear_button = nullptr;
	ObjectID _selected_object_id = ObjectID();
};

} // namespace zylann::voxel

#endif // VOXEL_NAV_EDITOR_PLUGIN_H
