#!/usr/bin/env python3
"""Generates the Triangle command menu's default list.

Writes ps5_commands.txt (KeyValues, for reference) and zmr_commands.c (the
same text as a C string the launcher writes to cfg/ps5_commands.txt). Bump
VERSION whenever the list changes: the launcher replaces an older file.
"""
from pathlib import Path

VERSION = 21
CHEATS = "sv_cheats 1; "

L = []  # (category, label, command, description)

def add(cat, label, cmd, desc):
    L.append((cat, label, cmd, desc))

# ---- Gameplay --------------------------------------------------------------
G = "Gameplay"
add(G, "Quick save", "save quick", "Save to the quicksave slot (same as the touchpad).")
add(G, "Quick load", "load quick", "Load the last quicksave.")
add(G, "Load autosave", "load autosave", "Load the most recent autosave (made at level changes).")
add(G, "Restart level", "restart", "Restart the current map from its beginning.")
add(G, "Kill yourself", "kill", "Die; any button then reloads the last save.")
add(G, "Difficulty: Easy", "skill 1", "Easy difficulty. Takes effect right away.")
add(G, "Difficulty: Normal", "skill 2", "Normal difficulty.")
add(G, "Difficulty: Hard", "skill 3", "Hard difficulty.")
add(G, "Main menu", "disconnect", "Leave the game and go back to the main menu (unsaved progress is lost).")
add(G, "Quit to home screen", "quit", "Close the game.")

# ---- Cheats ----------------------------------------------------------------
C = "Cheats"
add(C, "Cheats on", "sv_cheats 1", "Allow cheat commands. The entries below turn this on for you.")
add(C, "Cheats off", "sv_cheats 0", "Turn cheats off again (cheats already active, like god mode, stay on until toggled).")
add(C, "God mode", CHEATS + "god", "You can't be hurt. Run again to turn off.")
add(C, "Buddha mode", CHEATS + "buddha", "You take damage but can't die. Run again to turn off.")
add(C, "Noclip", CHEATS + "noclip", "Fly through walls. Run again to turn off.")
add(C, "Invisible to enemies", CHEATS + "notarget", "Enemies ignore you. Run again to turn off.")
add(C, "All weapons and ammo", CHEATS + "impulse 101", "Every weapon, full ammo and the HEV suit.")
add(C, "Infinite ammo", CHEATS + "toggle sv_infinite_ammo", "Weapons never run out or need reloading (on/off).")
add(C, "Health kit", CHEATS + "give item_healthkit", "Restores 25 health.")
add(C, "Suit battery", CHEATS + "give item_battery", "Adds 15 suit power.")
add(C, "HEV suit", CHEATS + "give item_suit", "Gives the HEV suit (sprint, flashlight, HUD).")
add(C, "Super gravity gun", CHEATS + "toggle physcannon_mega_enabled", "The blue, supercharged gravity gun from the Citadel (on/off).")
add(C, "Low gravity", CHEATS + "sv_gravity 200", "Moon gravity.")
add(C, "Normal gravity", "sv_gravity 600", "Back to normal gravity.")
add(C, "Slow motion", CHEATS + "host_timescale 0.5", "Half-speed game.")
add(C, "Fast motion", CHEATS + "host_timescale 2", "Double-speed game.")
add(C, "Normal speed", "host_timescale 1", "Back to normal game speed.")
add(C, "Freeze enemies (AI off)", CHEATS + "ai_disable", "Stops all NPC thinking. Run again to resume.")
add(C, "Kill what you aim at", CHEATS + "npc_kill", "Kills the creature under your crosshair.")
add(C, "Remove what you aim at", CHEATS + "ent_remove", "Deletes the object or creature under your crosshair.")
add(C, "Fullbright", CHEATS + "toggle mat_fullbright", "Light everything evenly (on/off).")
add(C, "Wireframe", CHEATS + "toggle mat_wireframe", "Draw the world as wireframe (on/off).")
add(C, "Third person", CHEATS + "thirdperson", "Camera behind the player.")
add(C, "First person", "firstperson", "Back to the normal camera.")

# ---- Weapons ---------------------------------------------------------------
W = "Weapons"
for name, ent, desc in [
    ("Crowbar", "weapon_crowbar", "Melee."),
    ("Gravity gun", "weapon_physcannon", "Pick up and throw objects."),
    ("Stunstick", "weapon_stunstick", "Combine melee baton."),
    ("Pistol", "weapon_pistol", "9 mm pistol."),
    (".357 Magnum", "weapon_357", "Revolver."),
    ("SMG", "weapon_smg1", "Submachine gun with grenade launcher."),
    ("Pulse rifle", "weapon_ar2", "Combine AR2 with energy balls."),
    ("Shotgun", "weapon_shotgun", "Pump shotgun."),
    ("Crossbow", "weapon_crossbow", "Scoped crossbow."),
    ("Grenade", "weapon_frag", "Frag grenades."),
    ("RPG", "weapon_rpg", "Laser-guided rocket launcher."),
    ("Bugbait", "weapon_bugbait", "Commands antlions."),
]:
    add(W, name, CHEATS + "give " + ent, desc + " (cheat)")
for name, ent in [
    ("Pistol ammo", "item_ammo_pistol"), (".357 ammo", "item_ammo_357"),
    ("SMG ammo", "item_ammo_smg1"), ("SMG grenade", "item_ammo_smg1_grenade"),
    ("Pulse rifle ammo", "item_ammo_ar2"), ("Pulse rifle energy ball", "item_ammo_ar2_altfire"),
    ("Shotgun shells", "item_box_buckshot"), ("Crossbow bolts", "item_ammo_crossbow"),
    ("RPG rocket", "item_rpg_round"),
]:
    add(W, name, CHEATS + "give " + ent, "Ammo pickup. (cheat)")

# ---- Spawn -----------------------------------------------------------------
S = "Spawn"
for name, ent, desc in [
    ("Zombie", "npc_zombie", ""), ("Fast zombie", "npc_fastzombie", ""),
    ("Poison zombie", "npc_poisonzombie", ""), ("Headcrab", "npc_headcrab", ""),
    ("Fast headcrab", "npc_headcrab_fast", ""), ("Poison headcrab", "npc_headcrab_black", ""),
    ("Antlion", "npc_antlion", ""), ("Antlion guard", "npc_antlionguard", "Big one."),
    ("Combine soldier", "npc_combine_s", "Unarmed unless the map gives it a weapon."),
    ("Metrocop", "npc_metropolice", ""), ("Manhack", "npc_manhack", ""),
    ("Rollermine", "npc_rollermine", ""), ("City scanner", "npc_cscanner", ""),
    ("Citizen", "npc_citizen", ""), ("Vortigaunt", "npc_vortigaunt", ""),
    ("Crow", "npc_crow", ""), ("Pigeon", "npc_pigeon", ""), ("Seagull", "npc_seagull", ""),
]:
    add(S, name, CHEATS + "npc_create " + ent, ("Spawns where you aim. " + desc).strip() + " (cheat)")
add(S, "Jeep (buggy)", CHEATS + "ch_createjeep", "Spawns the scout car in front of you. (cheat)")
add(S, "Airboat", CHEATS + "ch_createairboat", "Spawns the airboat in front of you. (cheat)")
add(S, "Explosive barrel", CHEATS + "prop_physics_create props_c17/oildrum001_explosive", "Spawns a red barrel where you aim. (cheat)")
add(S, "Wooden crate", CHEATS + "prop_physics_create props_junk/wood_crate001a", "Spawns a crate where you aim. (cheat)")

# ---- Level select ----------------------------------------------------------
LV = "Level select"
maps = [
    ("Point Insertion", ["d1_trainstation_01", "d1_trainstation_02", "d1_trainstation_03", "d1_trainstation_04"]),
    ("A Red Letter Day", ["d1_trainstation_05", "d1_trainstation_06"]),
    ("Route Kanal", ["d1_canals_01", "d1_canals_01a", "d1_canals_02", "d1_canals_03", "d1_canals_05"]),
    ("Water Hazard", ["d1_canals_06", "d1_canals_07", "d1_canals_08", "d1_canals_09", "d1_canals_10",
                      "d1_canals_11", "d1_canals_12", "d1_canals_13"]),
    ("Black Mesa East", ["d1_eli_01", "d1_eli_02"]),
    ("We Don't Go To Ravenholm", ["d1_town_01", "d1_town_01a", "d1_town_02", "d1_town_03", "d1_town_02a",
                                  "d1_town_04", "d1_town_05"]),
    ("Highway 17", ["d2_coast_01", "d2_coast_03", "d2_coast_04", "d2_coast_05", "d2_coast_07", "d2_coast_08"]),
    ("Sandtraps", ["d2_coast_09", "d2_coast_10", "d2_coast_11", "d2_coast_12", "d2_prison_01"]),
    ("Nova Prospekt", ["d2_prison_02", "d2_prison_03", "d2_prison_04", "d2_prison_05"]),
    ("Entanglement", ["d2_prison_06", "d2_prison_07", "d2_prison_08", "d3_c17_01"]),
    ("Anticitizen One", ["d3_c17_02", "d3_c17_03", "d3_c17_04", "d3_c17_05", "d3_c17_06a", "d3_c17_06b",
                         "d3_c17_07", "d3_c17_08"]),
    ("Follow Freeman!", ["d3_c17_09", "d3_c17_10a", "d3_c17_10b", "d3_c17_11", "d3_c17_12", "d3_c17_12b",
                         "d3_c17_13"]),
    ("Our Benefactors", ["d3_citadel_01", "d3_citadel_02", "d3_citadel_03", "d3_citadel_04"]),
    ("Dark Energy", ["d3_citadel_05", "d3_breen_01"]),
]
for chapter, ms in maps:
    for i, m in enumerate(ms):
        add(LV, f"{chapter} ({i + 1}/{len(ms)})", "map " + m,
            f"Load {m}. Starts with no weapons or suit unless the map gives them; "
            "use Cheats > All weapons and ammo.")

# ---- Display ---------------------------------------------------------------
D = "Display"
add(D, "FPS counter", "toggle cl_showfps", "Frame-rate counter in the corner (on/off).")
add(D, "Position readout", "toggle cl_showpos", "Map position, angles and speed (on/off).")
add(D, "Field of view 90", "fov_desired 90", "Wider view (the highest Half-Life 2 allows).")
add(D, "Field of view 75", "fov_desired 75", "Half-Life 2's default.")
add(D, "Brightness: darker", "mat_monitorgamma 2.6", "Darker picture.")
add(D, "Brightness: default", "mat_monitorgamma 2.2", "Default picture.")
add(D, "Brightness: brighter", "mat_monitorgamma 1.8", "Brighter picture.")
add(D, "Motion blur", "toggle mat_motion_blur_enabled", "Camera motion blur (on/off).")
add(D, "Weapon model", "toggle r_drawviewmodel", "Show or hide the weapon in your hands.")
add(D, "HUD", "toggle cl_drawhud", "Show or hide the whole HUD.")
add(D, "Crosshair", "toggle crosshair", "Show or hide the crosshair.")
add(D, "Subtitles", "toggle cc_subtitles; closecaption 1", "Dialogue subtitles (on/off).")
add(D, "Closed captions", "toggle closecaption", "Captions for dialogue and sound effects (on/off).")
add(D, "Frame-rate cap 60", "fps_max 60", "Limit to 60 fps (default).")
add(D, "Frame-rate cap 30", "fps_max 30", "Steadier 30 fps.")

# ---- Audio -----------------------------------------------------------------
A = "Audio"
add(A, "Volume 100%", "volume 1", "Master volume.")
add(A, "Volume 50%", "volume 0.5", "Master volume.")
add(A, "Volume 20%", "volume 0.2", "Master volume.")
add(A, "Music on", "snd_musicvolume 1", "Music volume full.")
add(A, "Music off", "snd_musicvolume 0", "Mute the music.")
add(A, "Stop all sounds", "stopsound", "Silences sounds currently playing.")
add(A, "Restart audio", "snd_restart", "Restarts the sound system (if sound cuts out).")

# ---- Controls --------------------------------------------------------------
K = "Controls"
add(K, "Weapon menu: pictures", "hud_fastswitch 2", "Hold L1 shows weapon pictures around the crosshair (default).")
add(K, "Weapon menu: quick", "hud_fastswitch 1", "Switch weapons instantly without the picture menu.")
add(K, "Look speed: slow", "joy_yawsensitivity -1.0; joy_pitchsensitivity 0.7", "Slower right-stick look.")
add(K, "Look speed: default", "joy_yawsensitivity -1.5; joy_pitchsensitivity 1", "Default right-stick look.")
add(K, "Look speed: fast", "joy_yawsensitivity -2.5; joy_pitchsensitivity 1.5", "Faster right-stick look.")
add(K, "Invert look", "joy_inverty 1", "Push up to look down.")
add(K, "Normal look", "joy_inverty 0", "Push up to look up.")
add(K, "Menu pointer speed: slow", "ps5_menu_cursor_speed 900", "Slower menu pointer.")
add(K, "Menu pointer speed: default", "ps5_menu_cursor_speed 1400", "Default menu pointer.")
add(K, "Menu pointer speed: fast", "ps5_menu_cursor_speed 2200", "Faster menu pointer.")
add(K, "Reload controller layout", "exec 360controller.cfg", "Restore the PS5 button layout.")

# ---- Troubleshooting -------------------------------------------------------
T = "Troubleshooting"
add(T, "Freeze test: single-thread rendering", "mat_queue_mode 0",
    "Draw on the game's main thread instead of a separate render thread (may lower fps). Main freeze test.")
add(T, "Multi-thread rendering (default)", "mat_queue_mode -1",
    "Normal: the engine decides, which on PS5 uses a separate render thread.")
add(T, "Memory release fix on (default)", "ps5_deferred_free 1",
    "Free graphics memory only after the graphics chip has finished with it. Fix for the Barney/Kleiner freeze.")
add(T, "Memory release fix off", "ps5_deferred_free 0",
    "Old behaviour: free graphics memory immediately (can freeze). For testing.")
add(T, "Freeze test: hide debug text", "developer 0; con_drawnotify 0",
    "Stop drawing developer messages over the game (shown when args.txt has -dev 2).")
add(T, "Show debug text again", "developer 2; con_drawnotify 1",
    "Developer messages drawn at the top-left again, as with -dev 2.")
add(T, "Shader cache fix on (default)", "ps5_shader_flush 1",
    "Flush the graphics chip's caches the first times a newly built shader is used. Fix for the freeze when Kleiner/Barney appear.")
add(T, "Shader cache fix: every draw", "ps5_shader_flush 2",
    "Flush shader caches on every draw (slower). Try this if the default still freezes.")
add(T, "Shader cache fix off", "ps5_shader_flush 0",
    "Original behaviour (freezes sometimes, once per launch). For testing.")
add(T, "Batch cache fix on", "ps5_batch_flush 1",
    "Every grouped draw flushes its own data caches instead of sharing one (a bit slower). Extra freeze test.")
add(T, "Batch cache fix off (default)", "ps5_batch_flush 0",
    "Original grouped-draw behaviour.")
add(T, "Monitor lock-step on", "ps5_monitor_sync 1",
    "When a security camera first starts, wait for the graphics chip on each draw for a moment. Did not stop the freeze; for testing.")
add(T, "Freeze test: CPU frame copy on", "ps5_cpu_present 1",
    "Copy each finished frame to the screen with the CPU instead of the graphics chip (lower fps). Freeze test.")
add(T, "Freeze test: CPU frame copy off (default)", "ps5_cpu_present 0",
    "Normal: the graphics chip copies each frame to the screen.")
add(T, "Freeze test: CPU frame flip on", "ps5_cpu_flip 1",
    "Wait for the graphics chip to finish each frame, then show it (slightly lower fps). Freeze test.")
add(T, "Freeze test: CPU frame flip off (default)", "ps5_cpu_flip 0",
    "Normal: the frame is shown as soon as the graphics chip reaches it.")
add(T, "Freeze test: all CPU screen copies on", "ps5_cpu_blits 1",
    "Every screen copy (frame, water/glass refraction, effects) done by the CPU (much lower fps). Freeze test.")
add(T, "Freeze test: all CPU screen copies off (default)", "ps5_cpu_blits 0",
    "Normal: the graphics chip does screen copies.")
add(T, "Monitor lock-step long", "ps5_monitor_sync 2",
    "When a security camera first starts, wait for the graphics chip after every draw for about a minute (slow while it lasts). Freeze test.")
add(T, "Monitor lock-step off (default)", "ps5_monitor_sync 0",
    "No waiting when a monitor camera starts.")
add(T, "Freeze fix test: hide monitor overlay", "ps5_skip_material dev/dev_prisontvoverlay",
    "Hide the TV overlay drawn on top of security monitors (the last thing drawn before every freeze). The live picture stays.")
add(T, "Freeze fix test: draw monitor overlay", "ps5_skip_material",
    "Draw the monitor overlay again.")
add(T, "Freeze finder: lock-step drawing ON", "ps5_sync_draws 1",
    "Slow: waits for the graphics chip after every draw, so a freeze stops on the exact draw that causes it. Turn on just before Barney's room, wait 10 s after a freeze, then send the next launch's log.")
add(T, "Freeze finder: lock-step drawing off", "ps5_sync_draws 0",
    "Normal speed again.")
add(T, "Live screens: moving picture (default)", "cl_drawmonitors 1",
    "Live camera screens on. They can freeze the game in Barney's and Kleiner's rooms; the tests below help find why.")
add(T, "Live screens: still picture", "cl_drawmonitors 0",
    "Monitors keep showing a picture but it no longer updates, so characters on them do not move. The setting that made the freeze rarer.")
add(T, "Monitor test A: no eyes", "cl_drawmonitors 1; r_eyes 0",
    "Screens on, characters' eyes not drawn. Run near Barney's screen: does it still freeze?")
add(T, "Monitor test B: no face animation", "cl_drawmonitors 1; r_eyes 1; r_flex 0",
    "Screens on, eyes back, facial animation off. Does it still freeze?")
add(T, "Monitor test C: no 3D skybox", "cl_drawmonitors 1; r_eyes 1; r_flex 1; r_3dsky 0",
    "Screens on, distant 3D skybox off. Does it still freeze?")
add(T, "Monitor test D: no characters", "sv_cheats 1; cl_drawmonitors 1; r_eyes 1; r_flex 1; r_3dsky 1; r_drawothermodels 0",
    "Screens on, no models drawn anywhere (cheat). Does it still freeze?")
add(T, "Freeze test: skip monitor shader", "ps5_skip_shader unlittwotexture",
    "Don't draw surfaces using the UnlitTwoTexture shader (the security monitors). If the freeze stops, that confirms the cause.")
add(T, "Clear camera picture now", "ps5_camera_rt_clear",
    "Give the monitors' camera picture defined contents (test; off by default).")
add(T, "Freeze test: draw everything", "ps5_skip_shader",
    "Undo the skip test (draw all shaders again).")
add(T, "Teleport guard on", "ps5_model_guard 1",
    "Don't draw a character on the frame it is teleported, or with broken bone data. Prevents the Kleiner/Barney teleport freeze.")
add(T, "Teleport guard off (default)", "ps5_model_guard 0",
    "Draw every model every frame (the original behaviour).")
add(T, "Character test E: software skinning", "sv_cheats 1; mat_softwareskin 1",
    "Bend character models on the CPU instead of the GPU. Does the freeze stop?")
add(T, "Character test F: no shadows", "mat_softwareskin 0; r_shadows 0",
    "Characters and objects without their soft shadows. Does the freeze stop?")
add(T, "Character test G: no teeth/mouths", "r_shadows 1; r_teeth 0",
    "Characters drawn without teeth. Does the freeze stop?")
add(T, "Character test H: no see-through parts", "sv_cheats 1; r_teeth 1; r_drawtranslucentrenderables 0",
    "Skip glass, glasses and other see-through parts. Does the freeze stop?")
add(T, "Character tests: reset", "mat_softwareskin 0; r_shadows 1; r_teeth 1; r_drawtranslucentrenderables 1",
    "Undo the character tests.")
add(T, "Monitor tests: reset", "r_eyes 1; r_flex 1; r_3dsky 1; r_drawothermodels 1",
    "Undo the monitor tests.")
add(T, "Eye shine: texture (default)", "r_glint_procedural 1", "Eye reflections drawn by texture upload (the PS5 default).")
add(T, "Eye shine: render target", "r_glint_procedural 0", "Source's usual eye reflections. May cause white eyes or freezes on PS5.")
add(T, "Save console to file", "condump", "Writes the console text to condumpNNN.txt in the app's data.")
add(T, "Show engine version", "version", "Prints the engine version in the console.")
add(T, "Clear console", "clear", "Empties the console.")

# ---- Developer tests ---------------------------------------------------------
X = "Developer tests"
add(X, "Sound freeze test: play new sounds", "ps5_freeze_recorder 1; ps5_sound_stress play 300",
    "Plays up to 300 voice/NPC/ambient sounds the game hasn't used yet, one every few frames, to reproduce the freeze from the menu. Recorder on.")
add(X, "Sound freeze test: load new sounds only", "ps5_freeze_recorder 1; ps5_sound_stress load 300",
    "Same, but only loads each sound without playing it (tells loading apart from playing).")
add(X, "Sound freeze test: stop", "ps5_sound_stress_stop", "Stop the sound freeze test.")
add(X, "Freeze recorder on", "ps5_freeze_recorder 1",
    "Record every draw to crumbs.bin so the next launch can report a freeze (costs some fps). Also: -ps5_freeze_recorder in args.txt.")
add(X, "Freeze recorder off (default)", "ps5_freeze_recorder 0",
    "Stop recording draws.")

# Player-facing entries stay in Troubleshooting; the freeze-hunting switches
# move to "Developer tests" (kept for testing, out of the way for players).
KEEP = {"Live screens: moving picture (default)", "Live screens: still picture",
        "Freeze test: hide debug text", "Show debug text again",
        "Eye shine: texture (default)", "Eye shine: render target",
        "Save console to file", "Show engine version", "Clear console"}
RENAME = {"Freeze test: hide debug text": "Hide developer messages",
          "Show debug text again": "Show developer messages"}
for i, (cat, label, cmd, desc) in enumerate(L):
    if cat == T:
        if label in KEEP:
            L[i] = (cat, RENAME.get(label, label), cmd, desc)
        else:
            L[i] = (X, label, cmd, desc)
# Troubleshooting before Developer tests.
L.sort(key=lambda e: e[0] == X)


def kv_escape(s):
    return s.replace('"', "'")

def build_kv():
    out = [f"// ps5-commands-version {VERSION}",
           "// Triangle command menu (PS5 port). Generated: replaced when the app updates.",
           "// Add your own commands to ps5_commands_custom.txt instead.",
           '"Commands"', "{"]
    cats = []
    for c, *_ in L:
        if c not in cats:
            cats.append(c)
    for c in cats:
        out.append(f'\t"{c}"')
        out.append("\t{")
        n = 0
        for cat, label, cmd, desc in L:
            if cat != c:
                continue
            n += 1
            out.append(f'\t\t"{n}"')
            out.append("\t\t{")
            out.append(f'\t\t\t"label"\t"{kv_escape(label)}"')
            out.append(f'\t\t\t"command"\t"{kv_escape(cmd)}"')
            out.append(f'\t\t\t"description"\t"{kv_escape(desc)}"')
            out.append("\t\t}")
        out.append("\t}")
    out.append("}")
    return "\n".join(out) + "\n"

def c_string(text):
    lines = []
    for line in text.splitlines(keepends=True):
        esc = line.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t")
        lines.append(f'\t"{esc}"')
    return "\n".join(lines)

here = Path(__file__).resolve().parent
kv = build_kv()
(here / "ps5_commands.txt").write_text(kv)
(here / "zmr_commands.c").write_text(
    "/* Generated by gen_commands.py: the Triangle command menu's default list. */\n"
    f"const int zmr_commands_version = {VERSION};\n"
    "const char zmr_commands_text[] =\n" + c_string(kv) + ";\n")
print(f"{len(L)} commands, version {VERSION}")
