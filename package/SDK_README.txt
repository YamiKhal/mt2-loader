MT2 Loader SDK {version}
Everything needed to write native plugins for MMORPG Tycoon 2, in C++. The full guide is LOADER_MODDING.md.
A plugin says what to change, and the loader does the rest:
  game::in("mmoCharacter::EquipWeaponModel").call("std::operator==").with_text("humanoid").returns(true);

START A PLUGIN
1. mt2sdk setup           says what this PC has and what's missing, and where to get it.
2. Install Visual Studio Community (free) with "Desktop development with C++".
   (Or MSYS2's MinGW-w64 with GCC 13 or newer, or clang 17 or newer: plugins are C++20.)
3. In this folder: mt2sdk new MyPlugin "My Plugin" --mappings <mt2-mappings folder>
   (--mappings is optional: it names more of the game's fields in the project's include\mt2game.hpp.)
4. In Visual Studio: File > Open > Folder, pick MyPlugin, then Build > Build All.
   The DLL goes into MyPlugin\mod\native, and mod\ is copied into the game's mod folder.
5. Start the game. mt2sdk log shows the loader's log as the game writes it, with your plugin's lines.
   To debug: start the game from Steam, then attach (Visual Studio: Debug > Attach to Process, MT2.exe;
   VS Code: "Attach to MT2" in the project's launch settings).

THE GAME'S CLASSES IN YOUR PLUGIN
  include\mt2game.hpp     a C++ class for each game class, with its fields: mt2::mmoNPC npc(pointer);
                          npc.state(), npc.level(), npc.spawnPoint(). Remake it with mt2sdk headers.

READ THE GAME
  mt2sdk find <words>     search the game's functions and globals by name (--cpp: a line to paste)
  mt2sdk class <class>    what a class is built on, its size, source file and fields
  mt2sdk enum [name]      the game's enums and their words
  mt2sdk uses <function>  what it calls, and the texts, globals and numbers it uses
  mt2sdk dis <function>   its code, instruction by instruction, with names and texts
  mt2sdk refs <name>      everywhere the game calls or uses it
  mt2sdk text <words>     texts the player sees, and the code that uses them
  mt2sdk vtable <class>   a class's virtual functions
  mt2sdk decompile        the whole game as C++ source, in the game's own folders (needs Ghidra and Java 21:
                          https://ghidra-sre.org, https://adoptium.net). First run about 40 minutes, then 20.
  mt2sdk reference        a page per class to browse or share (names only, no game code)
  mt2sdk check <mod>      check a plugin mod the way the loader will
  mt2sdk build            which game build you have, and whether this SDK knows it

AFTER A GAME UPDATE
  mt2sdk diff <old MT2.exe>   what changed, and which mapped facts need a look (keep a copy of the old exe)

SHARE WHAT YOU FIND
  mt2-mappings is where modders write down what they found out about the game's code (CC0).
  mt2sdk mappings check <folder> checks it against your game; mt2sdk mappings pull adds what you named in
  Ghidra. How: mt2-mappings\CONTRIBUTING.md.

WHAT'S HERE
  mt2sdk.exe              the tool above
  include\mt2loader.hpp   the plugin API: the one file a plugin includes
  include\mt2loader.h     the C layer underneath it (only for plugins written in plain C)
  template\               what "mt2sdk new" copies
  ghidra\                 the Ghidra scripts "mt2sdk decompile" runs (also usable from Ghidra's Script Manager)
  examples\               plugins to start from: hello_plugin (the smallest), weapons_any_rig (weapons on
                          every rig with a hand: finds and patches game code safely), top_spenders (a new
                          Leaderboards tab and a Features checkbox: data files and a plugin together),
                          quest_markers (each quest giver's marker picked in a new tab, kept in the save)

The game needs the MT2 Loader installed to run plugins (MT2 Mod Manager has a button for it).
MIT license (LICENSE.txt).
