<p align="center">
  <img src="docs/images/logo.png" alt="SA:COOP" width="440">
</p>

<p align="center">
  <a href="https://github.com/Karimov-Akbar/sa-coop/releases/latest"><img src="https://img.shields.io/github/v/release/Karimov-Akbar/sa-coop" alt="Release"></a>
  <a href="https://github.com/Karimov-Akbar/sa-coop/releases"><img src="https://img.shields.io/github/downloads/Karimov-Akbar/sa-coop/total" alt="Downloads"></a>
  <a href="https://github.com/Karimov-Akbar/sa-coop/actions/workflows/build.yml"><img src="https://img.shields.io/github/actions/workflow/status/Karimov-Akbar/sa-coop/build.yml?branch=main" alt="Build"></a>
  <a href="LICENSE"><img src="https://img.shields.io/github/license/Karimov-Akbar/sa-coop" alt="MIT license"></a>
  <img src="https://img.shields.io/badge/GTA_San_Andreas-1.0_US-orange" alt="GTA San Andreas 1.0 US">
  <a href="https://t.me/cavalier_mods"><img src="https://img.shields.io/badge/News-Telegram-26A5E4?logo=telegram&logoColor=white" alt="News on Telegram"></a>
  <a href="https://dalink.to/itscavalier"><img src="https://img.shields.io/badge/Support_the_project-DonationAlerts-F57507" alt="Support the project"></a>
  <a href="https://github.com/sponsors/Karimov-Akbar"><img src="https://img.shields.io/badge/GitHub-Sponsors-EA4AAA?logo=githubsponsors&logoColor=white" alt="GitHub Sponsors"></a>
</p>

SA:COOP brings the GTA San Andreas story to **2–4 players**. The host supplies the save and starts missions.
Each teammate joins the same world with their own character, money, clothes and skills.

![Playing together in Los Santos](docs/images/hero.jpg)

## Install and connect

Requires **GTA San Andreas 1.0 US on Windows**. Downgrade Steam or Rockstar Games Launcher editions first.
All players need the same mod version and compatible story scripts.

1. Download [SA-COOP.zip](../../releases/latest) and extract it beside `gta_sa.exe`.
2. Run `install.bat` to install the bundled ASI loader if needed.
3. Start GTA and press **F9**. Choose **EN** at the top for English.
4. The host opens **Host a game**, selects a save or **New game**, chooses 2–4 players, and starts the server.
   Hosting from a running game uses the current world.
5. Friends open **Join a game**, enter the host's invite code, and connect.

Share invite codes with your teammates. They contain the host's external IP address.
Allow `coop-server.exe` through Windows Firewall when prompted.

UPnP and NAT traversal usually handle connections. If a connection fails, forward **UDP 7788** or configure
`turn` in `coop/server.ini`. The archive includes a detailed Russian guide, `Инструкция.txt`.

To update, extract the new archive over the existing files. Settings and teammate profiles are preserved.
For a new story, the host plays the prologue while teammates wait, then everyone joins CJ at the bike.

## Features

- **Story co-op.** Missions begin when everyone reaches the marker. Objectives, dialogue, rewards, mission weapons,
  checkpoints and world changes are shared. Teammates can complete mission tasks, fly their own aircraft,
  use jetpacks and RC toys, and join vehicle gunner sections.
- **Shared cutscenes.** Teammates appear beside CJ and watch synchronized scenes. Skipping requires everyone's agreement.
- **Shared world.** Traffic, pedestrians, police, weather, time, fires, damage, collectibles, properties and gang territories
  stay synchronized as players travel.
- **Vehicles.** Drive separately or ride together, shoot from vehicles, share the driver's radio, tune cars and use trailers,
  tow trucks, trains, hydraulics and safe-house garages.
- **Player progress.** Money, appearance, gym progress and weapon skills belong to each player. Driving skills use the team's
  best values. Completed activities and their completion rewards count for everyone. The host saves profiles by nickname.
- **Activities.** Invite teammates to jobs, races and challenges with **Y** within 20 seconds of starting. Press **U**
  during that window to challenge someone to a contest. Invitations use **Y/N**. Pool and contests support two players.
- **Communication.** Text and proximity voice chat, shared map waypoints, player name tags and temporary ping beacons.
- **Customization.** Named skins with previews and native movement animations, plus an animation menu with poses, dances,
  paired interactions and support for custom IFP files.
- **Team rules.** The host controls friendly fire, shared lives and teammate teleport requests. Players choose English
  or Russian independently in the F9 window.

<p align="center">
  <img src="docs/images/cutscenes.gif" width="49%" alt="Teammates beside CJ in cutscenes">
  <img src="docs/images/drive-by.gif" width="49%" alt="Players shooting from a moving car">
  <img src="docs/images/animations.gif" width="49%" alt="The animation picker and a dance">
  <img src="docs/images/pool.gif" width="49%" alt="Two players playing pool">
</p>

## Controls

| Action | Keyboard / mouse | Controller |
|---|---|---|
| Co-op window | F9 | Select + D-pad Up |
| Ping the camera's target | F5 | Select + Circle |
| Skin picker | F6 | Select + Cross |
| Animation menu | U | Select + Square |
| Players, gifts and teleport requests | F2 | Select + Triangle |
| Revive a nearby teammate | Hold H | Hold Triangle |
| Enter a car as passenger | G | Hold Triangle |
| Aim / fire from a vehicle | Right / left mouse button | Game controls |
| Text chat | T, then Enter to send or Esc to close | |
| Voice chat | Hold B by default | |
| Hide / show the interface | F7 | |
| Offer / accept / decline a cutscene skip | Enter / Enter / Esc | |

In the skin picker, use the D-pad or left stick to move, Cross to apply, Circle to return, and L2/R2 to scroll.
Change chat, passenger, player list, voice and revive keys through **F9 → Keys**. Voice supports hold-to-talk,
toggle, always-on and off modes.

### Pings and revives

Point the camera at an object or surface and press the ping shortcut. Nearby teammates in the same area see a
pulsing beacon for **6.5 seconds**, matching your chosen radar blip colour.

With shared lives off, fatal damage on foot can leave you downed when a teammate is nearby in the shared world.
A teammate can approach and hold the revive button for **5 seconds**. The default rescue window is **60 seconds**.
With shared life enabled, any player's death fails the host's story mission.

### Commands and appearance

| Command | Action |
|---|---|
| `/help`, `/players` | Help and player numbers |
| `/tp [player]`, `/tphere [player]` | Request teleport to / summon a teammate, answered with Y or N |
| `/kick <nickname> [reason]` | Host removes a player |
| `/anims [number]` | Open the animation menu or play an animation |
| `/skin menu` | Open the skin picker |
| `/skin <ID>`, `/skin next`, `/skin prev` | Choose or cycle character models |
| `/skin 0` | Return to CJ |

Skin choices persist in `[player]` under `skin` in `coop/coop.ini`. **Use skin animations** selects the skin's idle,
walk, run and sprint set and saves as `skin_animations`. Change skins while standing freely on foot.
Special character slots 290–299 are excluded, and CJ's clothing applies to CJ.

Paired animations require the partner's acceptance with **Y**. Press **Space** or **F** to leave a sustained pose.
Put custom `.ifp` files in `coop/anims`. Teammates need matching files to see those animations.

## Settings

Edit `coop/coop.ini` for these options.

| Section | Setting | Effect |
|---|---|---|
| `[game]` | `down_seconds`, `revive_seconds` | Rescue window and revive duration |
| `[game]` | `partner_blip = 0`, `name_tags = 0` | Hide teammate radar marks or name tags |
| `[game]` | `gps = 0` | Disable road routes to waypoints and mission destinations |
| `[game]` | `windowed = 1`, `borderless = 1` | Use a borderless window |
| `[game]` | `window_title = ...` | Give the window a custom title for recording |
| `[debug]` | `clean_screen = 1` | Hide the mod's chat, windows and overlays |

## Compatibility and limitations

- Use the same nickname to keep your saved profile. Loading a host save restores teammate weapons to that save's state.
- The host starts story missions and Cesar's races. Teammates receive newly unlocked story content when the world reloads,
  usually at a mission marker or after ten minutes without missions.
- Teammate girlfriend progress comes from the host. Business income belongs to the shared world.
- Teammates cannot board the host's trains during story missions.
- Shop staff and activity characters may differ between games. Broken street objects reset after a few minutes.
- Each player hears their own User Track Player music.
- Modloader is supported. Players see their own models, textures and effects. Everyone needs mods that change story scripts
  or saves. A custom model requires the same model installed on other clients, and unavailable player skins appear as CJ.

## Bugs and support

Report issues through [GitHub Issues](../../issues/new/choose) or the [Telegram group](https://t.me/sacoop_forum).
Include the mod version, game build and steps to reproduce. The issue form accepts English answers.

Attach `coop/coop.log` from the affected player, `coop/crash.log` for crashes, and the host's
`coop/server-data/server.log` for connection problems. Use `coop/coop.old.log` if the game has restarted since the issue.

## Build from source

Requires Windows, Python 3.10+ and PowerShell. Download the llvm-mingw toolchain, then build the distribution.

```powershell
powershell -ExecutionPolicy Bypass -File tools/get-toolchain.ps1
python tools/build.py dist
python tools/fsd_check.py
python tools/i18n_check.py
```

The release archive is written to `dist/SA-COOP.zip`. GitHub Actions also produces build artifacts and publishes
releases for version tags.

Sources are split across `src/client`, `src/server`, `src/loader` and `src/common`. Each application follows
Feature-Sliced Design. See [ARCHITECTURE.md](docs/ARCHITECTURE.md) for implementation details in Russian.

## Third-party code

- [ENet](http://enet.bespin.org) (MIT license): network transport, `third_party/enet`.
- [MinHook](https://github.com/TsudaKageyu/minhook) (BSD 2-Clause license): function hooking, `third_party/minhook`.
- [Font Awesome Free](https://fontawesome.com) (CC BY 4.0 license): the voice chat's microphone icon,
  `src/client/features/voice/ui/microphone.svg`.
- [llvm-mingw](https://github.com/mstorsjo/llvm-mingw): the compiler, downloaded during the build.
- The Packs tab's animations (`package/coop/packs`) come from other authors' mods, only the new animations of each:
  Salute by KOT33 ([LibertyCity](https://libertycity.net/files/gta-san-andreas/94399-salute.html)), the GTA V greeting by Alexander (Skone Modding)
  ([LibertyCity](https://libertycity.net/files/gta-san-andreas/98406-military-greeting-mod-gta-v-to-sa.html)), rap by Aleksey_Move
  ([LibertyCity](https://libertycity.net/files/gta-san-andreas/79275-animation-rapping.html)), poses by Androsz and mrandres5555
  ([LibertyCity](https://libertycity.net/files/gta-san-andreas/132610-new-static-animations.html)), poses on the ground by Ruslan Konorov
  ([LibertyCity](https://libertycity.net/files/gta-san-andreas/132637-static-animations.html)), idle actions from GTA LCS converted by Mystertv and Junior_Djjr
  ([LibertyCity](https://libertycity.net/files/gta-san-andreas/140391-new-idle-animations-from-gta-lcs.html)), disco dances by GRAF MC
  ([1](https://libertycity.net/files/gta-san-andreas/119882-anim-dance.html), [2](https://libertycity.net/files/gta-san-andreas/127294-anim-dance-v2.html)), Fortnite dances by Sergio Persson
  ([LibertyCity](https://libertycity.net/files/gta-san-andreas/168251-dances-from-fortnite-music-v.2.html)), party dances by ATField02_GR
  ([LibertyCity](https://libertycity.net/files/gta-san-andreas/242533-new-dance-animations.html)), breakdance and tricks by Arch
  ([LibertyCity](https://libertycity.net/files/gta-san-andreas/26294-new-moves-breakdance-somersaults-and-more.html)), acrobatics by Spirit
  ([LibertyCity](https://libertycity.net/files/gta-san-andreas/34680-acrostreet-and-tricking-mod-v1.html)), parkour by Paul_Cortez
  ([MTA wiki](https://wiki.multitheftauto.com/wiki/EngineLoadIFP)).
- Engine addresses and structures were checked against [gta-reversed](https://github.com/gta-reversed/gta-reversed)
  and [plugin-sdk](https://github.com/DK22Pac/plugin-sdk).

## License

The SA:COOP code is released under the MIT license (see [LICENSE](LICENSE)). ENet, MinHook and the Font Awesome
icon have their own licenses, and their texts are in `third_party` and in the `coop\licenses` folder of the archive.

SA:COOP is not affiliated with Rockstar Games or Take-Two Interactive. You need your own copy of GTA San Andreas,
and the repository contains no game files.
