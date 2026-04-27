#include "voxel_nav_editor_plugin.h"

#include "../../terrain/voxel_nav_manager_3d.h"
#include "../../util/godot/classes/button.h"
#include "../../util/godot/classes/h_box_container.h"
#include "../../util/godot/classes/object.h"
#include "../../util/godot/core/string_name.h"
#include "../../util/godot/core/string.h"

#ifdef ZN_GODOT
#include "../../util/godot/core/callable_mp.h"
#endif

namespace zylann::voxel {

VoxelNavEditorPlugin::VoxelNavEditorPlugin() {}

void VoxelNavEditorPlugin::init() {
	HBoxContainer *button_bar = memnew(HBoxContainer);

	Button *bake_button = memnew(Button);
	bake_button->set_theme_type_variation(StringName("FlatButton"));
	bake_button->set_toggle_mode(true);
	bake_button->set_text(ZN_TTR("Bake Navigation Regions"));
	bake_button->set_tooltip_text(ZN_TTR("Bake Navigation Regions"));
	bake_button->connect("pressed", callable_mp(this, &VoxelNavEditorPlugin::_on_bake_button_pressed));
	button_bar->add_child(bake_button);
	_bake_button = bake_button;

	Button *clear_button = memnew(Button);
	clear_button->set_theme_type_variation(StringName("FlatButton"));
	clear_button->set_text(ZN_TTR("Clear Navigation Regions"));
	clear_button->set_tooltip_text(ZN_TTR("Clear Navigation Regions"));
	clear_button->connect("pressed", callable_mp(this, &VoxelNavEditorPlugin::_on_clear_button_pressed));
	button_bar->add_child(clear_button);
	_clear_button = clear_button;

	button_bar->hide();
	add_control_to_container(CONTAINER_SPATIAL_EDITOR_MENU, button_bar);
	_button_bar = button_bar;
	update_button_icons();
}

void VoxelNavEditorPlugin::_notification(int what) {
	if (what == NOTIFICATION_ENTER_TREE) {
		init();
	}
}

bool VoxelNavEditorPlugin::_zn_handles(const Object *p_object) const {
	return Object::cast_to<VoxelNavManager3D>(p_object) != nullptr;
}

void VoxelNavEditorPlugin::_zn_edit(Object *p_object) {
	_selected_object_id = p_object != nullptr ? p_object->get_instance_id() : ObjectID();
}

void VoxelNavEditorPlugin::_zn_make_visible(bool visible) {
	if (_button_bar != nullptr) {
		_button_bar->set_visible(visible);
	}
}

Object *VoxelNavEditorPlugin::get_selected_object() const {
	if (!_selected_object_id.is_valid()) {
		return nullptr;
	}
	return ObjectDB::get_instance(_selected_object_id);
}

void VoxelNavEditorPlugin::update_button_icons() {
	if (_bake_button == nullptr || _clear_button == nullptr) {
		return;
	}

	zylann::godot::set_button_icon(*_bake_button, _bake_button->get_theme_icon("Bake", "EditorIcons"));
	zylann::godot::set_button_icon(*_clear_button, _clear_button->get_theme_icon("Reload", "EditorIcons"));
}

void VoxelNavEditorPlugin::_on_bake_button_pressed() {
	_bake_button->set_pressed(false);

	VoxelNavManager3D *manager = Object::cast_to<VoxelNavManager3D>(get_selected_object());
	if (manager == nullptr) {
		return;
	}

	manager->rebuild_regions();
	manager->bake_navigation_meshes();
}

void VoxelNavEditorPlugin::_on_clear_button_pressed() {
	VoxelNavManager3D *manager = Object::cast_to<VoxelNavManager3D>(get_selected_object());
	if (manager == nullptr) {
		return;
	}

	manager->clear_regions();
}

void VoxelNavEditorPlugin::_bind_methods() {}

} // namespace zylann::voxel
