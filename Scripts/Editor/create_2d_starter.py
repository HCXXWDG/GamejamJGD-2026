"""Create the starter in a fresh namespace using Epic's editor tool APIs.

Run in UE 5.8.3: Tools > Execute Python Script. Existing starter assets are
validated without regenerating the map or overwriting later team edits.
"""
import json
from pathlib import Path
import re
import unreal
from editor_toolset.toolsets.actor import ActorTools
from editor_toolset.toolsets.asset import AssetTools
from editor_toolset.toolsets.blueprint import BlueprintTools
from editor_toolset.toolsets.material import MaterialTools
from editor_toolset.toolsets.object import ObjectTools
from editor_toolset.toolsets.scene import SceneTools

ROOT = "/Game/JGD2026"
OWNER = "JGD2026.2DStarter.v1"
PATHS = {
    "pawn": ROOT + "/Core/View/BP_2DViewPawn",
    "controller": ROOT + "/Core/BP_2DPlayerController",
    "mode": ROOT + "/Core/BP_2DGameMode",
    "material": ROOT + "/Materials/M_2DPrototypeGround",
    "texture": ROOT + "/Sprites/T_Placeholder",
    "sprite": ROOT + "/Sprites/SP_Placeholder",
    "map": ROOT + "/Maps/L_2D_Sandbox",
}


def ref(obj):
    return {"refPath": obj.get_path_name()}


def set_properties(obj, **values):
    # Use the same reflection API exposed through Epic's ObjectTools MCP tools.
    schema = json.loads(ObjectTools.list_properties(obj))
    unknown = set(values) - set(schema)
    if unknown:
        raise RuntimeError(f"Unknown UE properties: {sorted(unknown)}")
    if not ObjectTools.set_properties(obj, json.dumps(values)):
        raise RuntimeError(f"Could not set properties on {obj.get_path_name()}")


def validate():
    assets = {key: AssetTools.load_asset(path) for key, path in PATHS.items()}
    for key in ("pawn", "controller", "mode"):
        BlueprintTools.compile_blueprint(assets[key], warnings_as_errors=True)
    cameras = ActorTools.get_components(
        BlueprintTools.get_default_object(assets["pawn"]), unreal.CameraComponent.static_class())
    if len(cameras) != 1:
        raise RuntimeError("The view Pawn must have exactly one CameraComponent")
    camera = json.loads(ObjectTools.get_properties(cameras[0], ["projectionMode", "orthoWidth"]))
    if camera["projectionMode"] != "Orthographic" or camera["orthoWidth"] <= 0:
        raise RuntimeError("The view Pawn needs a valid orthographic camera")
    unreal.log("2D starter validated. Existing assets and map were preserved.")


def spawn(actor_type, label, location, scale=(1, 1, 1), yaw=0):
    actor = SceneTools.add_to_scene_from_class(
        actor_type.static_class(), label,
        unreal.Transform(
            location=unreal.Vector(*location),
            rotation=unreal.Rotator(0, yaw, 0),
            scale=unreal.Vector(*scale)))
    SceneTools.set_actor_folder(actor, "JGD2026/Starter")
    return actor


def configure_maps(map_path, mode_path):
    values = {"editorStartupMap": {"refPath": map_path},
              "gameDefaultMap": {"refPath": map_path},
              "globalDefaultGameMode": {"refPath": mode_path}}
    maps_class = unreal.load_class(None, "/Script/EngineSettings.GameMapsSettings")
    set_properties(unreal.get_default_object(maps_class), **values)
    # Native ConfigSettingsToolset methods are MCP-only in this build. Persist
    # the same three settings while keeping every other config value intact.
    path = Path(unreal.Paths.project_config_dir()) / "DefaultEngine.ini"
    text = path.read_text(encoding="utf-8-sig")
    section = "[/Script/EngineSettings.GameMapsSettings]"
    match = re.search(r"(?m)^\[/Script/EngineSettings.GameMapsSettings\]\s*$", text)
    if match:
        start = match.end()
        following = re.search(r"(?m)^\[", text[start:])
        end = start + following.start() if following else len(text)
    else:
        text += "\n" + section + "\n"
        start = end = len(text)
    body = text[start:end]
    for key, value in values.items():
        name = key[0].upper() + key[1:]
        line = name + "=" + value["refPath"]
        pattern = rf"(?mi)^{name}=.*$"
        body = re.sub(pattern, line, body) if re.search(pattern, body) else body.rstrip() + "\n" + line + "\n"
    path.write_text(text[:start] + body + text[end:], encoding="utf-8")


def main():
    existing = [path for path in PATHS.values() if AssetTools.exists(path)]
    if existing:
        for path in existing:
            if AssetTools.get_metadata_tags(path).get("JGDStarterOwner") != OWNER:
                raise RuntimeError(f"Refusing to overwrite an existing asset: {path}")
        if len(existing) != len(PATHS):
            raise RuntimeError("Partial starter found; inspect it before rerunning creation")
        validate()
        return
    dirty = (list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
             + list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()))
    if dirty:
        raise RuntimeError("Save current editor work before creating the starter")

    assets = {}
    for key, parent in (("pawn", unreal.Pawn), ("controller", unreal.PlayerController),
                        ("mode", unreal.GameModeBase)):
        folder, name = PATHS[key].rsplit("/", 1)
        assets[key] = BlueprintTools.create(folder, name, parent.static_class())
    camera = ActorTools.add_component(assets["pawn"], unreal.CameraComponent.static_class(), "ViewCamera")
    set_properties(camera, projectionMode="Orthographic", orthoWidth=2200,
                   relativeLocation={"x": 1200, "y": 1200, "z": 1200},
                   relativeRotation={"pitch": -35.2644, "yaw": -135, "roll": 0},
                   bUsePawnControlRotation=False, bAutoActivate=True,
                   bConstrainAspectRatio=False)
    for key in ("pawn", "controller"):
        BlueprintTools.compile_blueprint(assets[key], warnings_as_errors=True)
    set_properties(assets["mode"], defaultPawnClass=ref(assets["pawn"].generated_class()),
                   playerControllerClass=ref(assets["controller"].generated_class()))
    BlueprintTools.compile_blueprint(assets["mode"], warnings_as_errors=True)

    assets["material"] = MaterialTools.create_material(ROOT + "/Materials", "M_2DPrototypeGround")
    set_properties(assets["material"], shadingModel="MSM_Unlit")
    color = MaterialTools.add_expression(assets["material"], unreal.MaterialExpressionConstant3Vector.static_class(), -300, 0)
    set_properties(color, constant={"r": 0.12, "g": 0.16, "b": 0.20, "a": 1})
    MaterialTools.connect_to_output(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MaterialTools.recompile(assets["material"])
    for key, source in (("texture", "/Paper2D/PlaceholderTextures/DummySpriteTexture"),
                        ("sprite", "/Paper2D/DummySprite")):
        if not AssetTools.duplicate(source, PATHS[key]):
            raise RuntimeError(f"Could not duplicate {source}")
        assets[key] = AssetTools.load_asset(PATHS[key])
    set_properties(assets["sprite"], sourceTexture=ref(assets["texture"]),
                   defaultMaterial={"refPath": "/Paper2D/MaskedUnlitSpriteMaterial.MaskedUnlitSpriteMaterial"},
                   pixelsPerUnrealUnit=0.5, spriteCollisionDomain="None", pivotMode="Bottom_Center")
    if not AssetTools.save_assets([PATHS[key] for key in assets]):
        raise RuntimeError("Could not save starter assets")

    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_editor.new_level(PATHS["map"]):
        raise RuntimeError("Could not create the sandbox map")
    spawn(unreal.PlayerStart, "PlayerStart_ViewOrigin", (0, 0, 100))
    ground = spawn(unreal.StaticMeshActor, "Prototype_Ground", (0, 0, -25), (16, 12, 0.5))
    set_properties(ground.static_mesh_component,
                   staticMesh={"refPath": "/Engine/BasicShapes/Cube.Cube"},
                   overrideMaterials=[ref(assets["material"])], castShadow=False)
    for label, location, tint in (
            ("Sprite_Left_Orange", (-350, -200, 0), (1, 0.30, 0.06)),
            ("Sprite_Front_Green", (280, -260, 0), (0.16, 0.85, 0.35)),
            ("Sprite_Back_Blue", (50, 360, 0), (0.18, 0.48, 1))):
        actor = spawn(unreal.PaperSpriteActor, label, location, yaw=-45)
        component = ActorTools.get_components(actor, unreal.PaperSpriteComponent.static_class())[0]
        set_properties(component, sourceSprite=ref(assets["sprite"]),
                       spriteColor=dict(zip("rgba", (*tint, 1))), castShadow=False)
    assets["map"] = AssetTools.load_asset(PATHS["map"])
    for key, asset in assets.items():
        AssetTools.update_metadata_tags(PATHS[key], {"JGDStarterOwner": OWNER})
    if not level_editor.save_current_level() or not AssetTools.save_assets(list(PATHS.values())):
        raise RuntimeError("Could not save the sandbox")

    configure_maps(assets["map"].get_path_name(), assets["mode"].generated_class().get_path_name())
    validate()


if __name__ == "__main__":
    main()
