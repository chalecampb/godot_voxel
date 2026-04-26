#include "voxel_nav_editor_plugin.h"

#include "../../terrain/voxel_nav_manager_3d.h"
#include "../../util/godot/classes/menu_button.h"
#include "../../util/godot/classes/object.h"
#include "../../util/godot/classes/popup_menu.h"
#include "../../util/godot/core/string.h"

#ifdef ZN_GODOT
#include "../../util/godot/core/callable_mp.h"
#endif

namespace zylann::voxel {

namespace {
enum MenuItemID { //
	MENU_REBUILD_REGIONS,
	MENU_BAKE_NAVIGATION_MESH,
	MENU_CLEAR_NAVIGATION_MESH,
	MENU_CLEAR_REGIONS
};
} // namespace

VoxelNavEditorPlugin::VoxelNavEditorPlugin() {}

void VoxelNavEditorPlugin::init() {
	MenuButton *menu_button = memnew(MenuButton);
	menu_button->set_text(ZN_TTR("Voxel Nav"));
	menu_button->get_popup()->connect("id_pressed", callable_mp(this, &VoxelNavEditorPlugin::_on_menu_item_selected));
	menu_button->hide();
	add_control_to_container(CONTAINER_SPATIAL_EDITOR_MENU, menu_button);
	_menu_button = menu_button;
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
	update_menu_label();
	update_menu_items();
}

void VoxelNavEditorPlugin::_zn_make_visible(bool visible) {
	_menu_button->set_visible(visible);
	if (visible) {
		update_menu_label();
		update_menu_items();
	}
}

Object *VoxelNavEditorPlugin::get_selected_object() const {
	if (!_selected_object_id.is_valid()) {
		return nullptr;
	}
	return ObjectDB::get_instance(_selected_object_id);
}

void VoxelNavEditorPlugin::update_menu_label() {
	if (_menu_button == nullptr) {
		return;
	}

	Object *selected_object = get_selected_object();
	if (Object::cast_to<VoxelNavManager3D>(selected_object) != nullptr) {
		_menu_button->set_text(VoxelNavManager3D::get_class_static());
	} else {
		_menu_button->set_text(ZN_TTR("Voxel Nav"));
	}
}

void VoxelNavEditorPlugin::update_menu_items() {
	if (_menu_button == nullptr) {
		return;
	}

	PopupMenu *popup = _menu_button->get_popup();
	popup->clear();

	Object *selected_object = get_selected_object();
	if (Object::cast_to<VoxelNavManager3D>(selected_object) != nullptr) {
		popup->add_item(ZN_TTR("Rebuild Regions (No Bake)"), MENU_REBUILD_REGIONS);
		popup->add_item(ZN_TTR("Rebuild + Bake NavigationMesh"), MENU_BAKE_NAVIGATION_MESH);
		popup->add_item(ZN_TTR("Clear NavigationMesh"), MENU_CLEAR_NAVIGATION_MESH);
		popup->add_separator();
		popup->add_item(ZN_TTR("Clear Regions"), MENU_CLEAR_REGIONS);
	}
}

void VoxelNavEditorPlugin::_on_menu_item_selected(int id) {
	Object *selected_object = get_selected_object();

	if (VoxelNavManager3D *manager = Object::cast_to<VoxelNavManager3D>(selected_object)) {
		switch (id) {
			case MENU_REBUILD_REGIONS:
				manager->rebuild_regions();
				break;
			case MENU_BAKE_NAVIGATION_MESH:
				manager->rebuild_regions();
				manager->bake_navigation_meshes();
				break;
			case MENU_CLEAR_NAVIGATION_MESH:
				manager->clear_navigation_meshes();
				break;
			case MENU_CLEAR_REGIONS:
				manager->clear_regions();
				break;
		}
		return;
	}
}

void VoxelNavEditorPlugin::_bind_methods() {}

} // namespace zylann::voxel
