# Copyright (c) 2026 groatse. Licensed under the MIT License.
"""
Creates the DistanceLODder functional test content in the plugin's Content/Tests folder:

- SM_DistanceLODderTest: the engine sphere with 4 LODs switching at 1000 / 2000 / 4000 cm
  (with a 90 degree reference FOV).
- FTEST_DistanceLODder: the test map, with ADistanceLODderFunctionalTest and tagged meshes.
- L_DistanceLODder_Streamed: a streaming sublevel (Blueprint streaming, not initially loaded) with one mesh.
- FTEST_DistanceLODderPerf: the perf map, with ADistanceLODderPerfTest (it spawns its mesh grid at runtime).

Needs the PythonScriptPlugin and EditorScriptingUtilities plugins enabled in the host project. Run headless with:

    UnrealEditor-Cmd.exe <Project>.uproject -ExecutePythonScript="<path>/CreateFunctionalTestMap.py" -unattended -RenderOffscreen -nosplash

Not -nullrhi: placing actors queries the level viewport, which crashes (divide by zero) without a renderer.

Only missing assets are created, so existing (committed) assets stay unchanged. Delete an asset to rebuild it;
the main map and its sublevel are rebuilt together.
"""

import unreal

ROOT = "/DistanceLODder/Tests"
MESH_PATH = ROOT + "/SM_DistanceLODderTest"
MAP_PATH = ROOT + "/Maps/FTEST_DistanceLODder"
PERF_MAP_PATH = ROOT + "/Maps/FTEST_DistanceLODderPerf"
SUBLEVEL_PATH = ROOT + "/Maps/L_DistanceLODder_Streamed"  # No FTEST_ prefix, or it gets listed as a test map
SWITCH_DISTANCES = [1000.0, 2000.0, 4000.0]

assets = unreal.EditorAssetLibrary
mesh_editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def log(message):
    unreal.log("CreateFunctionalTestMap: " + message)


def create_mesh():
    mesh = assets.duplicate_asset("/Engine/BasicShapes/Sphere", MESH_PATH)

    options = unreal.StaticMeshReductionOptions()
    options.set_editor_property("auto_compute_lod_screen_size", False)
    reductions = []
    for percent in [1.0, 0.5, 0.25, 0.125]:
        settings = unreal.StaticMeshReductionSettings()
        settings.set_editor_property("percent_triangles", percent)
        settings.set_editor_property("screen_size", 1.0)
        reductions.append(settings)
    options.set_editor_property("reduction_settings", reductions)
    mesh_editor.set_lods(mesh, options)

    # Screen size R / D makes LOD i start at distance D with a magnification of 1 (90 degrees).
    radius = mesh.get_bounds().sphere_radius
    mesh_editor.set_lod_screen_sizes(mesh, [1.0] + [radius / d for d in SWITCH_DISTANCES])
    assets.save_asset(MESH_PATH, only_if_is_dirty=False)

    log("mesh radius %.2f, LODs %d, screen sizes %s" % (radius, mesh_editor.get_lod_count(mesh), mesh_editor.get_lod_screen_sizes(mesh)))
    return mesh


def spawn_mesh(mesh, location, tags, movable=False):
    actor = actors.spawn_actor_from_object(mesh, location)
    actor.set_actor_label(tags[0])
    actor.set_editor_property("tags", [unreal.Name(t) for t in tags])
    if movable:
        actor.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    return actor


def create_main_map(mesh):
    for path in [MAP_PATH, SUBLEVEL_PATH]:
        if assets.does_asset_exist(path):
            assets.delete_asset(path)

    # Sublevel first, so it can be added to the main map.
    level_editor.new_level(SUBLEVEL_PATH)
    spawn_mesh(mesh, unreal.Vector(0, 3000, 0), ["DLTest_Streamed"])
    level_editor.save_current_level()

    level_editor.new_level(MAP_PATH)
    spawn_mesh(mesh, unreal.Vector(0, 0, 0), ["DLTest_Main"])
    spawn_mesh(mesh, unreal.Vector(0, -800, 0), ["DLTest_OptOut", "NoDistanceLOD"])
    spawn_mesh(mesh, unreal.Vector(0, 800, 0), ["DLTest_Movable"], movable=True)

    test = actors.spawn_actor_from_class(unreal.DistanceLODderFunctionalTest, unreal.Vector(0, 0, 200))
    test.set_actor_label("DistanceLODderFunctionalTest")

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.EditorLevelUtils.add_level_to_world(world, SUBLEVEL_PATH, unreal.LevelStreamingDynamic)
    level_editor.save_all_dirty_levels()
    log("created " + MAP_PATH + " and " + SUBLEVEL_PATH)


def create_perf_map(mesh):
    level_editor.new_level(PERF_MAP_PATH)
    test = actors.spawn_actor_from_class(unreal.DistanceLODderPerfTest, unreal.Vector(0, 0, 200))
    test.set_actor_label("DistanceLODderPerfTest")
    test.set_editor_property("mesh", mesh)
    level_editor.save_current_level()
    log("created " + PERF_MAP_PATH)


def main():
    if assets.does_asset_exist(MESH_PATH):
        mesh = assets.load_asset(MESH_PATH)
    else:
        mesh = create_mesh()

    if not assets.does_asset_exist(MAP_PATH) or not assets.does_asset_exist(SUBLEVEL_PATH):
        create_main_map(mesh)

    if not assets.does_asset_exist(PERF_MAP_PATH):
        create_perf_map(mesh)

    log("done")


try:
    main()
finally:
    unreal.SystemLibrary.quit_editor()
