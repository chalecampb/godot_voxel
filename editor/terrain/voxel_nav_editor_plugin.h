#ifndef VOXEL_NAV_EDITOR_PLUGIN_H
#define VOXEL_NAV_EDITOR_PLUGIN_H

#include "../../util/godot/classes/editor_plugin.h"
#include "../../util/godot/macros.h"

ZN_GODOT_FORWARD_DECLARE(class MenuButton)

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
	void _on_menu_item_selected(int id);

	Object *get_selected_object() const;
	void update_menu_items();
	void update_menu_label();

	static void _bind_methods();

	MenuButton *_menu_button = nullptr;
	ObjectID _selected_object_id = ObjectID();
};

} // namespace zylann::voxel

#endif // VOXEL_NAV_EDITOR_PLUGIN_H
