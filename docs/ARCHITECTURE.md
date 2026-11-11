# SA:COOP architecture

SA:COOP targets GTA San Andreas 1.0 US (`gta_sa.exe`, 14,383,616 bytes). The original design described here
started with two players. Engine addresses and structures were checked against gta-reversed, plugin-sdk,
and the game executable. Network messages are defined in `src/common/api/protocol/Protocol.h`.

## Components

| Component | File | Purpose |
|---|---|---|
| Client | `coop.dll` (installed as `coop.asi`) | Runs inside `gta_sa.exe` and synchronizes players, vehicles, NPCs, missions, text chat, and voice chat |
| Server | `coop-server.exe` | Accepts connections, assigns roles, relays traffic, and stores progression profiles and world saves |
| ASI loader | `vorbisFile.dll` (`src/loader`) | Loads `*.asi` from the game folder and `scripts\` on clean installations, and forwards vorbisfile functions to `vorbisHooked.dll` |
| Tools | `tools/` | Build (`build.py`), layer checks (`fsd_check.py`), compiler download (`get-toolchain.ps1`), and reverse-engineering tools (`re/`) |

The ASI loader loads the client. On a clean installation, `install.bat` renames the original
`vorbisFile.dll` to `vorbisHooked.dll` and installs the bundled loader in its place. The game loads this DLL
at startup. The installer identifies the original by the lowercase string `vorbisfile.dll`, using a
case-sensitive `findstr` check. Widescreen Fix's Ultimate ASI Loader can already occupy the
`vorbisFile.dll` slot without a `vorbisHooked.dll`. The old installer renamed that loader too, which caused
it to print “This library isn't supported” and close the game (28 October). The installer now leaves that
loader in place and restores a previously renamed copy if the bundled loader is installed. The bundled
loader can be identified by its `vorbisHooked.*` forwarders. Modloader, CLEO, SilentPatch, and other mods
continue to work as before.

Plugins built with older versions of the injector library, including MixSets, Peds Extender, CLEO+, and
Framerate Vigilante, use `add byte ptr [esp+0Ch], 4` in wrappers that save registers for inline hooks. If
the low byte is FC, this addition loses the carry and the hook sees a stack address 256 bytes too low.
A hook that changes its return address then writes to the wrong location and returns into the middle of
an instruction. Peds Extender crashed at 6133DB when the game selected a train driver. The mod's own hooks
deepen the stack, exposing the problem in code paths that previously worked. On the first frame and when
a game starts, `FixPluginWrappers` in `Patches.cpp` finds these wrappers in loaded plugins and replaces
their byte-sized additions and subtractions with 32-bit operations of the same instruction length.

## Source layout

Sources are arranged according to Feature-Sliced Design. Each of the three programs (`src/client`,
`src/server`, `src/loader`) is arranged as a separate FSD application with top-down layers: `app`, `pages`,
`widgets`, `features`, `entities`, `shared`. The code common to all three (protocol, networking, log, ini,
utilities) is in `src/common`.

| Client layer | What's in it? | Examples |
|---|---|---|
| `app` | Entry point, main loop, module event dispatch, game patches, and crash handler | `app/core/Core.cpp`, `app/patches` |
| `pages` | F9 and F2 windows | `pages/connect-menu`, `pages/players-menu` |
| `widgets` | Overlay: contest scoreboard, status, mission marker waiting, and skip voting | `widgets/overlay` |
| `features` | Everything that synchronizes: story and cutscenes, progression, garages, voice, fire, weather and time | `features/story`, `features/progress` |
| `entities` | Entities: session, player, vehicle, pedestrian, host mission, and teammate profile | `entities/player`, `entities/session` |
| `shared` | Engine wrappers, network client, drawing and input, chat as a component, config, hooks | `shared/game`, `shared/ui/chat` |

Each slice has its own folder, and the complex slice is divided further: `features/story` consists of
`missions`, `mission-speech`, `cutscene-partner` and `cutscene-clips`. The layer connects only the lower
layers and its own slice. Entities can connect to each other (as FSD cross-imports through `@x` work). The
rule checks `tools/fsd_check.py`, it runs the build on GitHub.

The lower layers sometimes need an answer from the upper layers, for example, the player needs to know if the
host mission is on. Then the ad is at the bottom, and the definition in the layer to which it belongs:
- `shared/lib/core/Core.h` declares the core class of modules and kernel services, defines them `app/core/Core.cpp`;
- `entities/mission/Mission.h` announces questions about the host's mission, responds `features/story/missions`;
- `entities/profile/Profile.h` announces the preservation of the partner profile, it is performed by `features/progress`.

Test bot, test configs, runners and copies of the game for checks are not included in the repository. The
assembly connects the bot with the key `python tools/build.py --with <bot folder>`.

## Networking

As a transport, ENet is used over UDP with four channels: 0 for reliable events, 1 for unreliable states like
positions, 2 for voice and 3 for file transfer.

Players are connected by a star via a server. Game messages (id from 64) the server sends to other players,
substituting the sender's id, and service (input, chat commands, profiles, peace preservation) processes
itself. Therefore, new types of synchronization do not require edits on the server.

Roles two. The host (the first person to log in, usually the one who created the server) owns the plot: the
missions go to him, and his preservation serves as a common world. The second player is called a partner.

ENet operates in a separate client stream. The game takes incoming messages and gives outgoing ones through
the queues.

ENet datagrams are up to 1,392 bytes (1,420 with IP and UDP headers), and these do not run on every line: a
VPN, a mobile network or a router that throws out snippets of IP, they are lost. A reliable package that never
reached the connection breaks after 10-20 seconds. Therefore, the client and server set MTU 1200
(`net::kMtu`). When connecting, the parties take the lesser of the two, so one would suffice. Before the
breakup, the client writes to the log the delay, losses, the number of bytes without confirmation and how much
the server was silent.

Through a repeater (TURN) and that's not enough. A friend who connected through it, the server was silent from
the first major transfer (world, cutscene data), even pings did not come, and after 10–14 seconds the
connection broke. ENet does not send ping as long as it has unconfirmed reliable packets, and their full-size
datagrams on this line are lost. Therefore, the client who goes through the repeater asks for the smallest
ENet datagrams (`net::kRelayMtu`, 576 bytes) when connecting and declares the incoming band 128 KB / s
(`net::kRelayBandwidth`): from it the server outputs a window, and he has no more than 8 KB of unconfirmed
datagrams to such a player. The sockets of the repeater on the server received buffers of 1 MB, and he writes
once every 30 seconds in `server.log` how many datagrams passed in each direction and what is the largest
length.

The client’s queue does not grow endlessly. The game, which is worth (long load, hover, alto tab without
`run_in_background`), saved up all the fortunes and then applied them at once, already outdated. After 4,000
packets in the queue, new states and voice are discarded, events remain. When the queue is broken, the cause
of the failure or a piece of the world from the past connection fell into the next one. Messages from a player
who is no longer in the session are discarded by the client. The server sends states (channel 1) only to those
who are in the game: those who are still loading do not need them.

The protocol number is first in the greeting, and the server checks it until the rest is parsed: an old client
with a short greeting received “damaged data” instead of a version message. Nicks that Windows considers
devices (CON, PRN, AUX, NUL, COM1–9, LPT1–9), the server does not accept because the player profile is stored
in a file with the nickname. How many places on the server, the host selects in the F9 window before creating
the game: 2, 3 or 4 (`--max`, in `server.ini` line `max_players`). The server will not give more than four, no
matter how much it is (`proto::kMaxPlayers`).

The player who has lost the game, connects again under the same nickname at once. The old connection server
holds up to a timeout ENet (10-20 s), so before such a player was not allowed (“nick is already on the
server”, “no seats”). Now, if the old connection is silent for more than 3 seconds, the server closes it as a
communication break and launches a new one.

## Playing over the internet

At start-up, the server (`src/server/features/internet/Internet.cpp`) sprinkles the UDP port on the router via
UPnP. The lease is taken for an hour and extended, so that a fallen server will not leave the port open
forever. The server then recognizes its external address from the public STUN servers through the same ENet
socket and collects the invitation code `XXXX-XXXX-XXXX` from the external IP, port, and key. The host sees
the code in the chat room and in the F9 window, where there is a “copy” button. In addition, the code is
written to file `server-data\invite.txt`.

A friend enters the code in the F9 window. The client recognizes its external address through STUN, publishes
it to the signal channel ntfy.sh (theme is output from the code), sends the host punch packets and connects.
The server sees the message in the channel and sends packets towards it, so that the NAT opens on both sides.

If the friend and host have one external IP (one apartment or a shared NAT of an operator who does not know
how to “loop”), the client finds the server by a broadcast request on the local network.

The code is stitched with an external IP host, so it is worth sharing it only with those with whom you play.

When there is no direct path in either direction, the repeater rescues
(`src/server/shared/api/turn-relay/TurnRelay.cpp`). The host receives an address on the TURN server. A free
account metered.ca (20 GB per month, setting `turn = metered:<application>:<key>`) or any TURN server with a
login and password is suitable. The host publishes the resulting address as `RELAY ip:port` and gives
permission to each friend who appears on the channel. A friend sends packages to the relay address. For its
NAT, it’s normal outbound traffic to a public server, so it’s always going through. The ENet host sees the
friend as a feast of 127.0.0.1:<port>, because each friend has a local socket to the game port. The client
first tries the direct path and after 4 seconds without a response goes to the repeater. This is verified with
the local TURN server from the tests (`[debug] force_relay = 1`).

Before all this, the server must skip the Windows host firewall. The host's game comes through 127.0.0.1, and
friends, even from the same apartment, were not allowed to many: the Windows Security Warning window remained
behind the full-screen game, and canceled or not seen, it leaves rules that block `coop-server.exe`. The box
only for private networks does not allow it when the network is marked with public. Before starting the
server, the game reads the firewall rules (`INetFwPolicy2`, no administrator rights required,
`shared/lib/firewall/Firewall.cpp`). If the incoming UDP to the server on the current network profile blocks
the rule or none of it is allowed, the host receives a Windows request (UAC): `netsh` deletes all the rules
`coop-server.exe` and adds one permission for all the profiles. In tests, the request is turned off by line
`[debug] firewall_prompt = 0`.

Antiviruses found a threat in the files. They do not have a signature, so the build removes the features that
trained models caught them: all three files have a version resource (`common/config/version/VersionInfo.rc`,
numbers from `COOP_VERSION_STR`), the server also has an icon and a manifest. The release build does not leave
the DWARF sections with the names `/4`, `/67` (`--strip-debug`, flight reports are enough for PDB) in the
files and writes the checksum in the PE header as `/RELEASE` of the Microsoft linker. The server is launched
immediately, not suspended until it is included in the task: create a process suspended and then continue - a
technique that malware replaces someone else's code, and behavioral protections monitor it.

## Players

The local player remains a regular CJ (`CWorld::Players[0]`).

Remote player made real `CPlayerPed`, otherwise you will not get the clothes, fat, muscles and animations of
the player. The slot `CWorld::Players[1]` only gets it during its processing, so one slot serves all copies in
turn, and there can be three of them. If you leave a copy there, the engine will turn on the built-in mode for
two (`CGameLogic::IsCoopGameGoingOn`) with a shared camera, mission ban (`CPlayerPed::CanPlayerStartMission`),
turned off garages, interiors and gang wars. Just in case, `IsCoopGameGoingOn` is intercepted and always
returns `false`.

From the same mode, the tether remained: `CGameLogic::IsPlayerAllowedToGoInThisDirection` does not let the
second player further than 20 m from the first. While the copy is processed in the slot `Players[1]`, the
check worked on it. A copy of the partner, who left our player further than 20 meters, did not go and did not
swim, but stood, and the position lift dragged it behind the owner. Jumping in the same direction beyond 25 m
did not begin at all, so climbing the wall looked like a slide up. The function always returns `true`.

Another check of that mode, `CGameLogic::IsPlayerUse2PlayerControls`, looks at both `CWorld::Players` slots
herself, so the `IsCoopGameGoingOn` switched off didn't hurt her. The copy, which is processed in the slot,
was aimed at the rules of the split screen. The stick of the crouched player with the weapon was counted
through the camera on the axis of the copy itself. If the copy was rotated a few degrees from the owner’s
camera, rolling sideways turned into a step forward or backward. In addition, the angle of sight of the copy
each frame was given from the stick, even stationary, and the copy crouched with the gun trembled. Now, the
function always returns `false`, and the copy is controlled by the same rules as its owner.

The constructor `CPlayerPed` takes player data from the slot (`CWorld::Players[n].m_PlayerData`), and two
copies in the same slot would share the search, clothing, and tilt of the sight. Therefore, each copy has its
own block `CPlayerPedData` by its player number (`g_copyData`): before `CREATE_PLAYER`, the block is copied to
the slot, after it back, and `m_pPlayerData` copies point to it. What the designer has highlighted
(`m_pWanted`, `m_pPedClothesDesc`) remains in the block until the next copy of this player, as in the slot:
the game itself releases the slot data only when exited (`CWorld::ShutDown`). While the copy is being
processed, these two pointers are also in the slot because `FindPlayerWanted` reads them from there. Flag
`CPlayerInfo::m_bCanDoDriveBy` each copy has its own (`RemotePlayer::canDriveBy`): shooting from a window
removes it only from the one that leans out.

`CPlayerPed` occupies one of the eight groups of pedes, and when the pede is removed, the game does not return
it. Each new copy after death would hold another until the groups run out, and script `CREATE_GROUP` without
the free group drops. All copies are divided into one group (`g_copyGroup`), and an extra new copy is
immediately given away (`CPedGroups::RemoveGroup`). Working with a gang of spears is still in the games of
their owners.

The remote mod player is controlled through context-shifting, just like SA-MP and MTA do. In front of his
`CPlayerPed::ProcessControl`, his gamepad buttons, camera and sight parameters and `CWorld::PlayerInFocus = 1`
are substituted, and after a call, local ones are returned. Thanks to this walking, running, jumping, squats,
melee, aiming and shooting reproduces the regular logic of the game. The sight of the local player is not
affected by its processing: the camera weapon mode (`CCamera::m_PlayerWeaponMode`), the sight point and the
target capture mark (`gCrossHair[0]`, its game chooses by the type of pede) return with the rest.

The shot of a copy of another player is drawn by a tracer for the entire length of the bullet
(`CBulletTraces::AddTrace`, as in onboard machine guns). From the player's shot, the game draws only a short
pale piece in a random location, and in a first-person sight, the standing player draws nothing, and the other
player did not see the trace of shots (29.10).

The game does not shoot from a weapon on foot in `ProcessControl`, but after processing the frame
(`CTaskSimpleUseGun::SetPedPosition`), and only so far the information about the weapon that the task has
taken matches the information for the player's skill at this point (`CPed::GetWeaponSkill`, the player's skill
is taken from `CStats`). The copy took them with the skills of the owner, and shot already with the skills of
his player. If the skills of the two players for this weapon are different (newcomer, gangster, master), the
replica aimed and didn't fire and the players didn't see each other's shots (29.10). Now, the shot of the copy
that happens in this frame is done in its context: with the skills of the owner and where he is aiming, and
not at the camera of his player.

The peda (`CPedIntelligence::Process`) is set inside `CPed::ProcessControl`, so almost all of the player’s
logic works in a substituted context, and everything it writes into the common variables also returns:
- both the entire gamepad, along with vibration, and the camera shaking from his sniper rifle (`TheCamera` + 0x12C, +0x5C)
- His skills, endurance and fat during processing are taken from his statistics, and the progression counters after
  them (`m_SprintStaminaCounter` and neighboring, state `UpdateFatAndMuscleStats`) remain their own. Endurance
  + windows and progression texts are turned off at this time (`CStats::bShowUpdateStats`);
- The night and thermal vision goggles he puts on switched the effect of the player’s screen
  (`CPed::AddGogglesModel` indicates the peda to the flags `CPostEffects`): the flags return, and the pedal
  pointer is reset so that the removed points do not turn off theirs;
- his camera (`CWeapon::TakePhotograph`) flashed on this screen, saved the picture to the gallery of this player
  and paid this player for a shot in the frame: the call is missed, he receives pictures in his game;
- Gang work (`CPlayerPed::ProcessGroupBehaviour`): Grove's recruitment of local fighters with his buttons and
  Grove's hatred of the police for his local search. Call missed;
- This player's health: the ambulance he sits in treated the "player" (`CVehicle::SetDriver` gives a bonus of
  `Players[0]`).

The position is pulled by interpolation to the sent state. If the divergence is greater than 5 m or the player
has gone to the ground that is here (search for land `FindGroundZFor3DCoord` below his point), he is
teleported. Under the remote player in this game there may be no ground: while one of the players in the room,
his game unloads the street map, and in the distance the collision is not loaded. Physics would drop it
through the map, and a lift would balance a drop just above the teleport threshold, and it would hang
underground. Therefore, when players have different rooms up to a remote player more than 150 m or there is no
land under his point, he stands exactly in the place of the owner, without speed. This happens, for example,
when the script after the mission for the duration of the show rocking moves the host to 270 m.

On the move of the player, the movement itself turns, and the standing turn puts the sent state. While the
player is aiming, the camera turns him, and it is only in his game: a copy of the other player remained turned
where he looked at the beginning of the aiming, and only raised and lowered the weapon. Now, with a pinched
scope, the copy quickly rotates just like the owner.

The tilt of the player's sight (`CPlayerPedData::m_fLookPitch`, on it `CPedIK::PointGunInDirection` raises and
lowers the weapon) the game writes only in the third-person camera (`CCam::Process_AimWeapon`). In a
first-person sight (sniper rifle, RPG, homing rocket launcher, camera), it remains what it was, and the
replica held the sniper up while the owner aimed down (02.11). For these camera modes, the copy takes a tilt
from the direction of the owner's camera, which already comes in the state.

Indoor lighting, the game includes when the player enters the door (`CEntryExit::TransitionFinished`): an
additional color `CTimeCycle` from the entrance to the room. Save it with the world, so after loading the host
world, the partner received the light of the place where the host was, and after transferring (`/tp`, "move to
yourself", return to your place), the room remained illuminated as a street. Maud puts it himself
(`game::LightArea`): takes the entrance of the room closest to the point and its color of the sky like a door.
At CJ's house, it matched what the game puts in (Color 3, Weather 22).

A remote player behind the wheel shoots from a window (`CVehicle::DoDriveByShootings`). The game only does
this for a machine with player status, and its machine here is a mirror, so the mod invokes this feature in
the context of the remote player with his buttons. The side of shooting with a conventional camera in the car,
the game takes from the flags "look left / right" of the active camera, and at the time of the call they put
its buttons. Permission to shoot from a machine at a remote player slot is activated when it is created
(`SET_PLAYER_CAN_DO_DRIVE_BY`). SMG for such shooting the owner all the way in hand
(`CPed::RemoveWeaponWhenEnteringVehicle`), so the copy driver model Micro SMG, MP5 or Tec-9 remains: it was
shot as a passenger, and the copy fired from an empty hand (#107, disassembled 04.11). The other gun in the
driver's hand is not shown as the owner's.

The sound of the car (`CAEVehicleAudioEntity::Service`) counts the PLAYER1 driver as a player, so the remote
player's car turned on the radio here and picked up the player's motor sound. During this call, the remote
driver becomes a network player (`PED_TYPE_PLAYER_NETWORK`).

Falla counts the shooter's game and sends the event to the victim's owner. The rest of the bullets of a remote
player do not hurt anyone and are only needed for the view.

The remote player has a PLAYER1 pedal type so that the enemies will attack both. But for each pedo of this
type, the game casts a real-time shadow from a zero cell (`CRealTimeShadowManager::GetRealTimeShadow`), and
players took it from each other. The destructor (`CPhysical::~CPhysical` returns shadow to the master) dropped
the game when the world was loaded and when it was released. Therefore, during this call, the remote player
becomes PLAYER2 and receives a free cell, like a regular pedal. In addition, remote players are removed before
the game takes apart the world when booting from the game.

Save (`CPools::SavePedPool`) writes all PLAYER1 type peds that are not sitting in the car, and the remote
player gets into the host save and the world that the partner gets. When downloading, the game makes each such
player CJ 0 pedal with the general player data (clothing, wanted, sight), and assigns the latter to the
player. So the second CJ appeared: he ran at the player's presses and dropped his sight, and after loading his
save, the host was in the clothes of a partner. Now, for the duration of the save, the remote player becomes
`PED_TYPE_PLAYER_NETWORK`. If there are already copies in the save, only the story CJ is downloaded: the one
pointed at by `$PLAYER_ACTOR` (global variable 12 in main.scm, it has already been read to download the
pedals). The rest are created by player 1 peds without their own group (intercept constructor in
`CPools::LoadPedPool`) and are removed immediately after download.

The tag on the map (`FrontEndMenuManager.m_nTargetBlipIndex`) is checked once every half second and when the
sender `G_WAYPOINT` (`Waypoint.cpp`) changes. She draws alien tags with the same icon on top of her radar and
map tags (after `CRadar::DrawBlips`), rather than setting script tags for them: so they do not fall into the
save. Save the same keeps its mark, so the partner after downloading the world of the host removes the tag of
the host, which he got as his own.

The second player on the radar and map is drawn by the arrow in his direction in the color of his tag
(`DrawArrows` in the same intercept `CRadar::DrawBlips`, sprite center radar; `[game] partner_arrow = 0`
returns the point). Its size is `SCREEN_STRETCH_X(5)`: with the size of the CJ (8) arrow, it seemed larger
than it (27.10).

**The path to the radar marker** (`Gps.cpp` in the same part, idea 28.10: "a marker with navigation to go on a
strip"). While the player is in the car, on an ATV, motorcycle or bicycle (driving or a passenger), on the
street and not in the cutscene, the radar shows the path along the roads: a red line to his mark on the map,
and without it to the mark of another player, a yellow one to the goal of the mission (the nearest coordinate
mark without a badge on the radar, not the near one), if there are no marks (for purposes yellow and for the
mark red, 28.10). The target should be more than 30 meters. The path is searched by the game itself through
the nodes for the machines (`CPathFind::DoPathSearch` 0x4515D0, from the node at the machine
`FindNodeClosestToCoors` to the node of the same connected network closest to the mark on the plane: the mark
has no height). The search goes once a second and immediately if the mark has moved or the car has gone out of
the way beyond 40 m. The game only keeps nodes 350m around the player, so the mod orders areas between the
player and the tag (up to 1.5km forward) with the same single request as `LOAD_PATH_NODES_IN_AREA`
(`MakeRequestForNodesToBeLoaded`), and only if the request is not occupied by the mission script. If the path
is not found to the mark (the search for the game throws after 4950 knots), the path is taken to the point at
half, quarter and eighth part of the path. The line is drawn first in interception `CRadar::DrawBlips`, under
icons, quadrangles `CSprite2d::Draw2DPolygon`, and is clipped around the radar circle. If the limit limiter
moved `ThePaths`, there is no way and the log has one warning. Turns off `[game] gps = 0`. The line is in
`coop.ini`, which creates a mod (`WriteDefaultConfig`), and in the old file, the connection via F9 adds it if
it is not there (`SaveConnectSettings`): the player with a mod on the radar was looking for how to remove the
path (07.11). Counter-direction roads and closed bridges start the game search does not take into account.

Above the head of the remote player to the right of the health strip is the icon of his weapon. It is drawn by
the game's sprite code immediately after its interface (intercept `CHud::Draw`): the icons are in the
dictionaries of the textures of weapons models ("model>ICON"), the fist - the sprite interface. To the left of
the strip of his wanted star (level is in the player state): the same "]" sign of font `FONT_GOTHIC` of color
`HUD_COLOUR_GOLD` that the stars of the interface (`CHud::DrawWanted`), only smaller. `CFont::DrawFonts`
prints them immediately after the interface.

While another player is playing a toy on the radio control (Zero mission), his name with stripes and arrow on
the radar are shown above the toy, not above his CJ (`RemoteToy`, `TagAnchor`, `DrawArrows`). The name above
the toy can be seen up to 250 m, above the CJ up to 70 m: the toy is small, and without a mark it was
impossible to understand where the partner flies (31.10).

A script that puts its player at a point (a clothing store, a place in the mission) puts it inside a copy of
the partner, if he was standing there. The peds got stuck in each other (`CEntity::m_bIsStuck`), and the
player’s next scripting task didn’t start: the store waited for animations to exit the fitting room with the
control taken away. As long as players are inside each other, they don't clash
(`CPhysical::m_pEntityIgnoredCollision` in both, the game itself resets it when they've separated).

The jetpack flies on the player's buttons, and the remote player receives them here. Therefore, a flag with a
backpack is enough on foot (`CPedIntelligence::GetTaskJetPack`): the game gives the backpack to its remote
player pedal (`TASK_JETPACK`) or removes it if the backpack is left only here.

What the player does on the spot (barber’s chair, tattoo, food, drink machine, gym, dance, billiards, box in
hand, parachute), he plays a script or attractor, not buttons. The owner's game tells you which animations of
the IFP blocks of these classes the player is playing now (by block names and animations, like NPC mirrors,
`G_PLAYER_ANIM`), and the other plays it on a copy as soon as it is on foot and the block is loaded
(`Anims.cpp`). When the lesson is over, the copy is returned to the counter: the player's task starts walking
again only when the state of movement changes, and the pedal drops the game without animations (see). Machines
and NPCs: Who Owns What

Falling is also transmitted as an occupation. The player is hit by a car, thrown an explosion or impact, he
flew off the motorcycle: in his game there are tasks of falling and lifting (`CTaskComplexFallAndGetUp`,
`CTaskSimpleFall`, `CTaskSimpleGetUp`), and the other player saw only that he was standing again. While this
task is underway, any animation outside the gait set is considered an occupation, except for facial
expressions, hand movements (`ANIMATION_SECONDARY_TASK_ANIM`) and looped partial racks like IDLE_ARMED. The
drops of KO_* and KD_* in the game are marked partial (0x10): this flag means “on top of the rest” and not
“only top of the body”. Animation is also transmitted to the player, which the script has enabled
(`TASK_PLAY_ANIM`, task 401), from any block. A parachute jump loses a free fall from the PED block, and
before that, the copy of another player just fell. With animation comes the flag "holds": the owner it is
controlled by the task, not buttons. While the flag is raised, the copy receives an empty gamepad, the game
does not change her walking posture, does not start her own fall in the air (`CEventInAir::AffectsPed`) and
removes the already begun. The `CTaskSimpleInAir` would then superimpose a FALL_fall over the parachute.

The animations go between games by name rather than number: the block name key (`anim::BlockKey`, FNV-1a on
the lowercase name) and the animation name key, which the game itself stores in
`CAnimBlendHierarchy::m_hashKey` (`CKeyGen::GetUppercaseKey`). Each game’s block and animation numbers are
different: builds like the Essentials Pack add IFP files to `gta3.img` or change `ped.ifp`, and the numbers
are shifted. Until 03.11, the mod sent numbers, and the player with the mod assembly had a copy in the gym and
in the dressing room standing or playing other animations. The receiver finds the block by key
(`anim::BlockByKey`, the table is rebuilt when the number of blocks changes), and the animation in it by key
(`anim::Find`). If there is no block with such a name (the player’s own file, which the other person does not
have), the copy stands as it stood and someone else’s animation does not play. Also by name are animations of
NPC mirrors (`PedSyncState`), objects (`Props`) and billiards (`G_POOL`). Protocol 39.

The copy, which has no body animations left, stands in the anchoring position, the letter T. That's how she
was seen while the owner was playing basketball or going to the gym. Once a second such a copy returns the
rack of its set, and once in the magazine writes what animations were on it.

Animations that the player includes himself (window U, `features/anims/Emotes.cpp`, window
`pages/anims-menu`). The window divides them into emotions, poses, actions, dances, animations together and
their own, each number for the team `/anims <number>` (without a number it opens a window). The CJ plays the
selected team as the mission involves animation to the player: `TASK_PLAY_ANIM` (0x0605), and gestures with
one upper body `TASK_PLAY_ANIM_SECONDARY` (0x0A1A), on the go. This is a 401 task, so another player sees it
on a copy through the same "occupation" as any script animation (`G_PLAYER_ANIM`), with a "hold" flag. The
uninterrupted command (0x0812) is not moded: the CJ would have stood under bullets and cars. Strike, shot,
hit, fall, death, arrest end the animation with the events of the game as missionary. Poses, struts, dances
and seats (locked and with the retention of the last frame, on the whole body) are held until the player
presses the space or F (running or entering the car), like SA-MP animations: this is how the user chose 03.11
("to last long"), before they were knocked down by any WASD press. The game itself does not interrupt such
animation from the keys (`CTaskSimpleRunNamedAnim::ProcessPed`, 0x61BF20, does not read the keyboard), only
mod check stops it. At the beginning of the first such animation for the game, the chat once writes how to
stop it. Short animations still stop walking, jumping, running, getting in the car, shooting, sighting and
squatting. Animations with output (sit on the ground, lie down, tai chi, smoke) first play the output, the
rest of the animation comes to naught (`CLEAR_CHAR_TASKS`). Positions on the ground from the pack `restpose`
have no exit and lifted the CJ jerk in a quarter of a second. Now they have a way out of the game's
animations, the root of which starts as low (0.8-0.95 m): from the ground on the back as from the beach
(`SUNBATHE/Lay_Bac_out`), on the side and curled up as after a fall (`PED/getup`), sitting as from the grass
of the park (`SUNBATHE/ParkSit_M_out`). The exit block is asked along with the animation and only plays if it
is in memory. A key held before the start stops nothing. During the animation, CJ removes the weapon
(`SET_CURRENT_CHAR_WEAPON` 0) and takes it back if it is still in his hands. The animation does not turn on in
the car, in the water, in the air, squatting, in the cutscene, in the scene with the game’s camera (wide
screen), while the player has taken control (`CPad::DisablePlayerControls`: stores, telephone, mission
scenes), in the mini-game and competition, while CJ is busy with his task (falls, gets up, climbs, gets into
the car, flies on a backpack, carries a box) and while he sits in a toy or at a turrel. If any of this
happened during the animation, the mod throws it and the weapon does not touch: the CJ is now in the script or
in the game.

In the window, only what is used on RP-servers SA-MP: sit down, lie down, smoke, eat, phone, gestures,
adjuster (user choice 04.11, all 1678 animations of the game were superfluous: among them there is a lot that
looks bad on CJ). Sample: Samp-Rp server sections `/anim` and Advance RP window lines 88. The server does not
name the animations of the game, so its rows are matched in order of the official script `gl_actions` and by
the movement in the game files. What CJ does in the game, the list is not: strikes, battle racks, racks with
weapons, reloads, throws, warm-ups. It’s about 170 lines in `BuildList`, each with its own beginning and end,
if any, in the game. Lying down and after the falls, CJ stands up with animations of the game (`getup`,
`getup_front`). All names checked on `anim/ped.ifp` and `anim/anim.img`. PageUp and PageDown, Home and End are
listed, the typed letter translates to the pack on this letter. The wheel moves the list by three lines, and
the lines smoothly reach the place: `g_view` catches up with `g_scroll`, and `Renderer::Clip` crops them at
the edge of the list. Animation is checked in the downloaded library (`anim::Find` on key
`CKeyGen::GetUppercaseKey`): IFP mode instead of gaming may not have it. Libraries that the mod himself asked
for from the streaming (`NeedBlock`, the GAME_REQUIRED flag), are released (`CStreaming::SetModelIsDeletable`,
0x409C10, removes only this flag, the flag of the MISSION_REQUIRED script remains) when none of them plays
(`CAnimBlock::RefCnt`), they are not used by either CJ animation or preview (a copy in the preview plays
without reference to the block) and with the last need 3 seconds have passed. Otherwise, all 11 MB `anim.img`
would remain in memory after scrolling through the preview.

The U key opens the window and still reaches the game, and the window takes the keyboard only from its first
frame. So the code that starts with U (UZUMYMW, UBHYZHQ, URKQSRK) is typed further: its second letter in the
first 0.6 seconds closes the window and also goes into the game. If U is inside the typed code (BAGUVIX,
BUFFMEUP), the window does not open: the check is the same as the chat with T, on a list of 50 codes with U
(letter codes and phrases, checked with hashes `CCheat::m_aCheatHashKeys`). Next to the competition, U still
calls for competition.

The animation begins with an invitation (`G_EMOTE`: 1 go, 2 no, 3 start, 4 stop). The second player answers Y
or N as if on /tp. The answer “yes” is placed opposite the inviter, facing him, at the distance at which they
are put by the scripts of the game: handshake HNDSHKFA in a meter (main.scm 0xF302B, Sweet 0x113236), business
0.92 m (0x15CF78), hand on the shoulder 0.9 m (scene with Sweet 0x93776), dispute 1.2 m (`TASK_GOTO_CHAR`
0x2104E7). Place checked: ground at the same height and direct chest-to-chest visibility
(`CWorld::GetIsLineOfSightClear`), otherwise the answer is no and the reason is in the chat. The inviter
starts when he gets the "beginnings," responding to half the ping later, so both pieces go together. If one
stops or is knocked down, the second comes “stop”. A kiss and a slap on the back, the user decided not to add
(03.11).

The preview in the window (`AnimPreview.cpp`) is a copy of the body of our CJ, and in the animations, both are
also a copy of the body of a partner. The copy is made in the same way as the game makes the body of the peda
(`CClumpModelInfo::CreateInstance`: `RpClumpClone`, its own hierarchy of bones from clone frames, skin atoms
translated to it, `RpAnimBlendClumpInit`). The animation on it moves the game's animated code, and draws its
own RenderWare camera into texture after the main frame camera (call `RwCameraEndUpdate` of `Idle`, 0x53EBF5,
after the Core hook), as the game draws the shadow of the peda (`CShadowCamera`). There is no physics, no
tasks, no other player can see it. The side of the texture is the degree of deuce about the double size of the
preview on the screen (512, 1024 or 2048): the window shows it reduced, and the edges of the figure come out
smoothed. The animation copy plays the same way as the script (`TASK_PLAY_ANIM`) on the pede: a partial
overhead of the rack `IDLE_stance`. In emotion animations, 26 bones, in the rack all 32: jaw (8), eyebrows (6,
7), stomach (201) and “chest” are taken from the rack. When the whole body animation was playing and the rack
was gone, there was no source left for these bones, the turn was going out of an empty amount, and the mouth
with eyebrows moved (03.11). In order to prevent the root from sagging, the animation data of the copy
indicate a “pedal shift” like that of the real pede and the cutscene object (`m_PedPosition`,
`CPed::SetModelIndex`): then the root goes through the extraction of speed (0x4D1680), like that of the pede.
Without this, the root of the body without the pedal takes place from the sum of the weights of the stand and
animation (`FrameUpdateCallBackSkinned` 0x4D2B90), and the sitting CJ goes twice deeper. After updating the
dice, the preview calls `CPed::ShoulderBoneRotation` (0x5DF560), as a game of `CPed::PreRender` for the player
model and in the cutscenes for CSPLAY: CJ skin has bones between the collarbone and the shoulder ("chest", 301
and 302) that no animation moves. Without this challenge, the shoulders remained in the position in which the
skin is made (hands to the sides), and protruded with corners (03.11). The challenge is only if the skeleton
has bones 21, 22, 31, 32, 301 and 302: otherwise, the game code writes before the array of matrixes begins.
The camera holds the whole body: the bones are projected on the axis of the picture, the frame immediately
grows and slowly shrinks (in 2 seconds), so the loop does not “breathe”, and the lying CJ camera approaches
when he lies down. Turning the mouse starts the frame again. Under the bodies floor: light spot and shadow of
each (`RwIm3D`, fan triangles at a height of -1 m from the middle of the pedal, this is the bottom of its
collision frame). Light is own: filling, warm from above on the camera side (turned `pDirect`) and cold from
behind for the contour (extra directional, `AddAnExtraDirectionalLight`). After drawing, the light of the
frame and all the touched states of the drawing return. The frame of the game ends with a flat shading
(`rwSHADEMODEFLAT`, seen in the log 03.11): with it, the CJ came out in faces with dark triangles on jeans,
and the floor was not visible (the fan is painted with the color of the top of the rim with zero
transparency). Therefore, the preview shading is smooth (`rwSHADEMODEGOURAUD`). The depth buffer for the
RenderWare camera creates with the screen smoothing type (`_rwD3D9CreateZBufferRaster` 0x4CCE48 takes
`MultiSampleType` from `g_D3Dpp`, 0xC9C050), and the texture does not smooth. According to the rules of D3D9,
they must match, so at the time of creating our buffer, the screen smoothing is zeroed. After resetting the
device, RenderWare recreates the rasters with screen smoothing, then the preview camera is created anew (Deep
buffer check `GetDesc`). On the stand with Intel UHD and MSAA 2x preview was drawn without this: the driver
forgives the discrepancy, with other drivers not checked.

Their animations are .ifp files from folder `coop\anims` (up to 32 files, up to 4 MB each). The game reads
them once the player appears, until the first cutscene: the cutscene on exit removes the last block
(`RemoveLastAnimFile`) and it must be her. The game believes every number in the file, so first the mod reads
the file itself (`Ifp.h`: only ANP3 and ANP2, counters and dimensions within the file, the frame of compressed
and uncompressed keys, the key time does not go backwards, otherwise the animation cycle does not end). The
block is called `C_` and the file name is capitalized in Latin, since it is the same for both players at any
file name register. Place in the animation table (`ms_aAnimations`, 2500) at its end, with 2150: blocks of
games get places in order when they are first loaded (total 1871, our COOPCS 36, cutscene to 18), and the
block loaded after its own would go to the end of the table. The mod registers the block
(`RegisterAnimBlock`), gives it the first place and number of animations, and the game downloader leaves the
known block its place (0x4D520A). The table counter returns back after loading. If the blocks of the game
still reached 2150 (a game with many animation mods), your animations are turned off. Another player
recognizes his animation not by block number, but by name (`G_PLAYER_ANIM`, operation 5): the numbers of his
blocks in different games are different. If he doesn't have a file like that, the copy is worth it.

Packs are alien animations that the mod brings itself (`package/coop/packs`, in the game `coop\packs`), the
Paki tab with a section on the pack (the title and author above its lines), numbers with 601. Almost all packs
on sites are put instead of the game file (`crack.ifp`, `ghands.ifp`, `ped.ifp`), new in them only animations
that differ from the game. Script `anim_pack.py` (in the test folder, `scripts`) finds such records (`diff`
with the game file), transfers their bytes to the pack file (the ANP3 record itself: name, sequences, frames)
and gives them their names (`Graf01`, `Fortnite01`). Pack loads in the same way as its files, but before them
and with the prefix `P_` (`P_FORTNITE`): blocks of the game remain their own, scripts missions and cutscenes
see the old `GHANDS` and `CRACK`. The pack places at the end of the table are shared with their files (from
2150 to 2500), and when the table is overflowed with the game, they are turned off along with them. Animations
that have a root of meters (flips, dances that move on the floor) come with a flag of `MOVES`:
`TASK_PLAY_ANIM` with locks X and Y, CJ leaves with the animation and stays where it ended. Without locks, the
body would go forward and come back at the end. Sources of packs and authors are listed in README and
instructions. Fortnite v1 is not taken: its script includes the dances of the game itself. Repeats are thrown
out (Acrostreet `onehandlayout` equals `onehandflash`, six Military Greeting animations are the same,
`RHGsign2` from Fortnite coincides with the "Break 2" Arch in the length and stroke of the root and left at
Arch). In two animations (GTA V welcome, rap) the keys started with −0.05 and −0.3 s, our verification of such
a file does not allow: `anim_pack.py` shifts all the keys of the record so that the first one was in zero.
Unit test `test_ifp.cpp` checks and packs (files after the folder of the game). By the user’s decision, 39
animations from Assassin’s Creed and a conversation from GTA LCS were not taken. The preview turns the camera
to the face of the body taken along the thigh line 0.4 seconds after selecting animations: animations made for
other locations in the game unfold the body, and pack dances stood with their backs.

The gait set of a thick or inflated CJ lies in its IFP block, and the game only remembers its player's block
(`CClothes::RequestMotionGroupAnims`), so the fat partner walked with the other player as a regular CJ. Now a
block of copies is requested by its statistics once a second. The body assembly of the copy
(`ConstructPedModel` under `MuscleFrom`) requested a block over the muscles of a copy and released its
player's block. Streaming would unload it while the player was standing, and the first step would read an
empty set of gait: so the game would fall in `CTaskSimplePlayerOnFoot::PlayerControlZelda` (0x68861B). After
each such build, the player's block is requested again. Assembling without a dictionary of textures of the
fashion model misses: on the balaclava of Home Invasion, the game fell to `RwTexDictionaryForAllTextures`
(0x7F3737).

The style of combat from the gym (`CPed::m_nFightingStyle` + 0x72D and techniques + 0x72E) goes in the
description of the appearance, and the copy fights in the same way as the owner. In the past, boxing and kung
fu from another player looked like normal punches. The receptions are now stored by the partner's profile
(version 4).

In one place of the script (a dashboard, a hairdresser's chair, a simulator, a fitting room, a casino table),
each game puts its player, and the two bodies stood in each other. A replica that stands inside its player for
more than a second now hides with the name overhead until they disperse. In the cutscene, the partner shows
`ScenePartner`, and there's no copy hiding.

The dressing room door is an object with animation (`CJ_BINCO_DOOR` and five more models 2873-2879 from
props2.ide). It is created and downloaded (`PLAY_OBJECT_ANIM` blocks `CLOTHES`) by the script of the store
`CLOTH` of each game, and only for its player. A copy of another player entered the booth through a closed
door (28.10). Now that the copy has an entry or exit animation (`CLO_In`, `CLO_Pose_Out` – door
`CLO_Pose_Out_O`, `CLO_Out`, `CLO_Pose_In` – `CLO_Pose_In_O`, in the same pairs as in the script), the game
rocks its door 4 m from the copy at the same time (`SwingBoothDoor` in `Anims.cpp`). If the second script does
not have a store (came through /tp), there is no door and there is nothing to download.

The other player's motorcycle is a mirror, and the owner's fortunes pushed him into curbs and cars.
`CBike::VehicleDamage` drops riders (`DamageKnockOffRider`) before checking collision protection, so our
player flew from the back seat, although the driver was driving further. While our player is sitting on the
back of such a motorcycle, you cannot drop it (`CPed` + 0x474, bits 27-28, like
`SET_CHAR_CAN_BE_KNOCKED_OFF_BIKE`) and then it gets the same value. The driver's fall is decided by his game:
if his copy is here on foot without an exit event, our player gets off too. It's also protected by a copy on a
motorcycle that's not run by us. On our motorcycle, it can be dropped, otherwise our driver would not fall:
the game does not reset anyone, if one of the two can not be dropped.

The model of the player, which was replaced by the mod with skins (`SET_PLAYER_MODEL`, 0x47A820), goes in the
description of the appearance (0 - CJ in his clothes). The other game changes the copy model in the same way
that this command does: removes the RenderWare object, puts the model anew, and saves the gait set. Special
characters (290–299) are loaded under a name that the other game does not know, so the player remains a CJ.

A copy of another player dies by his animation. His game reports what fall from the default set he fell
(`G_PLAYER_DEATH`, Intercept Designer `CTaskComplexDie`). The death flag arrives earlier, so the copy waits
for this animation up to 300ms. Previously, any death of a player from another looked like one fall to the
back.

A partner who is killed or arrested during a host mission no further than 300m from the host returns to the
right of the host, not at the hospital or site at the other end of the map: mission enemies go to any PLAYER1,
and the mission continues without it.

If the host during the mission sat in a transport where the partner does not have a place (motorcycle, loader,
"Picador" Ryder, full "Greenwood" Sweet), the partner immediately sits behind the wheel of the same model of
transport (`WARP_CHAR_INTO_CAR`) and receives a message in the chat. Previously, the transport only appeared
nearby, and while the partner sat down, the host left (04.11). As long as the partner is held by another,
there is no transport, and the attempt is repeated once per second: a script or scene
(`DisablePlayerControls`, wide screen, mini-game), a turret (`AttachedTo`), a pack, landing in another car,
death. You can exit the issued machine, the second one will not come to the same host machine (`m_bikeFor`).

In missions Zero and Toreno (scenarios `zero*`, `toreno*`, `des*` and `maf4`, the eighth mission of the desert
with a backpack, 0.3.3) also issued a plane or helicopter if the host is at the helm and there is no space for
a partner, and a jetpack when the host put it on (`TASK_JETPACK`, model 370 loads in the background). A
backpack is issued once for each person who wears the host. Radio controlled toys Zero (RC Baron, Raider,
Goblin, Tiger, Cam, Bandit) do not count: they do not fly.

In the missions of Zero, the host does not fly (0.3.3, decision 31.10, `features/story/rc-toys`). In Air Raid
(`ZERO1`) he sits on a turret (`ATTACH_CHAR_TO_OBJECT`), in Superly Lines (`ZERO2`) he leads the RC Baron, in
New Model Army (`ZERO4`) he sits on a turret over a table in Area 10. While the turret or toy of the host is
working, his game sends to the partners `G_RC`, and the partner's game gives him his own: in Air Raid, a
machine gun next to the host turret, in the rest of his toy with the same team as the mission
(`GIVE_REMOTE_CONTROLLED_MODEL_TO_PLAYER`): RC Baron on the roof of Zero or RC Goblin 6 m from the host toy on
the table. `CRemote::GivePlayerRemoteControlledCar` (0x45AB10) puts the toy on the ground under a given point,
records it in `CPlayerInfo::m_pRemoteVehicle` (+0xB0) and disables the machine in which the CJ sits on the
mission with a bicycle: therefore CJ. A pedestrian CJ would walk from the same sticks and shoot from the same
button, so the partner for this time is frozen and without a weapon in his hands. The world is loaded around
the toy (`CStreaming::Update`), and collisions and interiors around the player's area: for the table, his CJ
moves to Area 10 where it stands, and back he is released when there is again a floor of the street under it.
The car boarding key returns the partner his CJ, and until the end of this part of the host's mission, he is
not given anything else. Instead of hitting a toy in 8 seconds give a new one. Toy machine gun
(`CWeapon::FireOneInstantHitRound`, 0x73AF00) hits cars without the author of the shot and with the number of
the weapon Micro SMG: as long as the player has a toy, `Damage` considers such a hit on a copy of it as a shot
and sends it to the owner. Games communicate the network number of their toys (`G_RC` 2) to each other, and
without fire, they do not injure it.

Toy explosion solves the flag `CVehicle::bDisableRemoteDetonation` (0xC1CC00), it puts
`SET_ENABLE_RC_DETONATE`. The `STATUS_REMOTE_CONTROLLED` machine explodes from the fire button
(`CarGunJustDown`) if the flag is removed and the RC Baron aircraft fire is the same button. Zero missions are
shutting down the explosion. The mod turned it on, believing that the toy was being blown up by a landing key,
and that the partner's plane in "Supply Lines" was exploding instead of being shot (31.10). Now the explosion
is off, like in missions.

The machine gun in the Air Raid mod does the same as the mission makes it a host. The host turret is an
invisible collision-free object on the roof of Zero, to which the CJ is attached with a minigan
(`ATTACH_CHAR_TO_OBJECT` with a displacement of -0.25 -0.5 1.25, a full circle and a weapon of 38, then
`SET_HEADING_FOR_ATTACHED_PLAYER 270 300` and `SET_CHAR_PROOFS`). The host sends the place and turn of its
object (`G_RC` 1 with minigan model 362). The game of the partner looks for a place 1.8-3.5 m away from him,
where the roof floor is at the same height and there are no obstacles, puts his object there and attaches the
CJ to it. The attachment gives a minigan (`CPed::AttachPedToEntity`), and the detachment returns the former
weapon to the hand. The minigan occupies a slot of heavy weapons: what lay in this slot, the mod remembers and
then gives. While his partner's at the machine gun, he's invincible, and then he goes back to where he was.
Previously, the partner received here the RC Baron, indistinguishable from Berkeley aircraft, which is shot
down by the host (31.10).

While the partner watches the host scene on the engine (`SceneWatched`), the toy is not given to him: the
scene takes the camera and at the end gives it to the CJ, not the toy. The toy issued at the beginning of
"Supply Lines" flew out of frame, and on the screen stood a frozen CJ (31.10).
`CRemote::GivePlayerRemoteControlledCar` points the camera through `CCamera::TakeControl` (camera target at
`TheCamera` + 0x958, mode `MODE_BEHINDCAR`). If the camera's target is not a toy and there is no scene or
cutscene, the mod points the camera at the toy again.

The game places the toy on the ground under a predetermined point (`CWorld::FindGroundZFor3DCoord`) and
collisions are loaded around the CJ. There was no table in area 10 for the CJ partner: the helicopter fell
under him into the void, and the screen remained green (31.10). Therefore, for the CJ table, the partner first
moves to area 10 and waits next to the place where the mission holds the CJ host (on a bicycle in the air at
-1004, 1001), and the camera, meanwhile, looks at the table from the point of one of the planes itself
`ZERO4`. The toy waits for the floor under its seat for up to 8 seconds (`ToyPlace`). The place is searched by
rings 4-8 m from the host toy: the floor is not higher than its point and not lower by 1.5 m on the roof or 7
m at the table (the table lies 5 m below the host helicopter), and a meter around empty. The first check was
waiting for the floor 1.5 m from the height of the host helicopter and the table itself was not counted by the
floor: the partner hung in the void for 8 seconds over and over again (31.10, fourth check). If the floor
never showed up, the toy is no longer given in this part of the mission. After the toy, CJ returns to the
street where he stood.

A partner further than 300 m from the Zero store, fashion once for part of the mission carries to its door
(`ComeOver`), as a transfer behind the host (`CS_FOLLOW`) would do: not from his part-time job, shop or after
death, and the car he drives is carried along with him. While the host's CJ sits in frozen transport,
`CS_FOLLOW` waits until the end of the mission, and the partner next to whom the host dialed /mission zero2
2.8 km from the store was left without an aircraft (31.10). There is no transfer for a machine gun in the Air
Raid: the usual `CS_FOLLOW` leads a partner there, because the host on the turret can move. The /mission
itself launches the mission where the host is: from Los Santos, `ZERO2` put the host plane on the roof, which
had not yet loaded, and it was underground. In the game, the mission begins in the store, and this does not
happen. In the /mission test build for missions, Zero first transfers the host to the store door if it is
further than 50 m (01.11: 4.4 km from the store, the host is again under textures).

The mod partner's toy is removed by himself (`DropToy` ). `REMOVE_RC_BUGGY` only returns the CJ to the player,
and the car remains in the world: the partner's RC plane fell without a pilot and exploded next to him (01.11,
"Supply Lines"). Now, after this command, the toy is removed by `DELETE_CAR`. The table toy in the New Model
Army appeared in the zero area and in the hall (area 10) was not visible: the partner was flying an invisible
helicopter (01.11). Now she gets the area of her seat (`SET_VEHICLE_AREA_VISIBLE`). After the toy at the
table, the partner goes to the door of the Zero store, rather than to his former place in the city from which
he was taken.

The helicopter partner on the table magnet, like the host (player selection 02.11, `Objects.cpp`). The host
magnet works via script `ZERO4`: a point in a meter under the helicopter (`GET_OFFSET_FROM_CAR_IN_WORLD_COORDS
0,0,-1`), an object within a meter of it (`LOCATE_OBJECT_3D`), a fire button (button 17) and
`ATTACH_OBJECT_TO_CAR`. The partner has a similar `MINI_MAGNET` under the helicopter, and the fire button also
picks up a copy of the barrel (`BARREL2`) or board (`KMB_PLANK`) and drops it. The game of the host on
`G_OBJ_HOLD` (`MAGNET`) clings the real object to its copy of the partner's helicopter, and when reset
(`RELEASE`) unhooks it where the copy was hanging. This is how the partner removes obstacles from the path of
the bandit. An object that is already hanging on a magnet (host or Berkeley) can not be taken, and if someone
else's magnet took it, the partner lets it go. The bomb (`RCBOMB`) remains the domain of the host: the script
only explodes the one that dropped its magnet. The host sees the magnet under the partner's helicopter.

The game remembers through which door the pediatrician entered the interior (`CPed::m_pEnex` + 0x78C) and
displays it outside. The player, transferred to the interior to another (`/tp`, saved seat, transfer to the
host), was there without a door. Now he receives the door of a copy of the person to whom he was transferred,
and if the copy does not have it, the door on the street where the player was standing (`StreetDoorOf`). A
copy of the other player also receives the door through which he entered.

After processing the copy, its rotation is aligned with the owner when he is aiming or when they have diverged
by more than 45 degrees. With a gunshot, the game turned the copy to where the owner’s camera was aiming from
the point where the copy itself stood, that is, up to 7 degrees to the side. Stick in the squat, the game
spreads along the axes of the character himself, and the sideways sideways turned the copy into a step
forward. A 180° turn could have gone the other way. The squat tumbling (`CROUCH_ROLL_L/R`) goes as a lesson,
and the squatted copy after the lesson returns to the squat rack, not to the usual rack: before it was 0.4
seconds up. Reloading (`ANIM_ID_RELOAD`, `CROUCHRELOAD` groups of weapons) is transmitted by animation: the
copy does not run out of cartridges, and it itself was not reloaded.

When the player is led by a task rather than a stick (passing to the store door `CTaskComplexGotoDoorAndOpen`,
walking on the script command: tasks 900-939), the owner places a flag `F_TASK_WALK`. The copy gets a stick
that leads it in the same turn and at the same speed, and the push of the door (`Walk_DoorPartial`) goes like
an occupation that does not hold it: when it ends, the copy goes on, and does not stand up and move again.
Previously, the copy slid standing and spinning jule: its stick relatively fixed camera at the door showed
where it went. The flag stands all the way through the door (`CEntryExitManager::ms_exitEnterState`), not just
while the door task is on. As the screen darkens, the player walks in under the empty task
`CTaskSimpleUninterruptable`, and the drop has already been turned on again: the copy ran along his stick and
the door camera to the side, or even back to the street. The owner transmits the transition sign itself
(`F2_DOOR`) and the copy, having unfolded more than 115°, disappears before being transferred inside. While
the copy itself gets into the car (`EnteringCar`: the task of landing by the driver or passenger), this stick
is not given to it. To the door, the owner goes by the task, the flag stands, and the stick argues with the
task of landing the copy: she walked on the spot until the synchronization after a few seconds put her in the
car (27.10).

The host and the partners stand side by side according to the same rule. `PartnerRank` gives the partners the
same order in all games: the first, second, third in the player's number, the host out of count. `PartnerSide`
places the first on the right of the person to whom he is led, the second on the left, the third behind. This
is how transfer after host (`CS_FOLLOW`), return after death next to the host, appearance after downloading
the world, entry into someone else's part-time job, places in the lowrider competition and in dance. Turtles
and Zero toys look for space from different points of the circle around the host, the second arrow and the
third sit next to the first with a shift of 0.8 m. The starting point of the mission is wider, the more
players there are: on one spot, four will not stand up.

Moving `/tp` and `/tphere` with three and four players goes by the player number (`/tp 2`): nicknames are
uncomfortable to dial. The numbers show window F2 and `/players`, only a nickname remains above the heads, and
the team without a number writes a list to the chat room. Nick's a good fit, too. There can be several
requests at once: one for each player (`g_asked`), another is in line, and Y or N responds to the first
(`g_asking`). With two players, the number is not needed and is not shown anywhere. The same commands in the
F2 window go with arrow buttons.

If there are three or four players, the owner's game announces what is next to any other player, and the
message goes to everyone. A player away from this place would get copies of other people's streets, which he
does not have with two players, and they would occupy pools. `FarFromUs` keeps such machines in `m_pending`,
and pedes without a pede in the record, as long as they are further than the same distances as the owner (170
and 120 m for appearance, 220 and 150 m for departure). The states then update the record. Script machines and
machines in which the player sits are always displayed: they are supported by mission tags, arrows and the
players themselves. The sight and damage of the returned car comes with the owner's next mailing, it goes once
every 10 seconds.

When the player leaves, his machines and people are left with one game (`WeInherit`): the host, and if the
host is gone, the first one is the number. She announces them again, the others delete their copies.
Otherwise, each game would make them their own and show others on top of their own. The car you’re sitting in
doesn’t remove the game. The population of two partners away from the host creates the first of them
(`UpdateSuppression`): without this, there would be people on the same street of both games.

## Vehicles: driving, riding, and stealing

In cars, players are equal.

You can get behind the wheel in any parked car, and a car with a driver-NPC can be stolen: the driver is
pulled out by regular animation. This also works with machines that are simulated by another player. For the
owner of the machine, the remote player performs the same landing task (`CTaskComplexEnterCarAsDriver`) and
the NPC that is thrown away remains with the owner. The player takes the car to the first frame behind the
wheel. Previously, this was waiting for a state to be sent, which does not go more often than once in 33 ms,
and the next update of the copy managed to return it to the owner’s deadened motor: the car did not go until
you got into it again (issue #43). When an NPC pulls out a player of this game or a copy, the owner of the
pedal is already standing outside, and here his mirror is still pulling out of the machine landing task.
Previously, the mirror was immediately placed next to the machine and stood up without animation, as if from a
communication delay (27.10). Now, while the machine is being boarded (`m_nNumGettingIn`, +0x485, or our
player's driver-landing task 701/713, or a copy of this machine), the mirror waits for the animation to end,
but no longer than 6 seconds (`kCarTaskGraceMs`).

The key `G` (which can be changed) is placed on the nearest available passenger seat. In single-player, there
is no such possibility, and the mod adds it. You can sit with a partner, to the NPC or in an empty car. There
is no free button on the gamepad, so the passenger sits down, holding the entry button (triangle) for half a
second: the started landing behind the wheel is removed, and the player goes to the passenger seat
(`Players::HoldForPassengerSeat`). The same retention applies to the input key on the keyboard. While the
player is aiming or holding the target, `G` recruits Grove fighters, and before CJ instead went to the car
next to him. At this time, the passenger key does not work.

The mode location is selected at the nearest door (`NearestFreeSeat`): for each free space, the game calls its
door (`CCarEnterExit::ComputeTargetDoorToEnterAsPassenger`) and the point where it is opened
(`GetPositionToOpenCarDoor`), and takes the place whose point is closer to the player. Previously, the first
free seat, i.e. the right front, was taken, and the CJ walked the car past the free rear door (user request,
03.11). Places without a door (by the bus, the fourth place of the van) are last. It also selects the place
and the entry key of the car, which is driven by another player.

From the water to Skimmer, Vortex, Sea Sparrow and Leviathan, the game sails to the nearest of the two front
doors, whatever seat is needed (`CTaskComplexEnterCar::CreateFirstSubTask` and on road
`CTaskComplexGoToCarDoorAndStandStill`, both through `CCarEnterExit::GetNearestCarDoor`). The passenger swims
to the pilot’s door and takes out a copy of the partner, and the pilot to the passenger’s door, where the
partner sat, and no one sat down. Both of them landed from the pier (Players Complaint, 08.11). Now the player
and the player's copy in the water, if the seat at the selected door is occupied, and the other front door is
free, the answer will be a free door (intercept `GetNearestCarDoor`, `Players.cpp`). On land, at the NPC, and
when both seats are free, it's like a game.

Passenger shoots out the window. Such shooting in the game is only in the split-screen mode
(`CTaskSimpleCarDrive::ProcessPed`, 0x6449AB): the player in the passenger seat sticks out with a weapon
(`CTaskSimpleGangDriveBy`). Maud replaces the call `CGameLogic::IsCoopGameGoingOn` in this branch with his
check: the session is going, the right mouse button is clamped (in the car it is `m_bVehicleMouseLook`) or R1
on the gamepad, the camera is not yet in sight mode from the car, in the hands of a firearm of instant hit
with cartridges. Checks slot submachine guns and camera mode for two after the call removed: the task takes
any weapon instant hit. At the end of the landing, the game puts an Uzi, Tec-9, or MP5 in hand, if any
(`CPed::RemoveWeaponWhenEnteringVehicle`, 0x5E6370). The passenger, who sat down with another firearm of
instant hit (M4, shotgun, pistol), mod leaves it (#83): at the time of the call shoots
`CPlayerInfo::m_bCanDoDriveBy`, and the game removes the model of the weapon, as in a car without shooting,
and the tilt from the window returns it to the hand. The driver, the passenger with fists, cold weapons or
submachine gun receive their weapons, as before. Once leaning out, the passenger switches the weapon with a
mouse wheel or a gamepad cross up and down (`Players::PassengerWeaponWheel`): in the car, both put `DPadUp` /
`DPadDown` for the radio. In a circle there is an instant hit weapon with ammunition, the selected one remains
in the hands after the exit (`m_nSavedWeapon` = 55). The radio at this time is not heard by the wheel: two of
its calls `CPad::NextStationJustUp` / `LastStationJustUp` (0x4EB72A, 0x4EB753) are redirected. The window
firing task holds the weapon data from the start (`m_pWeaponInfo`, +0x30), so the mod resets them every step
of the way, and the task takes them from the weapon in hand. Without a button, the firing task that the mod
began ends: intercepting the `CTaskSimpleGangDriveBy::ProcessPed` puts it in the completion flag (+8), and the
firing the mission gave does not touch. The player who is in focus is processed, so a copy of the partner
sticks out under his buttons. His game adds a mouse button to the R1 state, which sends while he is a
passenger (`FillCommon`). The copy weapon in the mod machine puts himself, `CPed::SetCurrentWeapon`
(0x5E6280): he immediately changes the slot and model. `SET_CURRENT_CHAR_WEAPON` peda player only records the
choice (`m_nChosenWeapon`), and uses it `CPlayerPed::ProcessWeaponSwitch` (0x60D850) only on foot. A replica
of the passenger who had changed his weapon with the wheel remained with the ouzi the game had given her on
landing, and the second player saw a submachine gun in her hand (27.10). Until the owner sticks out (no R1),
the gun model is removed from the copy (`CPed::RemoveWeaponModel`, 0x5E3990), like that of a seated passenger.

The driver is also protruding (0.3.3, player selection 01.11). In the game, this is the driver's branch in
`CTaskSimpleCarDrive::ProcessPed` under the "aiming behind the wheel" cheat (OUIQDMW,
`CHEAT_WEAPON_AIMING_WHILE_DRIVING`): with the cheat included, the player at the wheel sticks out with a
submachine gun from the slot 4. The mod replaces reading the cheat flag (0x6446B6) with calling its check
(`DriverDriveBy` in `Players.cpp`, via an insert that saves the ecx and edx) and removes the check of the slot
4 after it. With the cheat turned on, the check repeats the game. Without a cheat in the session, the driver
sticks out while holding the right mouse button (`m_bVehicleMouseLook`; R1 the driver has a handhold, so there
is no such thing on the gamepad), with any firearm instantly hitting in the hand and until the mission has
banned firing from the car (`m_bCanDoDriveBy`). Released the button, and the task ends with its 7 command,
like a passenger. A copy of the driver is not able to receive such a button: it sticks out while the camera of
its owner is in sight mode from the car (`MODE_AIMWEAPON_FROMCAR`, it is set by the shooting task itself for
the player, `CTaskSimpleGangDriveBy::SetupStaticAnimForPlayer`). Repeat the task while it is running
(`CTaskManager::FindActiveTaskByType`, problem 1022).

The camera of the protruding driver was pulled towards the center during shooting (01.11, again 02.11). The
sight from the car (`CCam::Process_AimWeapon`) at the driver is a timer of inactivity (0xB6EC48, ms). When it
has passed 2 seconds and the sight is closer to 30° to the nose of the car, or has passed 5 seconds, the
camera smoothly returns to the nose. The timer resets only the mode stick for two and the shot until the timer
is less than 2s. The mouse does not touch him, and while the driver held the mouse in place and shot, the
camera was taken to the center. While the player driver is leaning out, the mod keeps the timer at zero.
Turning the pedal here does not give anything: each frame gives the driver a turn of the car
(`CPed::SetPedPositionInCar`).

The passenger also fires rocket launchers (#94, only the passenger of the player's choice). The task of firing
from the window immediately ends for the weapon is not an instant hit (0x62D3DF): this check is replaced by an
insert that passes the RPG and homing rocket launcher at the passenger-player (`DriveByWeaponOk`).
`CWeapon::FireProjectile` shoots the player's missile only from the aiming camera (`MODE_ROCKETLAUNCHER` and
others from the first person), and once it sticks out in sight mode from the car. At the time of the shot
(call `CWeapon::Fire` of `CTaskSimpleGangDriveBy::FireGun`, 0x628328), the mod puts the camera
`MODE_ROCKETLAUNCHER` and the rocket flies where the camera is looking. There's no target capture, homing goes
straight. A copy of the passenger does not fire a rocket: it is sent by the owner's game (`Fire`). The round
sight of the protruding player `CHud::DrawCrossHairs` draws only for weapons 22-33, 37 and 38, and the rocket
launchers had no sight (01.11). Insert on the 0x58E1C7 adds 35 and 36 in sight mode from the car.

The car is owned by its driver. As soon as the player gets behind the wheel, his game begins to send out the
physics of the machine: position, quaternion, speed, steering wheel, gas and brake, damage, siren and horn.
The mirror has the status of an AI-controlled machine, so its AI steering (`CCarCtrl::SteerAICarWithPhysics`)
is turned off: without a task, it stung the brake and the handheld, and the mirror rode in jerks. The mirror
displays the damage to the owner in the way that the save load builds them: repairs the machine
(`CAutomobile::Fix`) and puts the parts in condition (`SetupDamageAfterLoad`). This assembly re-discards every
missing part (`SpawnFlyingComponent`) and with each new dent in the owner’s dent, the mirror again dropped the
already lost hood and bumpers: one car had a pile of them (02.11). The assembly itself rewrites part of the
state (door that can not be damaged, `SetDoorDamage`), and the mirror, which checked the living state with the
sent, was assembled twice per second, dropping parts each time. Now the part lost from the last assembly falls
once, the rest are simply hidden (at the time of assembly, the counter of temporary objects
`CObject::nNoTempObjects` costs 150, and the game does not create debris), and the reassembly takes place only
in a new state from the owner. In the back seat of Wayfarer (586), players saw a distorted face: jaw down,
eyebrows sideways (02.11). This is a game file error: `WF_passenger` is the only one of the passenger
animations without facial bones (26 bones vs. 32), and these bones do not have a single node with a turn.
`HoldWayfarerFace` (Players.cpp) holds on such a passenger `CAR_sitp` of ped.ifp partial animation with a
weight of 1%: the facial bones take rest from it, the rest it hardly moves. The flag of the secondary problem
removes it with the landing (`CCarEnterExit::RemoveCarSitAnim`), and `anim::Main` does not count such
animations. Log `NoteRideAnims` is still there. Handle (`bIsHandbrakeOn`) takes a mirror from the owner
(`F_HANDBRAKE`): left on since the car was in this game, he held its rear wheels, and the car was driving with
burning stop lights and smoke from under the wheels, as if braking (“Cesar Vialpando”). If there is no land
under the mirror point in this game, it stands exactly in the place of the owner. The mirror that our player's
car crashed into (`CPhysical::GetHasCollidedWith`, 0x543540, both ways) moves 0.6 seconds only by the physics
of this game and the next 1.4 seconds returns to the owner's place several times slower than usual.
Previously, every shot of it was pulled to the place of the owner, who did not yet know about the impact, and
the car from the traffic of another game stood as a wall: our car rested on it (27.10). Cars with a player
inside and airplanes with helicopters are not affected. An empty car stays behind the last driver while he's
around. Then the possession goes to the nearest player.

The interior door for the time of dimming (about 2 seconds) puts up to 32 things around area 13 to hide them,
and then returns (`CEntryExitManager::SetAreaCodeForVisibleObjects`, `ms_visibleEntityList`,
`ms_oldAreaCode`). The empty machine at the entrance was where there was no player: cycle `gone` in
`ScanTraffic` slal `G_VEH_REMOVE`, the other game took a copy of itself (`KeepsWhenDeleted`), and after the
door both declared the car again, and there were two of them (Binco, disassembled 04.11). `TrueArea`
(Game.cpp) sees through it: machines and people take the real area. The same pairing occurred when one player
took another player's car (`createdByUs`) and left it while he was in the interior: the owner let it go
because the other player was in another area, and his game announced its copy over the top. Now, such a
machine closer to `kTrafficOut` to the door that another player has left (`rp.streetPos`) remains online
(`ByTheirDoor`). If she's taken further and abandoned, a couple is still possible.

An empty machine, under which the owner has no land in the game (`CColStore::HasCollisionLoaded`, 0x410CE0:
the player has gone far or into the interior, and the collision is loaded only around him), the owner's game
keeps motionless: flag `FREEZE_CAR_POSITION` (`CPhysical::bDontApplySpeed`), check once in half a second
(`Vehicles::HoldCarsWithoutGround`). This is how the game holds its mission machines until a collision loads
underneath them (`m_bIsStaticWaitingForCollision`). A standing car with no land did not drop anything, but
when it was touched, it fell through the map. The partner went to Binco, the host opened the door of his
Savanna: in the partner's game, a copy of the host opened the same door, the machine woke up and fell, and
behind it a copy of the host and the host himself who sat in it (03.11, 0.3.2; "at a distance, the partner
often does this"). The car is released as soon as the ground is back, our player has sat in it or the car is
no longer ours (the one who sat behind the wheel, drives it in his game, where the land is). A copy of another
player, seated as a passenger, does not remove the retention: it would wake the car and wake up. Cars with
people who are not players (traffic), the mod does not keep: their game leads itself. Other people's flags
freeze (mission) fashion does not touch: such cars he misses. The mirror takes the position with advance on
the speed of the owner for the time from the last state, and the turn now too: the quaternion is twisted at
the angular velocity (`Quat::Turned`, unit test `unit/test_quat.cpp` in the test repository). Previously, the
car, which is rotated in place with a braked gas (W+S, “donut”), each frame was pulled back to the turn from
the previous state, and the other player it rotated slower and twitched. In the bot test, the course
divergence at such a turn is not more than 7 °.

With the state of the car there are also parts that the driver moves. The mirror does not change them, because
his status is not a player, and each frame receives the values of the owner:
- `CAutomobile::m_wMiscComponentAngle`: Hydra nozzles, raised hydraulics, tow truck boom, forklifts and other
  movable parts;
- chassis of the aircraft `CPlane::m_fLandingGearStatus`, during the lifting and release of another `SetGearUp`
  / `SetGearDown`, so that the wheels disappear or return, and for the flame of the engines
  `m_fAccelerationBreakStatus`;
- Rhino towers, SWAT van and fire truck (`m_fDoomVerticalRotation`, `m_fDoomHorizontalRotation`)
- motorcycle and bicycle slope (`CBike` + 0x648, + 0x650). Its `CBike::ProcessControl` outputs from the lateral
  velocity change in a step, and the speed of the mirror puts every state: the copy swung from side to side,
  and the rider did not lean forward and backward.

The horn sounds while the counter is raised (`CVehicle::m_HornCounter`), only in the player's car. In any
other machine, the sound of the horn plays a random pattern of signals, so the mirror hums through the
interception `CAEVehicleAudioEntity::GetHornState`: exactly while the owner hums. The owner picks up the
player's horn from the button (`CPad::GetHorn`) and the NPCs from the counter. The alarm keeps its time in the
same meter, so the car with the mod alarm does not touch it.

The helicopter spotlight (`CHeli` + 0xA11 on, + 0x9F4 points, + 0xA00 brightness) comes with the state of the
car. `CHeli::ProcessControl` at the mirror it turns off (there is no police AI or player at the helm), so the
mod puts the owner's values after processing, and at night `CHeli::PreRender` adds light to the frame, like
the owner. There are four spots for spotlights in the frame (`CHeli::HeliSearchLights`), and
`AddHeliSearchLight` does not check the border: with helicopters of another game, the night chase lit the
fifth on top of what lies behind the array. The extras are missed.

The height of the hydraulic wheels is kept only in local variables, while counting it from the right stick.
Therefore, in a car driven by a remote player, heights are considered here from his stick the same formula.
The mirror has the status of a mission machine, and for it, `CAutomobile::HydraulicControl` takes heights out
of `CVehicle::m_aSpecialHydraulicData` (the slot allocates `GetSpecialColModel`, slots for the entire game
four).

A trailer or a car pulled by a player's car is picked up by the game (`m_pVehicleBeingTowed`) and sent out as
often as a car with a driver. The status of physics is not imposed on the mirror of the trailer: the abandoned
trailer itself is looking for a tractor (`CTrailer::ScanForTowLink`), even if the tractor in this game is a
mirror.

`CREATE_CAR` marks the car occupied by the script (`CVehicle::bIsLocked`), and the tow truck and tractor do
not catch such (`CAutomobile::TowTruckControl`). Mirrors are created by the same team, so the mark is removed
from the mirrors of ordinary cars, and the mission machines remain as in the owner's game. Only the tow truck
driver button clings to the car, so the coupling is transmitted by the driver’s game (`X_TOW`, network id of
the tractor), and the other clutches copies (`CVehicle::SetTowLink`) and leaves the car on the hook the status
of towed: with any other, its processing breaks the coupling. A luggage train at the airport is a chain where
each trolley pulls the next one, and the driver’s game takes the entire chain.

Events “began to sit down” (car, seat, driver or passenger, theft) and “out” are transmitted reliably. Remote
ped plays regular landing and exit tasks, and if the animation fails due to outsynchron, it is transplanted
instantly (`WARP_CHAR_INTO_CAR*`). There will be no animation if the car has already left: its speed is more
than 3 m / s or up to it is more than 8 m (`CarGoneOn`). Then the copy immediately sits in place. Previously,
she ran for a car or motorcycle up to 4 seconds, on which the owner sat for a second, and only then was
transferred (27.10). While the owner is still walking to the car (his state is “on foot”), the speed threshold
is 8 m / s: the car is stolen, which rolls to a stop. At 3 m / s, the copy did not go to the door, the driver
from the partner pulled out the synchronization, and he played the animation “pulled out” in the side, at the
lamp, and a copy was put in the car without animation (28.10). The entrance pass is written into the log with
the speed of the machine and the distance to it.

If our player is sitting in the driver's seat, his companions (player group: Denise in Burning Desire,
recruited Groves) sit in the vacant seats of the same car, and when he gets out, they go out
(`Players::GroupRides`). The game itself calls the group into the car only by the event "leader got behind the
wheel" (`CEventLeaderEnteredCarAsDriver`), so before the satellites remained outside until the host sat behind
the wheel. Maud lands them 3 seconds after landing and only those who just follow him (Task 913, 923, 1207): a
mission that seats its people (Sweet's Girl) has time earlier. The mission ban (group `m_bGroupNeverFollow`,
`m_bMembersEnterLeadersVehicle`) is observed.

Body details (`CVehicle::m_anExtras`, + 0x438: board on the roof of the Picador, raised or folded roof of the
convertible) are selected by the game when creating the car randomly, and the two players did not match. The
owner sends them with the announcement of the car, and a copy is created with them
(`CVehicleModelInfo::ms_compsToUse`, 0x8A6458: in front of `CREATE_CAR` parts number, after it again
"accidentally", -2). The parked car, which has already stood in both games and has been accepted as a replica,
remains with its details.

The passenger sees the partner's car as a mirror that moves smoothly through network data. The driver shows
the passenger as a remote player in his seat.

If the owner has removed the car in which our player is traveling as a passenger (“Home Invasion” removes the
van from the warehouse), our player leaves next to the owner’s player when he walks and is not more than 50
meters, otherwise in the place of the car. He used to stay in it: the car would become his own, get stuck in
the gates that are closed here, and he would fall under the floor.

The health of the car was in a state before, but a standing car was sent out every 2 seconds. An inverted or
burning machine loses about 200 units per second, and the smoke of the copy (health white, then black) and the
fire lagged behind the owner. Now, a car that has changed its health is sent out as often as a car that is
traveling.

The radio-controlled model is a normal car of its game without a driver. The player who controls it stays on
foot (`CPed::bInVehicle` is not put, the model lies in `CPlayerInfo::m_pRemoteVehicle`), so his fortune goes
like a foot, and the model of the other game sees a mirror like any car.

Trains are not network machines: cars go on the rails themselves, behind the first car. This is shown by
`Trains.cpp` (see para. "Common World." A player on a train sends a state of a pedestrian with a flag of
`F_IN_TRAIN`, and another game lands a copy of it in the cockpit of his nearest train
(`CTrain::FindNearestTrain`), removing the NPC driver with whom the train is created. If the player gets into
the cockpit of a copy of a foreign train, the train owner’s game first leads to the cockpit a copy of it
(`TRAIN_ENTER`), and transplants it only if it does not reach.

** Radio in the car** (`Radio.cpp`, `G_RADIO`; user request 03.11, device from PR #49). Each game has its own
radio (`CAERadioTrackManager`, 0x8CB6F8). The remote driver was not a player for the sound of the car
(intercept `CAEVehicleAudioEntity::Service` in `Players.cpp`), so the passenger's radio turned on his own
landing (`TurnOnRadioForVehicle`): the station of the car, a random track from a random place, and the
passenger switched it himself. Now the station is selected by the driver (user choice). While players are
driving in his car, his game sends a station, queue tracks (`m_ActiveSettings.TrackQueue`, their types and
numbers) and a place in the playing track (`PlayTime`: the flow position that `CAERadioTrackManager::Service`
receives each frame). It sends immediately when changing the station, track or passengers, and to correct once
every 3 seconds. As long as the driver's radio is rebuilt, it doesn't send anything. The passenger's game puts
the same station as the button (`StartRadio`, 0x4EB3C0), and replaces the tracks it has chosen with the
driver's tracks in `m_RequestedSettings`. The place she puts when `Service` starts the track (call
`AEAudioHardware.PlayTrack` to 0x4EBB11): where the driver is now, adjusted for ping and for the time the
stream spends starting. If the passenger has separated from the driver for more than a second, the track is
restarted, but not more than twice per track. The click, rearrange sound and station name on the screen are
the same as the button.

While the game itself runs the same track (passenger just sat down, `m_nMode` less than `RADIO_PLAYING`), the
matching waits. If the track is still in the flow channel (`CAEStreamingChannel`, "ready" or "stopped"),
`CAEStreamThread::Service` does not rewind it, but only releases it (`SetReady`), and the place from the
launch disappears. This branch of the game connects the tracks (`CheckForTrackConcatenation`). The track was
running from where the game had chosen, 135 seconds from the driver (05.11), and one of two corrections was
lost. The bill of amendments starts anew with a new trip and a new station. What ended the amendment, writes
the log: “in step” with a difference in MS or “kept missing”. Mode tracks are matched by number, so players
with different sound files (mode) number will match, and the song does not. The length of the track of the
driver comes in the same message, and if your own log writes it once on the track (“the two games’ audio
differs”).

The time of the game itself is not accurate. The flow channel buffer consists of two halves, at the 32 kHz
track, half plays 3072 ms. `CAEStreamingChannel::PrepareStream` fills both and puts `m_lastSlot` in 0, and
`m_lastWrittenSlot` leaves from the previous track. If there's 1, `UpdatePlayTime` shows half the buffer more
until the first half finishes. The passenger checked after 3 seconds after the launch and saw a difference of
3 seconds where it was not, and the second time in vain started the track. The driver, who had just got into
the car, sent a seat 3 from the front, and the passenger actually went ahead until the next amendment (05.11:
7 trips out of 12 ended in "kept missing" with a difference of 2.9-3.1 seconds). Hook `PrepareStream` in
`Radio.cpp` levelles the field, and the time is correct from the first frame. The field only reads
`UpdatePlayTime`, and it has no other customers.

The radio buttons of the remote driver passenger do not work: `Service` passes `CheckForStationRetune` and
`CheckForStationRetuneDuringPause` (0x4EB9F4, 0x4EB9FD), and their requests (buttons, `RetuneRadio` scripts, a
sound page in the menu, at `no_pause_menu` and under the open menu) are reset. After the first tap in a
session, the chat writes that the driver switches the radio. User Track Player plays files from the driver’s
computer, so the passenger turns on his own, and without his files the radio is turned off, and this also
writes a chat. It doesn't transmit music online. The driver or the passenger is out, the synchronization ends,
and the radio stays as it was. With the driver-NPC and in the car with a special radio (police scanner)
everything is in the game.

When leaving the machine, the game remembers the station (`CAERadioTrackManager::StopRadio`, 0x4E9820: station
at +0x74, time at +0x58, day and hour of the game at +0x16 and +0x17) and in the next machine for a minute
turns it on itself (`StartRadio` when the autoset radio is included in the menu). After the passenger
traveled, the partner station came to the host’s own car (03.11: the host machine played SF-UR from the trip
two minutes earlier), and the pause menu showed it selected: the menu takes the current station when it opens.
Now, the passenger's game remembers these four fields when he sits at the remote driver, and returns them when
he gets out of the car (as soon as `StopRadio` recorded his, or a second later). If a passenger has moved
behind the wheel of the same car, the station is already his, and nothing returns.

When leaving the pause menu at the host "the game was not shown, there were lights" (03.11), this is not
visible in the logs: the mod menu did not record. Now `Diagnostics` writes opening and closing the pause menu,
whether the radio is reconfigured (as long as it is heard, `CMenuManager::CheckForMenuClosing` does not close
the menu on Esc: `CAudioEngine::IsRadioRetuneInProgress`) and the screen is darkened (`TheCamera` + 0xBFC;
while the screen is darkened, `Idle` does not draw the world). If after 1.5 seconds after closing the screen
is still darkened, this is a warning in the log.

## Ownership of vehicles and NPCs

Each entity (machine, peda, object) has an owner, that is, a game that simulates it. The owner sends out her
fortune, and the rest of the players instead create a mirror: a missionary entity without AI, which is driven
by the network.

Each player creates a population around them. If the partner is closer to 60 m to the host (hysteresis up to
90 m) and he has no wanted, he does not create pedestrians, traffic and parked cars. All this is done by the
host (`Population.cpp`). With the wanted partner calls his cops himself, but the generators of parked cars are
still there (`g_covered`): his traffic and the police are responsible for the chase, and the host game gives
the parking nearby. A wanted partner's game put their cars in the same parking spots as the host, both
declared them at once, and a copy of each wiped out her car at the other game (08.11, four, further on what
came out of this). When a partner's population is turned on again, the counters
(`CPopulation::m_CountDownToPedsAtStart`, `CCarCtrl::CountDownToCarsAtStart`) fill the county at once, rather
than one pedestrian at the edge of the screen. They are turned on only behind a dark screen: they put people
10 meters in plain sight, and cars are close to the player. The host keeps its peds and cars near the partner
only as long as it has land under them (no further than 100 m from the host). Otherwise, the engine would have
removed them from the host’s camera, and without the ground they would have failed.

With a toy on radio control, the game creates a population around it: `FindPlayerCentreOfWorld` (0x56E250)
takes `CPlayerInfo::m_pRemoteVehicle` before the machine and the player himself. Therefore, if a partner or
host has a toy, the distance for this rule is measured from it, and the partner finds the host toy by its
network number (`G_RC` 2). In "Supply Lines", both CJs stood on the roof of Zero, the partner's population was
turned off, the host created his own around his toy, and the partner's plane flew over empty streets (31.10).

Pedestrians are declared within a 120m radius of another player. Cars with drivers and passengers he declares
in a radius of 170 m. Further 150 and 220 m, respectively, he removes them from the network (`Peds.cpp`,
`Vehicles.cpp`). The pedal mirror is obtained from the civil pedal of the same model with an empty decision
maker (template 65542) and all proofs. It goes with the tasks of “go to the point”, so the animations of
walking and running are common. Host cops are seen by a partner as mirrors, and vice versa. A mirror that
could not be created in 5 seconds, once falls into the log with the reason: a special model, the model is not
in this game, the model is not yet loaded, the car is not or the place is occupied, the pool of pedes is full
(no more than 30 per session). In Robbing Uncle Sam, the partner did not see the attackers on Ryder (01.11),
and there was no reason in the log.

The distance to the other player is measured from his toy (`RemoteViewDistance`): the partner sees what is
next to his plane, not just what is next to his CJ on the roof. Without this goal, the hosts in the "Supply
Lines" were only tagged on the radar (31.10). The game announces its toy, no matter how far it flies, so that
the other game knows where the player is looking from.

If the same car is already in place (parked cars both games create the same), it becomes a mirror instead of a
new one. The machine that the script created is not taken this way: then it would become a normal machine, the
engine would remove it, and the script would fall on the next command with it.

The car with the player behind the wheel is owned by this player. The rest of the cars and pedals are owned by
the person who created them. The owner does not remove his peds and cars next to his partner (ManagePed and
PossiblyRemoveVehicle are intercepted).

The car in which the player arrived, waits at the entrance while he is in the interior. The interior lies
miles from the door, and the `CCarCtrl::PossiblyRemoveVehicle` measures the distance on the map, so the game
removed the car or gave it to another player with a regular machine, and it disappeared as soon as he too
left. Now, for the duration of the interior, the machine is listed as a mission machine, and after the exit,
it receives the former type.

The door lock (`CVehicle::m_nDoorLock`) comes with the exterior of the machine: the mission locks and unlocks
its machines after both games know about them. There used to be a lock, which was when the car became known,
and the partner who got into the locked mission car, could not get out of it (Savanna after "Cesar
Vialpando").

When the owner’s game has removed his or her passer-by or regular car and the other player is noticeably
closer to them (30 m for the pedal, 40 m for the machine), it passes them to the other player’s game
(`G_PED_OWNER`, `G_VEH_HANDOVER`). The car leaves with the peds sitting in it. The host makes his mirror a
normal pede or car and lets the car with the driver drive around the city, and then removes them herself.
Previously, such passers-by and cars disappeared in front of a partner as soon as the host drove away. Without
a margin of distance, they were thrown back and forth while the players were standing nearby. What the game
would delete (`CVehicle::CanBeDeleted`, `CPed::CanBeDeleted`: not mission characters, not recruited, not those
who get in the car or get out), what is not visible on its screen and what does not hold its script
(`CTheScripts::MissionCleanUp`, `StoreVehicleIndex`). The usual machine, which the owner's game has removed
next to another player (about 80 m, in the same area), this game keeps with the people sitting in it
(`Vehicles::KeepsWhenDeleted`, `TakeOver`). If there are several players next to the car, one game leaves it:
the host, if the host is nearby, otherwise the player with a smaller number (the same order as two at the
wheel). The others delete their copy and get the machine from the game announcement, so they lose about 0.3
seconds. Three and four of each game left a copy next to itself and announced it again, and in one place stood
up one car from each (08.11). If two at the 80m border think differently and leave the car both, their copies
will wipe each other off when they appear and leave like the cars that gave way (below). So after the death of
the host, his game removes the police from the streets, and police cars cannot be transferred: their crew goes
into someone else's game, only to chase. They used to disappear in front of a partner. During the mission of
the host, the partner does not leave such machines to himself: the mission clears the scene (`CLEAR_AREA` in
the lowrider contest), and the abandoned machine returned to the host with the partner's machine. Mission
vehicles, skeletons and helicopters with planes are still leaving: a helicopter without police AI would be
hanging in the air. Cops, medics and firefighters are not transferred: the mirror of another player is always
a civilian pedal. Nor is a person with a script-brain (`CPed::bHasAScriptBrain`) transmitted: a diner cashier,
an Ammu-Nation salesman, a hairdresser, a tattooist. The game puts it at the attractor and leads the script,
and the other player he remained a copy without both. The attractor there was considered empty, the game put
its cashier, and there were two. Only the player who is currently in the game gets it: while the partner loads
the world, nothing is given to him.

In "Ryder" the script of the diner host (`JFUD`) at the signal of the mission (`$352`) removes its cashier,
and the mission puts its own 0.9 m from its place (`CREATE_CHAR 24 155`) and its copy goes to the partner. The
partner does not receive such a signal, and his `JFUD` (he goes if the partner's player enters the door) holds
his cashier: two cashiers next to him, calm and with his hands raised (28.10). Now the man script of this game
(`Peds::MarkLocal`) is hidden from the partner while during the host mission 1.5 m away from him is a copy of
the man mission of the host of the same model, and again visible when they dispersed (`Peds::HideTwins`). You
can't delete it: `JFUD` without a cashier, each frame erases hints (`CLEAR_HELP`).

Police lock-up cars (`IMPND`, it is launched by `IMPND_L` when the player is closer to 120 m) each game puts
itself, and copies of strangers stood on top of their own (“why the car on top”, “clone”, 28.10). The machines
that created this script remain their own until no one is behind the wheel (`Vehicles::KeepLocal` of
interception `CCarCtrl::CreateCarForScript`, check at `ScanTraffic`). His people were already his own
(`MarkLocal`).

Also, other mod scripting machines (Busy Pedestrian, Urbanize: CLEO scripts put machines in permanent places
and let their garbage trucks run) remain in their game. Their code lies outside the main.scm space, and it's
not the main.scm stream from a single file (`m_bIsExternal`) as in #36. Each game made its own, copies of the
other game's machines got into them, and the machines exploded (30.10). Such a machine goes to another game
only when the player is sitting in it.

The machine that the owner removed to the shelter garage (the door closed and the game saved it in the garage,
or the host took the partner's car), the owner's game deletes, and the other game left a copy of itself as a
regular machine (`KeepsWhenDeleted`). In the garage, the car was in two places at once: in the garage
recording and on the floor. The garage was full, cars were coming out again and again, and the door would not
open (CJ 28.10). Now, a copy that stands in the shelter garage (`CGarages::IsPointWithinHideOutGarage`) when
the owner removes it, goes away here (`HandleRemove`): it is held by the garage record.

A pedestrian who fights, shoots or runs away does not give up as long as there is ground under him: handed
over, he became calm to a passer-by in the middle of a fight. Further than 100 m from his player, the land
under the pedal of this game may not be, and then he is given even in plain sight, otherwise he will fail.
Released by the script to disappear (`REMOVE_CHAR_ELEGANTLY`, `CPed::bFadeOut`) is not held or given: Catalina
after the failed mission stood on the restart twice.

The game removes its passers-by by its camera: outside it and beyond 25 m peda disappeared in 4 seconds,
although standing in front of another player. Now the ped stays while the other player is looking at him (no
further than 60 m and within 60° of the direction of his camera, the camera is in his state) and this game has
the ground under him.

Passing through the door, the game removes all of its passers-by (`CEntryExit::TransitionFinished` →
`CPopulation::RemoveAllRandomPeds`). The street around the other player was empty when this one entered the
store, and the interior people were disappearing from the one who was left inside. Now those who are 60m away
from the other player move into his game first.

The machine that was transmitted by another game loses its mirror protection (`Release`). In the past,
collision protection remained on it, and its traffic did not get damaged in accidents.

The mirror is protected from the population of its game: while the machine is alien, it is listed as a created
mission, and when it becomes its own again, it receives the same type (`Protect` and `Release` in
`Vehicles.cpp`). In the past, a car that was taken by another player remained common in the previous owner’s
game and was removed as soon as he left, along with the other player’s character sitting in it. The script
machine of another game remains missionary for the new owner: it cannot be removed in the middle of the
mission. If the owner removes the machine, and its copy here is not created by a mod, but is taken from his
world, it is not deleted and returns to his game, the script machine remains at his script. When a script
releases a machine that is now a mirror in its game (its mission is over), the owner learns about it
(`G_VEH_SCRIPT_FREE`) and makes his copy a regular machine. Otherwise, she remained missionary forever, and
with a new announcement, she stood second. The walking mirror may lag behind: the task of walking is waiting,
the path blocked what is only in this game, or the straight line did not coincide with the route of the owner.
If the backlog is more than 1.5 m, each frame is pulled up by 5% to the owner's place. The mirror goes to a
point slightly ahead, in the same direction of the owner, about a second ahead, so that the walk does not
freeze between states. When the owner rolls around the corner, the dot ends up in the wall and the mirror goes
into the building (issue #43). If a building or object interferes with it (`CWorld::GetIsLineOfSightClear`),
the mirror goes straight to the owner’s seat. The mirror crouches with the owner’s pedal (over the cover in a
shootout): the flag came before, but was not used.

The pede, which the owner was out of the car without the task of exiting (dropped off the motorcycle, thrown
out by an accident, pulled out), here comes out immediately in its place. The `TASK_LEAVE_CAR` used to get off
politely or sit on a sliding motorcycle for a few seconds. While someone gets into the car here (our player
steals it), the driver is not put back: the next one will come in a state that he was pulled out. The driver
that the mission keeps behind the wheel (`CPed::bDontDragMeOutCar`: Smoak on a motorcycle in Just Business)
cannot be pulled out of the mirror. Previously, our player took away the mission motorcycle here, and from the
owner, Smoak sat on a copy of the car and did not go anywhere.

The local manager of the population does not touch the mirror: the removal comes from the owner. When the game
starts or restarts, the player sends `G_RESYNC`, and the others re-announce their cars and peds.

The task that the script gave takes up one of 128 entries `CPedScriptedTaskRecord` until the ped takes it. The
mirror is where this game is not loaded with collision, does not take tasks, and new ones accumulated until
the recordings ended, and the game fell. Now the mirror is not given a new task while the old one is waiting
(`TaskPending` in `Peds.cpp`). If the records are still over, the game first releases the executed, and the
log is written which teams hold them.

The time of day and weather are set by the host (`World.cpp` times per second). The partner lets down his
watch when it parted with the host's clock for more than half a minute of play. A couple of weather hosts (old
and new) he takes, only as long as both are the same hour. Each game counts a fraction of an hour
(`CWeather::InterpolationValue`) by its own clock, and its decrease of `CWeather::Update` (0x72B8A1) takes it
for a new hour and moves on to the next weather of its list. The mod used to record a partner's share of an
hour of the host. Because of the network delay, it often slightly overtakes his own, and the partner's weather
and color correction switched about once per second. Now, after the eyeliner, the fashion puts a fraction of
an hour on the partner's new watch. The game itself translates hours after death or arrest (12 hours), saving
(6 hours), and missing a trip (`CGameLogic::PassTime`). A partner in the host world misses this translation
(call `CClock::SetGameClock` to 0x441534): the clock of the world is shared, and before they returned in a
second synchronization, and the light galloped twice.

The search for a host that comes to the partner along with saving is reset at the start.

NPC actions are repeated by the game itself. Aiming and shooting are repeated with "aim" and "shoot" tasks
with posture, flash, sound and tracers. The target could be a player, a network pedal or a point. Copies of
police officers look like police officers themselves, according to three edits, verified by a bot from
animations on both screens. The policeman holds the gun with two hands (`colt45_fire_2hands`): the game gives
the skill `WEAPSKILL_COP` peda type pistol COP, and the copy is a civilian, not to be considered by the
police, so the skill (`CPed::m_nWeaponSkill`, + 0x72C) she puts himself while in the hand the gun. Step side
by side with the weapon (`GunMove_L/R/FWD/BWD`) copy makes its task shooting: in
`CTaskSimpleUseGun::m_MoveCmd` (+ 0x14) is written the direction of speed of the owner relative to the copy.
At `ControlGunMove` it is impossible: at a high FPS, its steps do not grow to the threshold of 0.1, and the
copy stood and slid. The squat copy (`SetTaskDuckSecondary`) was created as a squat that holds another task
(`DUCK_TASK_CONTROLLED`). The policeman has the task of attacking him, and the copy has no one, and after 4
frames it ended. Maud put it up again every 0.4 seconds, and each time the game filmed an animation of the
weapon, so the copy never fired a squat. Now the squat is translated into script
(`CTaskSimpleDuck::SetControlType`, `DUCK_SCRIPT_CONTROLLED`) and holds until the owner stands up. The
policemen walk into the game of the wanted player with only two stars: one is chased by one policeman, and the
others stood next to him and did not let the star burn (issue #56). Getting into and out of the car is
repeated by regular tasks. Everything else (conversations, phone, benches, fights, falls) is transmitted as
the current “action animation” of the owner: the number of the IFP block and the animation number in it.
Walking, running and rack (the pedal movement group) are not transmitted, facial expressions (flag
`ANIMATION_FACIAL`) are also transmitted: each peda has its own task. Partial animation (hit, block) is
superimposed on the full (battle stand) and is considered an action while playing. The mirror holds the full
under it and continues it, rather than starting over. Cycling animation that has had time gone backwards has
just gone on a new lap, it's not a restart. As long as the mirror holds full action, the game does not change
the position of walking and standing (`CPed::SetMoveAnim`): the real pedal does the task of fighting, but the
mirror does not. Without it, the rack would go out and rise anew after each stroke, and it seemed like the NPC
was swinging all the time.

Updating the animations peda takes the first of them without looking (speed allocation, 0x4D1750), so the
pedal without animations drops the game. The peds of the game itself always have a stand or a walk under
everything else. The action of the mirror (combat stand) fades when the owner runs out, and the game gives the
walking posture only when the state of movement changes, and the games flew out half a second after the fight.
Now the mirror without a long animation of the body returns the rack of its set
(`CAnimManager::BlendAnimation`, `ANIM_ID_IDLE`).

On foot, the NPC mirror shoots the firing task (`TASK_SHOOT_AT_CHAR`, `TASK_SHOOT_AT_COORD`) while its owner
shoots, and the rest of the time only aims. What the NPC shoots, the owner’s game saw on the empty clip, and
cheat endless rounds (`FULLCLIP`, each game has its own) does not spend the clip from anyone: the partner’s
people of the host only aimed (29.10). Now the shot is seen at `CWeapon::Fire`. The mirror is given 25,000
rounds: from such a reserve, the game does not waste ammunition, and the previous 500 ended in a long
shootout, and the mirror only aimed further.

The mirror repeats NPC shooting from the car with the gang’s regular task (`TASK_DRIVE_BY`, task 1022): stick
out, aim and shoot at the same target as the owner (at a copy of his target or at a point). His shots hurt
what this game possesses, including its player, as does shooting a mirror on foot. Finishes the shooter: the
mirror is cleared of tasks (`CPedIntelligence::ClearTasks`) and it sits again.

Interior people (policemen, shoppers, tenants of a furnished house) are created by the game when its player
enters (`CEntryExit::GenerateAmbientPeds`, `InteriorGroup_c::SetupPeds`). Before, each player had his own, and
he did not see strangers. Now they are created by the player who entered first, and the other player misses
these calls while the other player is in the same interior. The check goes to the point that the game
transmits: the player is still standing at the door outside. Such people remain in the zero area, so
`Peds.cpp` counts them in the player's interior, next to which they are standing. It could also be another
player: earlier, when the host left, their game took them off the network, and the partner who remained
inside, they disappeared. Now they're moving into a partner game. Most of the interiors hang high above the
map, but some stand at street level (Atrium, Lil' Probe Inn), and the people of the street around them were
counted inside and disappeared from the player outside. Below 500m pedes are always considered outdoors.

The people that the script creates, in addition to the story mission of the host (seller Ammu-Nation,
shooters, casino croupiers, gym boxers, airport security), the same script creates in another game for its
player. Announced in the network, they stood at each two in the same place. Now such pedes remain in their
game as rebounds (interception `CPed::SetCharCreatedBy`). The pede, who left by car, is announced again, and
the other player still sees the people who work part-time on the street (a taxi passenger, vigilante
criminals). Models of such people in two games can differ: the script chooses them randomly. Tested by a bot
at Binco and Ammu-Nation: In each game, one seller, host buyers are visible to a partner, and no one is worth
twice.

The exception is the gym coach in the ring (`GYMLS`, `GYMSF`, `GYMLV`). While he is fighting our player (Task
`TASK_COMPLEX_KILL_PED_ON_FOOT` with the goal is our player, health is above 100 after `SET_CHAR_HEALTH 1000`;
the sparring pair in the corner of the hall does not change health) or teaching him how to take him after the
first victory (wide screen, closer to 8 m), he is announced to the network (`GymOpponent` at `ScanOwned`).
Previously, the second player saw a copy hitting the void: “fight the invisible” (28.10). His coach of the
second player at this time is in his corner, it is not hidden: in Las Venturas the same model boxes sparring
partner, and the hidden model would leave the other without a pair.

The pedestrian, who was handed over by another game, needs to be made normal again: the mirror was civil
without reaction. It is returned to the owner type (except for cops, medics and firefighters, who have a
different class) and the standard decision maker according to its statistics
(`CPed::SetPedDefaultDecisionMaker`). The mirrors of the player who went out also become their mirrors: before
they were removed, and his cars remained on the road without drivers. The cars of the deceased become their
own and leave the network. When he connects again, his game will start counting numbers from the beginning,
and the matching number will give him our car. The number counter now and starts every time from a new place.
Accepted ped passes in the population counters from the type with which it was created to his own
(`CPopulation::UpdatePedCount`). Without this, everyone left an unnecessary civilian and missing bandit, and
the composition of the streets moved for a long game. The senses of the passerby return to him (gangs hear and
see at 40 m, the rest at 15 m): the mirror is created by a script, and the script pedal hears and sees at 30 m
and does not call its own.

The mirror of the machine is invulnerable to collisions (`bCollisionProof`) as long as it is a mirror. One
strong blow (the Rhino ram is considered fifteen times) exploded the copy immediately, its passengers died in
this game, and the car from the owner drove further. The game gives its car in place of the mirror to him only
if it is empty (the car of the parking generator is in both games): the car with the driver would be driven
further by its driver, against the owner's states.

Two drivers of one car (sitting at the same time, or one pulled out the other, and the game of the second did
not see this) used to send each other applications for it while both were driving, and each saw a copy of the
other frozen nearby. The machine remains with the player with a smaller number, that is, the host, and the
second is transferred to the next place if it is free. The mission machine of the host, which was ridden by
the partner, returns to the host when it is empty for 2 seconds. Otherwise, the host mission commands
(replace, freeze, repair, delete) acted only on a copy of it from the host and immediately rolled back.

The commands with which the mod creates and moves copies of people and objects (`CREATE_CHAR`,
`SET_CHAR_COORDINATES`, `WARP_CHAR_FROM_CAR_TO_COORD`, `CREATE_OBJECT`) no longer clear space around them
(`CTheScripts::ClearSpaceForMissionEntity`). A copy of the partner removed the taxi of this game along with
the driver when the partner was driving in it, and a copy stood in his place. Copies of cars are still
clearing the place, otherwise two cars at the same point flew away.

His announced car, which was so erased by a copy of the car of another game, gave way to it. The game sends
its deletion with a mark (`G_VEH_REMOVE`, byte 1, `NetVehicle::gaveWay`), and no one leaves such a machine for
themselves: in its place stands the one whose copy was erased. Two games put their cars in the same parking
spots and declared them all at once, a copy of each erased the other's car. The rest of the games were left
erased and announced again, these copies erased each other in turn, and four in 15 seconds in one place scored
a hundred cars: the pool was filled, the frame went 0.7 seconds (08.11). If both cars wiped each other at the
same time, neither remains.

The gait that the script gave to the pedestrian (`SET_ANIM_GROUP_FOR_CHAR`: drunk, old man), the owner sends
in condition (`CPed::m_nAnimGroup`), and the copy walks just as soon as the block of these animations is
loaded.

A replica of a bandit, dealer, criminal, prostitute or bum gets the owner type. `CREATE_CHAR` makes for all
these types of the same civilian, and the gangs of this game didn't consider copies alien: a fight between the
people of the two games didn't start. The copy itself does not react to anything, the people of this game
react to it. Our pedestrian hits a copy of a stranger by the owner (`G_DAMAGE` species `TARGET_PED_BY_PED`
with the attacker’s number), and there his peda is injured by our copy of the attacker.

The cops of this game, next to another player who is wanted (and we don’t), go into his game: only she can
chase him. There, an accepted cop is created by a cop (`CREATE_CHAR` with Type 6) and begins the chase
himself.

A Grove fighter can be recruited when he is a copy, and a prostitute can be called to his car, too. The game
asks the owner to give the pede (`G_PED_OWNER`, addressed to the sender himself), and the received pede joins
the player group (`MakeThisPedJoinOurGroup`).

The body removed by the owner dissolves in another player (`0x034F`) rather than disappearing immediately. If
the pool of peds or machines is full, the copy takes the place of the farthest passer-by of this game, which
is not visible, or an empty random car. Before, a copy just didn't show up.

A replica whose owner is in the air (a SWAT fighter on a helicopter cable, a fall) hangs where he is.
Previously, gravity pulled her down, the rule of “elevation difference greater than 4 m” put her back, and the
fighter jumped up and down. A copy of the model that is not in this game (the other player has a mod) is not
shown, and the magazine once writes which model is not.

The game puts new people and machines in front of its camera. Another player nearby saw people appear 5-45m in
front of him and cars right in front. Now the seat is closer to 42.5 m (for a 90 m machine) to the other
player's camera and within 60 ° from its direction is skipped until the next attempt
(`CPedPlacement::IsPositionClearForPed` in two calls `CPopulation::AddToPopulation`,
`CCarCtrl::GenerateCarCreationCoors2`). Two more people check calls are untouched: one gathers neighbors, and
the other "busy" means "put."

The machine generator (`CCarGenerator`, parked cars) also looks only at its player
(`CheckIfWithinRangeOfAnyPlayers`), and the high-priority generator places the machine at any distance. The
robbery van (`CREATE_CAR_GENERATOR` model 609) appeared 2 meters from another player, inside the van of their
joint robbery: both exploded and the robbery failed (03.11). The generator machines played the robber, and the
second played the generators whenever one player was in the house and the other on the street. The generator
now waits for another player to stand closer than 20m from his seat or sees that spot from 90m
(`CCarGenerator::CheckForBlockage`), as well as a spot for a random machine.

The high-priority generator puts the car further than 140 m from its player, and the
`CCarCtrl::PossiblyRemoveVehicle` immediately removes the car outside the camera further than 45 m. The
generator puts a new one in 0-10 seconds, and so on in a circle. The host waited at the driving school 144m
from the van generator of the robbery in San Fierro, and in 10 minutes the mod announced 37 vans to another
game, and the latter each created and removed. Now, a parked car only leaves the net when it has stood 5
seconds (`CVehicle::m_nCreationTime`, + 0x430) or when the player is sitting in it (`Vehicles::ScanTraffic`).

Interior people are created by the game of the one who entered first. If the host and the partner entered at
the same time, each game saw the other outside and created its own. Now, the host reports that it has created
interior humans (`G_INTERIOR_POP`) and the partner is cleaning up their own. Interior spaces for visitors
(buyers of Ammu-Nation) are filled only by the game that created them.

The pedestrian of this game next to another player would be silent if our camera was further than 40m: the
game doesn't start a phrase further away from its camera. For such phrases, the distance check is from another
player's camera (`PedSpeech`).

## Damage

Peds and cars are damaged only by the owner. The local player gets hit by the mirror (`G_DAMAGE`) and the
owner uses it regularly (`CWeapon::GenerateDamageEvent`, `CVehicle::InflictDamage`), considering the attacker
to be a remote player. The NPC runs away, fights back, or dies as usual.

An NPC strike in close combat on a remote player, his game forwards that player's game (`G_DAMAGE` with the
goal of "players"). Here, the remote player has all the protections, and the NPC mirror in that game only
repeats the animation, so without sending the fists and bats, no one was injured. You don’t need to send NPC
bullets: the mirror in that game shoots for real. The mirror's own blow to the player is not considered
anywhere: the owner reports the actual strikes. Mirrors sometimes fought themselves and the player was hit a
second time off (29.10).

With a blow is the reception, which he was inflicted in the game of the batter (`CTaskSimpleFight`: set and
number of the hit). The victim's game chooses the fight response (`CEventDamage::ComputeDamageAnim`:
`HitA_1..3` style, knocking down the last punch of the series), and the mirror does not have such a task, and
the victim always shuddered the same. At the time of calling damage in the attack slot, the batter is tasked
with fighting with this technique (`FightAs`), then the slot returns its own. The victim's game determines the
side of the blow to its player (`ComputePedShotSide`) by how its player actually stands: the copy of the
victim in the batter's game turns with a delay.

Katana, who beats the player to death, blows his head (`CPed::RemoveBodyPart`: flag `bRemoveHead` and body
part in +0x754). In a single-player game, CJ can't be killed, and the other player's copy is ped `PLAYER1`,
and her katana kills like that. Resurrection did not return the head (`CPlayerPed::SetInitialState` of these
fields do not touch), and both players went headless (29.10). Now, when the player is resurrected, the flag is
removed and a part of the body is dropped.

The crime counts the play of the person who committed it. The engine records the crime on the player in focus
(`CEventGroup::Add` → `ReportCriminalEvent` → `FindPlayerWanted`), so that the beats of the partner used by
the host game raised the search for the host. Now, applying someone else's punch, the owner returns his
`CWanted` as was (before the sound object of the police scanner), and the player's play records the crime
itself, as the game records his player's punches (`CEventDamage::ReportCriminalEvent`): in line
`CWanted::RegisterCrime`, with the same type of gun crime. Stars are given by the police, who will see it,
through the mission search multiplier. `CCrime::ReportCrime`, which was here before, with a police officer
closer than 14 meters, immediately put the star past the multiplier, and the partner received stars in
missions where the host does not receive them.

The police hosts of a partner stand in replicas with the type of an ordinary passer-by: they do not need
police tasks. `CWanted::WorkOutPolicePresence` counts only people of type `PED_TYPE_COP`, and the car theft in
front of such a copy of the star did not give (#92). Now, at the time of the report of their crime
(`CCrime::ReportCrime`) copies of the policemen are given their real type, and then return the old one.

The game records the hijacking directly in the landing task: `CTaskSimpleCarSetPedInAsDriver` and
`AsPassenger` are called `ReportCrime(CRIME_CAR_STEAL, vehicle, FindPlayerPed(-1))`, whoever sits down. A copy
of a partner getting into the host's car gave a star to the host (#92, disassembled 04.11). `HookReportCrime`
misses the hijacking report if the machine is not the one our player is landing in (`m_pVehicle` tasks put
before the report), or if there is another player in it. In his game, the partner did not receive a report:
`CREATE_CAR` marks the copy as the player had already had it (`bHasBeenOwnedByPlayer`, bit 2 in + 0x42A), and
the task missed it. Now the `VehicleSpawn::owned` carries the owner's real bit, and a replica of a regular
machine takes it. The car that the player already had is not considered a theft, so landing to the host by a
passenger does not give a star.

The battering of a police car gives a star of `CBike::VehicleDamage` and `CAutomobile::VehicleDamage`, but
after checking the collision protection, and copies of it have: a shot down policeman of another game cost
nothing (disassembled 04.11). `RammedPoliceCopy` (Damage.cpp, hooks 0x6B8EC0 and 0x6A7650) repeats the terms
of the game for a copy with a policeman behind the wheel: a hit stronger than 25, our car is faster and faster
than 0.1. The flag `bIsLawEnforcer` is not in the copy, the model (`IsLawEnforcementVehicle`) and the driver
type (`pedType`) are checked. If the mission multiplier is 0, there is no star.

The other player's car here is being driven by a replica of him, the PLAYER1 ped, and her crimes were recorded
on this player. Who did this car hit at low speed (`CTaskSimpleKillPedWithCar` raises "hit"), what cops jumped
from it, its explosions and fire raised the search for this player: "I am given stars when my partner crushes
people." The driver's game counts this itself, and here such reports are omitted if the culprit is a copy:
`CCrime::ReportCrime`, `ReportCriminalEvent` hit, damage, damage to the car and knocking off the motorcycle,
jumping to the side (`CTaskSimpleEvasiveDive`).

A car hit (`CPed::KillPedWithCar`) is also decided by the driver's game. The mirror is protected from
collisions and would stand like a stone, so its downfall goes to the owner, and he knocks down the pede with
the car of a remote player. This car doesn't hit anyone at the owner's. The canister and fire extinguisher do
not reach the mirror (`CShotInfo` passes refractory peds), so the owner applies their effect on his peds
himself, except for the action on his player.

A weak machine kick (momentum less than 12) does not kill a person in any game (`CPed::KillPedWithCar`). Each
frame was pulled back to the owner’s place, and it stood in front of the car like a wall (30.10). Now, second
after being hit by our machine (`lastRamMs`), the copy is not pulled, and our physics is repulsing it as a
human. A further 6 m copy is still transferred immediately.

The money and weapons of the murdered pede fall in the game of the one who killed him. If the pedestal dies
from a blow, the owner turns off his fallout. The killer's mirror receives a gun drop-off flag and money
through `SET_CHAR_MONEY`: the scripted pedal drops only the money issued by the script. Police, medics and
firefighters don't drop money, just like in single-player. The selections are not synchronized, so only the
killer sees the victim.

The mirror killed by our strike falls as the game throws: the mod applies the same blow (weapon, body part,
direction) to it with a loss of 1000. Previously, `TASK_DIE` for any death played one downside, "they just
fall down." The killer is not recorded: the murder counts the answer of the owner. The mirror killed by
someone else first rises to the place where the owner’s pedal lies (the running one lagged behind), and dies
as before. After death, the body of the copy continues to follow the body of the owner: if it flew away (mode
"super-strike", explosion) or lies further than 1.5 m, the copy is transferred there once in 0.5 seconds and
flies at its speed. Previously, the copy was where it fell. In the test, the body, thrown by the explosion at
7 m, lies in both at the same point.

The shots of the remote player are only repeated by emulating the gamepad, and their damage is discarded. The
shooter's game reports the hits. The victim's owner thinks it's a car hit.

The car shooting mod (any weapon behind the wheel, its aiming) fires its own code, and it works only for its
player: a copy of the other player did not shoot at the gamepad, and the players did not see each other’s
shots (#76). Now each game reports the bullets that its player fired from the machine (`Shots.cpp`,
`G_BULLET`: where from, where from, weapons, interception `CWeapon::DoBulletImpact`). Another player's game
200 ms later draws them at a copy: the tracer (`CBulletTraces::AddTrace`), the flash at the barrel
(`g_fx.TriggerGunshot`) and the sound of the shot (`AudioEngine.ReportWeaponEvent`). If the copy during this
time shot itself (regular shooting from the car comes on the gamepad), the bullets are not drawn, and the
shots are not blown away. There are also queues of machine guns mounted on the machine
(`CVehicle::FireFixedMachineGuns`: Hunter, Seasparrow, Rustler, Predator): a copy of them never fires
(`CVehicle::ProcessWeapons` works only for the player's car), and the other player did not see these shots.

NPC shots, explosions and fire damage what this game has, including its player.

Teams `SET_CHAR_ONLY_DAMAGED_BY_PLAYER` and `SET_CAR_ONLY_DAMAGED_BY_PLAYER` (mission target, criminals on
part-time police work) miss only hits `FindPlayerPed` (`CEventDamage::AffectsPed`,
`CVehicle::CanVehicleBeDamaged`), and hits by a partner on them did not work. The other player is also a
player, so while the owner applies his hit, this flag (`CPhysical::bInvulnerable`) is removed.

During the script scene (the wide screen of the camera), the other player’s side does not hurt anything here:
his player runs on while this one is watching, and the mission failed when her people were injured in the
scene (the partner’s car, left at the hairdresser, was touched, and Ryder appeared in it).

Damage between players involves the host ticking "Partner Damage" before creating the game. The server
receives it with the key `--friendly-fire` (for a dedicated server it is `friendly_fire` in `server.ini`) and
tells everyone in `Welcome`. Without it, nothing from a remote player or his car hurts his player. With her
bullets and blows considers the shooter's game: she sees a hit on a copy of the partner and sends it
(`G_DAMAGE` for the purpose of "player from the player"), and the partner's game uses it normally, and the
attacker is considered a copy of the shooter. Explosions, fire, gas and car are decided by the game of the
victim. The copy of the partner has all the protections, and `CWorld::TriggerExplosionSectorList` and
`CShotInfo::Update` protected pedes are missed, so such hits do not reach the shooter's game. A rocket,
cocktail or gunman's car is repeated in the victim's game and injures the player there. Quiet stabbing from
the back goes past `GenerateDamageEvent`: the attacker's play itself gives the victim damage and the task
`CTaskSimpleStealthKill`, and a copy of the attacker from the victim does the same under his pad and kills the
partner without a tick (issue #58). Without a tick, `CPedIntelligence::TestForStealthKill`, the player
responds with “no” and the knife strikes with a conventional blow that does not reach the partner.

The “Common Life” tick (0.3.3) follows the same road: `shared_life` in `coop.ini` and `server.ini`, key
`--shared-life`, flag `Welcome`. When a partner dies (flag `F_DEAD` in its state) during a host story mission,
the host game fails the mission in the same way as when the host dies: puts the mission scenario a death flag
(`CRunningScript` + 0xD4, read by `HAS_DEATHARREST_BEEN_EXECUTED`) and gives `FAIL_CURRENT_MISSION`. One
`FAIL_CURRENT_MISSION` would return the mission from the main part straight to the cleanup, without the
inscription "Mission Failed."

Without a tick, the death of the host in the story mission does not fail, while the partner lives in his world
(solution 31.10): the host stands next to the partner and plays further, and failure happens only if both are
killed. While the CJ lies dead, `CGameLogic::Update` each frame evokes `CPlayerInfo::KillPlayer` (0x56E580).
The mod intercepts him and the player remains in `PLAYERSTATE_PLAYING`: without `PLAYERSTATE_HAS_DIED`, the
mission script death check (`CRunningScript::DoDeathArrestCheck`) does not work. After 1.5 seconds, the screen
darkens, and the mod picks up the CJ himself (`Revive`): takes out of the car, as the game does in the
revival, gives complete health, calls `CPlayerPed::SetInitialState` and `RestartNonPartialAnims` and transfers
to the partner (`Players::GoTo`, to the right at 1.5 m or 2.5 m from his car, to the ground below).
`CGameLogic` while already standing at `GAMELOGIC_STATE_WASTED` and 3 seconds after the last call would send
the CJ to the hospital, so the mod returns `GameState` (0x96A8B0) to `PLAYING`. Its revival of the game
(`RestorePlayerStuffDuringResurrection`) is not suitable: it removes people, cars and fire for 4 km around and
removes the wanted, and the path through the hospital translates the clock to 12 hours and takes $ 100. The
weapons, stars, and time remain with the host. If a partner has also died or the mission is over, `KillPlayer`
goes through as usual.

Without fire on the player's shots on a copy of the machine in which another player is sitting, it is not
transmitted. In "Just Business," the Russian partner's blunders hit Smoak's motorcycle with the host, and his
strip melted twice as fast (30.10). The other player's empty car gets the damage as before.

The death of a remote player determines his game. We've got his dog playing death and re-created after the
respawn.

Murder counts as the killer's game: statistics, respect, slaughter and provocation of gang warfare. The owner
of the pede, killed by someone else's blow, informs the killer (`G_DAMAGE` with the view of "your murder",
interception `CEventHandler::RegisterKill`), and believes that he was killed by someone else
(`CDarkel::RegisterKillNotByPlayer`). The killer's game calls in `RegisterKill` for its copy of the pede,
giving it a pede type from the owner for the time being: copies are created by civilians. Gang wars only occur
in the host game, so killing a ballass or vagos by a partner 100 meters from the host sends the host, and the
host adds it to the provocation (`CGangWars::AddKillToProvocation`).

Explosions of a remote player (missiles, grenades, tank) occur in our country, repeated `Fire.cpp`. The pedal
mirrors in the game shoot all the defenses, and the explosion misses them, so the explosion hurts their pedes
here: the exception is `GenerateDamageEvent` for `WEAPON_EXPLOSION`, the wanted at the same time remains. The
machine explosion injures the shooter in the game, and the hit is sent to the owner like a bullet.

## Story and missions

The world is shared by preserving the host. When connected, the partner receives the host save file and
downloads it, so both have the same open areas, gangs, homes, and stores. The search for the host from the
partner's save is reset.

The host selects a new game in the F9 window as the last item on the save list. The prologue (Mission `INTRO`:
two cutscenes, a ride in a police car under the credits, a bicycle) host passes one: this is a mission, and
during the mission the game is not saved, so there is nothing to convey the world. Here's the order:

1. When the host launches the prologue, the game sends `C_WORLD_NEWGAME`. The server deletes the saved world (it
   is from the previous game), and those who ask for peace, responds `S_WORLD_WAIT`. The partner waits on the
   menu with the status "Host passes the prologue", and his game does not give up the wait after 120 seconds,
   as usual.
2. The prologue ends when CJ gets on the bike. You can’t stay on the bike either, so the host sends
   `C_WORLD_FOLLOW` and the server sends `S_WORLD_NEWGAME` to the waiters and tags them. If someone asks for
   peace while the host is driving, they will get the same.
3. The teammate starts his new game. Its prologue ends immediately: as soon as stream `INTRO` is started, its
   first command is replaced by `TERMINATE_THIS_SCRIPT`, and the dimming that `main.scm` includes before the
   prologue is removed by the mod itself. The game transfers the partner to the host, and, since the host is
   on a bicycle, the same one appears next to the partner (`Players::FollowHost`).
4. Once the host gets off the bike, his game takes a snapshot of the world. The server sends it to tagged
   partners, and they move into the host world with a normal download.

Save the game loads without checking that it is from the same `main.scm`: `CTheScripts::Load` reads the script
sizes and throws them away, and streams puts them in their places in the downloaded `main.scm`. The world of
the host with another `main.scm` flew from the partner in the first scripts. Saving these dimensions stores,
so before downloading the host world, the partner's game looks for its own (`MainScriptSize`, the size of the
largest mission, the number of missions): downloaded `main.scm`, and if it is not already loaded, a game file
or a replacement from the modloader. I didn't find it, the world doesn't load, and the chat says what's going
on.

A partner gets a bicycle when the host gets on a bicycle in the story of the mission (for example, in Big
Smoke), if the host is nearby. Also, the partner gets his car when there is no room in the host car: Ryder's
Picador, Greenwood Sweet with the gang. The place to which the copy of the host person goes is considered
occupied (`Peds::SeatClaimed`), otherwise the partner sat in the place of Ryder. The car is placed 3.5 m to
the right, left, rear, front or between where there is ground, no wall or pillar on the way
(`CWorld::GetIsLineOfSightClear`) and no car or person in the 2×2 m square (`IS_AREA_OCCUPIED`). This team
counts everything that the limiting sphere reaches. The sphere of the partner (1.25 m) touched the previous
square of 5×5 m at each place, and the bike did not appear at all (INTRO1). The model loads in the background,
and the unsuccessful attempt is repeated once a second until the partner is 150 m from the host. The host
machine, for which the partner has already received his, the game remembers by handle: the new machine in the
same pool slot is already different.

Main.scm scripts work for both, so that shops, rockers, food, hairdressers, tattoos and tuning are available
to everyone. Story missions are launched only by the host. `CPlayerPed::CanPlayerStartMission` says no if it
asks for main.scm flow (mission triggers). Streaming scripts of shops, food and hairdressers (ammu, junkfud,
barber, tattoo, clothes and others) ask from their buffers and work as usual.

The mission begins when both are in the marker. Yellow plot markers draw radar at contact points
(`BLIP_CONTACT_POINT`), and the trigger only checks where the player is and asks `CanPlayerStartMission`. If
the host is at the point of contact or in the location-team marker, and the partner is not nearby (two have a
meter of stock), the host also receives this question “no”, and the host sends `G_MISSION_WAIT`. The host on
the screen says "Waiting for a partner", the partner - "the host is waiting for you at the mission marker",
and the marker is drawn where the host is. Missions without a marker (call, story event) start as usual.

The host is also waiting for a partner (0.3.3). If the host has reached the mark of his story mission
(`LOCATE_..._2D/3D` of his player with a drawn tag), and the partner is 30-300 meters away from it, the host
game responds to the script “not yet in place” (`ObjectiveAnswer`). The wait lasts until the partner comes
closer to 30 m, but not longer than 45 seconds. It is not there while the mission timer is on the screen: the
wait could exhaust it. A partner beyond 300 m host is not waiting, he is busy with his own. The host says
"Wait for a partner at the target", the partner says "Host is waiting for you at the target" (`G_MISSION_WAIT`
with the last byte 1).

A partner may be the first to arrive. His game tells the host once a second that he is standing in it on foot
(`G_MISSION_WAIT` in the opposite direction, flag `PW_AT`), and the partner says “Wait for <host> at the
mission marker”. If the point is on the street, and the host is free (not in the mission and not in the
cutscene, controls itself and does not stand in the marker), the host appears “a partner is waiting for you at
the mission marker” with the prompt “Y – move to the marker, N – no”. At Y, the host stands at a point next to
the partner, in the middle of it, or 0.9 m from it, if the partner is at the very middle. Next, the mission
launches the usual host trigger. N removes the invitation until the partner gets out of the spot.

While the player is on the mission (`CTheScripts::IsPlayerOnAMission`), the radar does not draw the starting
points of the missions: their letters and markers. The partner doesn't have a mission, and he's seen them
while the host mission was going on. Now, in these two calls (`CRadar::DrawCoordBlip`,
`CRadar::Draw3dMarkers`), the host mission is also considered.

The beginning points themselves (the contact letter on the map and the marker at his house) put and remove the
script of the mission that has passed, that is, the game of the host. Previously, the partner received them
only with the world of the host, and the world is loaded at a quiet moment: on foot, without stars, outside
the mission. While the partner was driving, he had a point of the mission passed, inactive, and there was no
new one, sometimes until the next mission. Now the host checks its contact points once a second (without
houses and saves) and sends them `G_CONTACTS` when changing, and at the same time repeats once every 5
seconds. The partner removes the points that the host does not have, adds the missing ones
(`ADD_SPRITE_BLIP_FOR_CONTACT_POINT`) and exposes them to the appearance of the host, along with the binding
to the interior door.

The mission scene on the game engine (wide stripes or taken control together with the script camera) is
watched by the host camera (`G_SCENE_CAM`). While the host’s camera is moving, it sends its position,
direction and viewing angle 20 times per second, and while it is standing, 4 times. He notes the slake
himself: by the camera flag or by jumping more than 3 m. The end of the scene leaves a reliable message. A
partner takes a scene if he is 100 m from the host and 150 m from the camera, in the same interior and his
camera is free (not his cutscene). His player freezes, and the camera each frame stands where the host camera
is now, with predictions from the last two packets and smoothing. Moving further than 150 m from the host or
200 m from the camera, the scene throws. In the end, the camera goes back behind the back, and the control is
back at the partner. Those who do not watch the scene, 300 meters from the host still lose control with him
(`SET_PLAYER_CONTROL`), so as not to push her participants: in the cemetery in “Los Sepulcros” scene because
of this stood for several minutes.

While the host is on a mission or trigger, the partner repeats the following (`Missions.cpp`):

- Texts `CMessages` (small, large, with numbers), prompts `CHud::SetHelpMessage` and their cleaning, and with
  them and subtitles cutscene, because they print the script mission. Texts lie in a permanent pool, as the
  game stores pointers on them. Text from GXT is also transmitted with a key (hash plus mission table), and
  the partner shows the text of his game: he can have another translation or language, where someone else’s
  line will come out as cranks. The line itself remains a backup option. The host mission table is uploaded by
  the partner to his `TheText`, and the signatures of timers and counters are also drawn from its keys. The
  host's "WASTED" and "BUSTED" (`CPlayerInfo::KillPlayer`, `CGameLogic`) are not transmitted: it is not
  mission text, but the partner's interface keeps the text of this style until its own player resurrects
  (`CHud::ResetWastedText`). The mission melody (`PLAY_MISSION_PASSED_TUNE`) is the same message (`TXT_TUNE`),
  previously played only by the host.
- Replicas of people outside the cutscene (`MissionSpeech.cpp`, `G_MISSION_AUDIO`) The mission uploads a replica
  to one of the mission's four sound slots, ties it to the person who's talking, or puts it on a point and
  loses, and the subtitle comes with the lyrics. The host forwards these calls to its mission scripts
  (`CAudioEngine::PreloadMissionAudio`, `AttachMissionAudioToPed` and neighboring, `SetMissionAudioPosition`,
  `PlayLoadedMissionAudio`, `ClearMissionAudio`), and the partner does the same. The mission's script here is
  not only its stream, but also the streams it launched (`START_NEW_SCRIPT`), with their descendants: "Cesar
  Vialpando" speaks replicas of its scenes in `AUDIOL`, a stream from main.scm, and the partner saw in these
  scenes subtitles without voices. The facial expressions of these replicas (`START_CHAR_FACIAL_TALK`) follow
  the same rule. The stream is recognized by its object and the address of the beginning of the code: the pool
  slot, then occupied by another thread, is no longer considered a mission. The replica sounds from his copy
  of the speaker, so the volume depends on how close he is. A replica of someone who is not a partner (a
  person far away, a phone call), he hears only 50 meters from the host. If the partner's replica is still
  loaded, it will begin when it loads, and after 5 seconds of waiting, it is skipped. Story calls between
  missions are not in the mission, but in main.scm streams (`MOB_LA1`, `MOB_SF` and others: the mission flag
  is raised, there is no mission), and their lines did not go to the partner (28.10). Now, stream `mob_…` is
  being forwarded while the mission is not on its way. Random calls (`MOB_RAN`) are still their own. If the
  partner is talking on the phone himself (task 1600), the host's replicas do not load into his slots.

  The game does not load a replica into the slot where the previous one still sounds (`PreloadMissionAudio`
0x4EC190 checks `IsMissionAudioSampleFinished`), and the partner started the replica later than the host. The
next load of the host, his game silently missed, and the command to “play” sounded the past line: in
conversations in the car, the phrases went in circles, and one word could be repeated all the way (30.10).
Now, before downloading, the slot, which still sounds the past replica of the host, is cleared
(`ClearMissionAudio`).

  The phone removes only the call task, and only when the animation `phone_out` passes 1.57 s out of 2
(`CTaskComplexUseMobilePhone`). Interrupted earlier (fight, button), she left the phone in her hand without
ringing and did not return the weapon, at the player himself and at his copy. Now each player's game removes
the phone from his or her CJ's hand if there is no call task (`CPed__removeCellphone` 0x6348F0: Model 330 is
removed, the weapon from `m_nSavedWeapon` is returned).
- Radar tags created after the mission began. They are placed on the same machines, peds and mission objects
  (Home Invasion boxes: with an arrow above the copy) over the network id. If the partner does not yet have an
  entity, the tag is placed by coordinates and updated until the entity appears. Color, size and icon match.
  Labels at the radar 175 (`CRadar::ms_RadarTrace`, then lie `vec2DRadarOrigin` and text tips `CHud`): before
  the mod read 250 and sent a partner garbage tips like tags with other people's sprites. The game removes the
  mark of the car or peda with them, so before updating, the partner checks that his label is alive (the
  counter in the tag number), otherwise puts it again. The tags of the airfields are set by the game itself
  when the player is on the plane, and they are not transmitted. An indoor tag tied to the entrance
  (`SET_BLIP_ENTRY_EXIT`: Pizzeria counter in "Ryder") is drawn by radar at the street door of this entrance
  while the player is outside. With the tag now comes the entry number in the input pool (both games load the
  inputs the same). My partner used to see her at the coordinates of the interior, far away in Red County.
- Destination markers for "locate" commands (`CTheScripts::HighlightImportantArea`: three red cylinders through
  `CShadows::RenderIndicatorShadow`). Only mission script markers (`m_bIsMission`) leave, not those that the
  host game draws for itself (at tuning workshops, while the host is in the machine). The partner puts them up
  to `CGame::Process`: the 3D marker becomes visible in `CSpecialFX::Update` (`C3dMarkers::Update`) of the
  same frame, and the one put after it is destroyed when rendered without showing (`Render3dMarkers`).
  Previously, the mod put them after processing the frame, and the partner did not see a single red cylinder
  of the mission of the host.
- Racing checkpoints (`CREATE_CHECKPOINT`, array `CTheScripts::ScriptCheckpointArray`) and script spheres
  (`ADD_SPHERE`, `ScriptSphereArray`, `G_CHECKPOINTS`). The partner puts the same ones, and at the end of the
  mission cleans up. The checkpoint stores the direction to the next point (`CCheckpoints::PlaceMarker`
  normalizes the difference), so the partner passes the team a point a step along it, rather than the
  direction itself. Red Circle `DRAW_SPHERE` script draws in each frame while it is needed (at Freddie's door
  in "OG Loc"), through `C3dMarkers::PlaceMarkerSet`. The host sends it along with the location command
  markers (recording of the “sphere” type: center and radius), and the partner draws the same one.
- The weapon that the mission issues to its CJ (the canister in the Tagging Up Turf, the barrels in some
  missions) and takes back (`G_MISSION_WEAPON`). The host sees this by CJ's weapon slots before and after
  mission script commands. Only what the mission gave away is taken from the partner. Cartridges are
  transferred as much as the CJ became, not how much is added: the missions and reduce them (emmet's pistol
  30000 for the training and 10 after it, the can again with 1000 at the end), and the partner remained with
  an infinite supply. The gun of the machine shooter is not transferred: it is issued by `ATTACH_CHAR_TO_CAR`
  (`CPed::AttachPedToEntity`) and takes `DETACH_CHAR_FROM_CAR`, and after these commands the host again
  removes the CJ slots. The partner walked with a helicopter minigan from The Da Nang Thang (28.10). So is the
  turret on the object or motorcycle (`ATTACH_CHAR_TO_OBJECT`, `ATTACH_CHAR_TO_BIKE`): after "Air Raid" the
  partner was left with a minigan with endless rounds (#102).
- When the plot takes away all of CJ's weapons (`REMOVE_ALL_CHAR_WEAPONS`: Tenpenny before the reference to "The
  Green Sabre", the river in "The Da Nang Thang"), the partner 150m from the host loses all of his too
  (`WEAPON_TAKE_ALL`, player selection 02.11). The link takes the CJ to Angel Pine in the same frame, so the
  distance is measured from where the CJ was at the beginning of the frame.
- `CLEAR_AREA` Host Missions (Call `CWorld::ClearExcitingStuffFromArea`) Ordinary empty cars from the place
  where the mission puts its own, the game removes itself, and the partner's car remained there because the
  host has a copy of it - the mission machine. In Ryder, she was put on Ryder's car at a pizzeria, it
  exploded, and the mission failed. The host now sends out copies in this area (`G_VEH_CLEAR`) and owners
  delete their empty machines. Alien cars are cleaned only 60 meters from the point and not in the garages of
  shelters (`CGarages::IsPointWithinHideOutGarage`): missions clean both 500 and 1000 meters, and the partner
  lost cars left across the city and in his garage.
- Script dimming, wide screen, screen timers and counters (block `COnscreenTimer` together with variable
  values). A blackout is considered scripted if it started while the script commands were running
  (intercepting `CRunningScript::Process` on its single call 0x46A21B, not at the beginning of the function:
  our transition at the beginning ended at 0x469F04, where the build plugin put its `call`, and the game
  jumped on E8xxxxxxx, issue #52). Visibility of timers and counters the game counts each frame by their
  variables (`COnscreenTimer::ProcessForDisplay`), and the host mission shoots its counters in the last frame,
  which is no longer transmitted, so at the end of the mission the partner resets the entire block
  (`COnscreenTimer::Init`). The host’s dimming (door entrance, rebirth) is not transmitted to the partner:
  before his screen darkened at every door that the host entered, and the light pulsated. The partner’s screen
  remains dark only as long as the host screen is really dark. If after darkening the script, the screen
  opened the cutscene or door, the partner screen no longer goes out again with each update.
- Entrances to buildings. Missions open and close them for a while (the prologue closes the hairdressers, the
  Ryder opens one for a haircut), and the partner's game does not perform missions. The entry state is stored
  in its flags (`CEntryExit::m_nFlags`, which is also written by the save), and the host sends them out when
  changed (`Entrances.cpp`, `G_ENEX`), except for bits about where the player himself went, and the removal
  bit (`DELETE_ENEX`): it is set by loading its game when the building with the door is unloaded while the
  player is inside, and when this player exits the door is removed. The host beat removed the door of the
  host's house from the partner, and the mark of the house disappeared during the robbery (28.10). Entrance is
  by name and place, not by number: with other mods (extra interiors) and for inputs that the script created
  later, the numbers in the two games diverge.
- Cutscenes. When the host calls `LoadCutsceneData`, each partner loads the same cutscene with the host's
  interior and `LOAD_SCENE` and launches it as soon as it loads. At the end, the partner passes it and calls
  `CLEAR_CUTSCENE`, after which the control and camera return.
- Cutscene skip. The pass button (Enter, space, left mouse button, gamepad cross) does not miss anything, but
  only suggests: its check at `CCutsceneMgr::Update_overlay` (call at 0x5B1947 ) is intercepted. The host
  collects responses (`G_CUTSCENE` with `CS_SKIP_VOTE` and `CS_SKIP_STATE`) and calls `SkipCutscene` when
  everyone who already has a game in the host world agrees: the partner who waits on the menu for the end of
  the prologue does not hold a pass. Esc means “look ahead” and without a reply, the sentence is removed after
  15 seconds. The partner's cutstage ends with the host's cutstage. Along the way, the cutscene is no longer
  missed by itself when the window loses focus: the original check considers the background to be a tap.

In the cutscene, the partner plays a secondary character. Where and what he does, the game of the host decides
on each change of plan (`ScenePartner.cpp`). She sends the seat along with the start time of the plan by the
camera clock (`G_CUTSCENE` with `CS_SPOT`) and the partner puts the character up when his camera reaches that
time. One game decides, because the frames of the two games do not match and the choice of each separately
differed. The host has a remote ped, and the partner has his own player. The selected place belongs to the
first partner, and the next place counts each game itself and the same (`ForRank`), so the network is one
message. The cutscene route (`POSE_WAY`) has its own: `cspath.py --ranks 3` considers it on the same map,
putting the already counted partners people of the stage, and in `cutscene_ways.txt` it lies under the name
`NAME#1`, `NAME#2`. If this is not the case, take the route of the first with a shift of 0.9 m. In the scene
car, the `InCar` gives the remaining available seats (`Spot::seat`, go along with the seat) to the next
teammates, and those who do not have a seat are not in this regard. Next to the CJ, the next one takes the
other side, stands on the floor one step sideways from the first, and if the first one is sitting, it stands
next to it. On the partner's screen, copies of the other partners stand where their own game puts them. The CJ
partner in the cutscene is dressed as a host, and the host's ped is hidden. CJ cutscene (model 1) takes
textures by name from the vocabulary "player", common to model 0, and there lies the last one to wear, that
is, the remote player. Therefore, before loading the cutscene, the model 0 is assembled from the host's
clothing (`CClothes::ConstructPedModel`), and the CJ cutscene itself from the host pedal: the call
`RebuildCutscenePlayer` inside `LoadCutsceneData` (0x4D5EA3) is redirected. The `RebuildCutscenePlayer`
himself first writes down fat and muscles from his game statistics in the description of clothes, and the
model collector always takes muscles from statistics (0x5A42B0). Therefore, the CJ of the host is collected
directly (`ConstructPedModel`), and at the time of assembly of the alien body, the muscle statistics are
temporarily it (`game::MuscleFrom`). The same is going to be the view of a partner at the host.

If there is no texture in the general vocabulary to decorate the CJ cutscenes (chain, watch, glasses, hat,
"extra1"), it is not going to: without material, the assembly fell (#72). The check was shifted to one model:
without the texture of “extra1” (it almost no one wears) the hat was removed, and without “necklace” instead
of shoes, bare feet remained, at the host and at the partner, in all cutscenes (30.10). Jewelry is model 5-9,
and model 4 is shoes.

- What stands around CJ is taken from what is drawn. Triangles of buildings, objects and props of the cutscene
  are laid in the height map with 10 cm increments: the highest obstacle in the cage, seat, floor. Collisions
  for this is not enough: the scenery walls and furniture are often without it, and props (tables, chairs,
  sofas) do not have it at all. The people of the scene do not fall into the map, these are skeletal objects
  of the cutscene.
- Places: dots around the CJ and on the floor that the camera sees; chairs, sofas and steps (flat surface at
  seat height, back, legroom in front); walls behind you to lean against.
- The place is suitable if the character is completely in the frame, does not obscure CJ and other people,
  stands on the floor, does not rest on anything, and the camera sees his head and chest. The wall, facing the
  camera with the reverse side, does not close the view: cutscene cameras often stand behind such a wall. The
  character is no bigger than the people on the stage and doesn't stand between two of them closer to the
  camera than they are. Only those who have the middle of the figure in the frame are counted: the CJ at the
  camera itself, from which one elbow is visible, otherwise it would allow the partner to occupy the entire
  screen.
- All of this is tested for the whole plan, not the first frame: the cams of the scenes pan, and people walk.
  The path of the camera to the next gluing is taken from the spline of the cutscene
  (`TheCamera.m_aPathArray`, Bezier curves between the keys as they are read by `CCam::Process_FlyBy`; the
  keys closer to 32 ms to each other are glued). Where people will go, say the root tracks of their
  animations: the object of the cutscene moves exactly at the displacement of the root. The place is discarded
  if later in the plan someone enters it, the character will overshadow someone or almost all the time will be
  out of the frame or cut off by its edge. Slugs are also on the splice, even if the camera barely moved.
- Cutscene machines are its own collision-free models, and they are animated just like humans. Their outline is
  taken from the drawing, the path to gluing is taken from the animation track, and the partner does not stand
  where the car will pass. If CJ is sitting in the car, the partner is hiding: outside he has nothing to do,
  and you can not put him in the cutscene car.
- Animations of leaning and sitting shift the body back from the pedal point (by 0.5 m at `leanIDLE` and
  `SEAT_idle`, by 0.24 m at `M_smklean_loop`) and the point is put with this margin, otherwise the character
  goes into the wall or into the back of the sofa.
- Classes: standing and listening, smoking with a cigarette in his hand, leaning against the wall, sitting.
- The character is worth the same as CJ in cutscenes. Module `SceneMocap.cpp`, when entering the game, cuts 28
  five-second segments from the animations themselves (`anim\cuts.img`), where the CJ silently stands and
  listens, and the character plays them in a row with a transition of 0.8 seconds (`kClipBlend`). The segments
  are chosen as follows: the root almost does not move, and the body moves no more than the listener. And then
  they're all seen in the game, and they've dropped the phone at the ear, the hand at the mouth, the foot on
  the stand, the support on the rack, the bends and the raised hands. From the segment remain the bones of the
  body without a face. The root stands on the pedal point at the height of normal postures, and on average
  looks at the same place as the pedal. File analysis is in the background stream. The finished clips are
  collected into an ANPK file in memory, and the game itself makes a block `COOPCS`
  (`CAnimManager::LoadAnimFile` from the RenderWare stream in memory). The block is created before the first
  cutscene: when the cutscene ends, the game removes the last block (`RemoveLastAnimFile`) and it must be the
  block of the cutscene itself. Rockstar Mod animations are unlucky. If the clips could not be collected, the
  character stands in the postures of waiting for the game.
- If CJ is in the plan, the character goes with him: 1.7 m behind him on his way and 0.75 m to the side. The
  path is the root track of the animation of the CJ itself, so there's a floor and a passage there. Both games
  consider the path by their own CJ and therefore coincide, and only the “go” pose and the side go away on the
  network. The step is also taken from the cutscenes: the same module cuts out eight cycles of CJ walking in
  two steps, in which the last pose almost coincides with the first. The root in them is in place, and the
  peda moves the mod every frame. The pede is frozen because there is often no collision in the cutscene
  scenery. The pace is adjusted to the speed of the path (`SET_CHAR_ANIM_SPEED`), and when the CJ stops, the
  character gets up and moves to the clips of the rack. Walking is checked for the whole plan, as is the
  place: it does not obscure anyone, does not bump into anyone, and the camera does not approach it closer
  than 1.3 m. The camera often follows the CJ exactly where the partner would walk, and the body projection at
  the camera itself shows nothing. Therefore, in such plans he does not go, but stands.
- The key time in the clip goes from zero in order. In looped animation, the negative frame duration loops
  permanently `CAnimBlendNode::NextKeyFrame` (0x4D04A0), and the game hangs. In cutscene files, a key is
  occasionally found 3 ms back in time: when slicing, it is skipped, and a clip with a disturbed order does
  not enter the block.
- The character lives on top of the pose. In the poses of the game, he occasionally plays a variation of the
  same posture: nods, thinks, shakes his head, carries weight, kneads his leg or shoulders, scratches the back
  of his head, looks at his watch, tightens himself with a cigarette. Variations begin and end in the position
  of the loop (the root moves no more than 6 cm), so the transitions do not twitch.
- The head and shoulders turn to the person who is speaking (`IKChainManager_c::LookAt`) and sometimes for a
  second to someone else. The speaker gives out the jaw: the faces of the cutscene is a bone of 5015, and the
  rest of the people 8. Its movement relative to the head in the last third of a second in the speaker is
  several times more than in the silent. When the line ends, the character nods sometimes. He raises his
  eyebrows to a new voice and chews gum from time to time (facial expressions are performed by the peda’s
  `CTaskComplexFacial` task).
- Variations, looks and gum are selected by the clock of the catscene camera and the place of the character, so
  that the host and partner have a difference of several frames. While the cutscene is running, the `Anims`
  module does not send out waiting animations: the partner both games animate themselves.
- People don't go missing or jump between plans. The character's previous place is retained throughout the
  stage, whether it is visible in the new plan or not (`Stays`): if it does not obscure anyone, does not
  interfere with the scene, does not stand in someone's way, the camera does not rest on him, and the CJ is
  next to him (from 1 to 9 m, on the same floor). Previously, the place was only preserved if the new plan
  showed it at least half the time, and the character moved to where it was visible: from the wall to the
  suite and further to the depths of the room on each change of plan (28.10). The players chose one place on
  the stage. A new place is sought only when the old one is not suitable or the scene has gone: to another
  floor or beyond 25 m (previously enough for the CJ to move away by 9 m). The old place holds up only if the
  new plan shows it at least some part of the time (`SeenShare` is above zero). In "Cleaning the Hood," the
  partner stayed where he wasn't seen, in 8 out of 12 plans (29.10). Now, for such a plan, a place in the
  frame is first sought, and the partner moves there, and not in the place that this plan shows. Why the old
  place didn't fit, writes the host's magazine ("not staying: ..."). The place chosen outside the frame (no
  place in the frame of this plan was not found) does not hold: the next plan first looks for a place in the
  frame, and only finding it, the partner stands there further. In "Cleaning the Hood," the first-plane spot
  was off-screen, and ten consecutive plans were not visible (28.10). Then the place is searched in the frame,
  and if there is no good place in the frame, the place is taken worse, but in the frame: on the edge, half
  behind something or behind people. You can stand behind a person if he is closer to the camera at least a
  meter, and people cover no more than 60% of the figure. Previously, the place was rejected by any person
  with whom the partner intersected in the frame, and in the average plans of the company he was not a place.
  Such a place should be visible at least a third of the plan, and from several take what is seen longer.
  Unless there is one, the character stays in the same place outside the frame or stands next to the CJ where
  the plan does not look, and the next plan, turned that way, finds him there. If the camera for the plan
  bypasses everything around the CJ, he stands in the frame as it turns out: he can cut the edge of the frame
  or half-close the pole, but he still does not obscure anyone.

  If the old place in the new plan is not visible, the new one is now chosen by the nearest one: each meter from
the former place removes from the score (`Rate`) a point. Previously, the best seat was 6m away from the CJ,
and the second CJ crossed across the room in almost every way (31.10). Players chose the option "visible in
every plan, but in short steps." The penalty is valid only if the previous place in the same scene: no further
than 25 m from the CJ and on its floor.

  In the 31.10 test, the second CJ was still missing: in 21 out of 47 plans, he was not visible at all, in the
first cutscene of "Nines and AKs" almost all the time. Strict and loose rules discarded each seat, and he
stayed where he was, out of the frame, or where the plan did not look. Now before that comes the final attempt
in the frame (`SEEN` in `Rate`): a place up to 10 m from the CJ, sideways to someone other than the CJ (up to
a third of the width), or three quarters behind the backs of people, for a moment at someone in the way. Such
a place should be visible at least a fifth of the plan, and from several take what is seen longer. It remains
in its original place only if the new plan shows it at least a fifth of the time: before, one moment was
enough and the rest of the plan was not visible, and half and a half it changed place in almost every plane,
as if teleported (second check 31.10). In the last attempt, he does not lean against the wall (at the door of
the Zero store he stood like a guard) and stands only where you can walk from the CJ: from the CJ on the
obstacle map there is a free floor pour with a margin of 20 cm (`BuildReach`), and the place behind the
counter or behind the wall does not fit. At the Zero store, his last attempt was to put him behind the
counter. The spare seat next to the CJ (`Beside`) is now 2.3m rather than 1.8m: in a cramped store it stood
close to the CJ.

  After the sixth check (01.11), the players said that in some scenes the second CJ stands aside, as if not
involved. Now each meter further than 2 m from the nearest person of the scene (CJ too) removes from the score
0.7 points, and the free space around it is considered only up to 1.5 m: before the assessment grew with every
meter of emptiness around.

  After the eighth check (02.11): he jumped from place to place, and you need a live walk. Now he walks to a new
place near him (`Spot::walk`, `StartGoing`, `Going`). The host lets it in step, when both places are
"standing" or "leaning", on the same floor, no further than 8 m, and on the straight between them on the map
of the frame there is nothing to trip over, there is a floor and there are no people of the scene (`WalkWay`).
The mark goes to the partner along with the seat (`CS_SPOT`, protocol 36). Both games lead him with clips of
walking from the cutscene at a speed of 1.3 m / s on the cutscene clock, so the steps are the same. In the
last 0.4 seconds, it turns as the place stands, and there the action of the place begins. Distant places and
sitting still without walking.

  After checking the 18th build (03.11), the players asked not for random seats, but next to the host: in
"Tagging Up Turf", the second CJ stood at Sweet and Big Smoak 2-5 m from CJ, in ZERO_2 went from wall to wall
and out of frame. User selection: side of CJ. Now, immediately after the trip and walking with the CJ, the
place is searched for at its side (`BesideCj`): level with the CJ at 1.2 or 1.5 m to the right or left, facing
the same place where the CJ looks, and if it is occupied, then 1.1 m to the side and 0.8 m behind. The side is
selected in the foreground of the scene, the one farther away from the people of the scene (it does not stand
between the CJ and the one CJ is speaking to), and holds the whole scene (`g_side`), the other is taken only
on the plan where the first is occupied. The place is checked strictly (there is a place for the body on the
floor of the CJ, no one is standing there, no one is in the camera, no one is shielded and no one will pass
through it for the plan), and if in closeness neither strictly fits, then gently: without the last two
conditions. If it doesn’t work, the old rules work. If it is already no further than 0.8 m from the new side
and no closer than 0.9 m to the CJ, it remains where it stands, without unnecessary steps. Standing at the
side of the CJ, it doesn't go off to the people of the scene in plans where the CJ isn't visible (`Elsewhere`
doesn't work for it): it's next to the host, visible or not. When CJ walks, his partner follows him on his
side.

  Verification 04.11 (Both sides logs, 26 plans): 14 plans did not show him, and in conversation he went from
plan to plan. Now it stays in place as long as it fits (`Stays`) and the CJ is no further than 3.5 m, no
matter how the CJ turns. Previously, the side seat was recounted from the CJ's gaze on each plane, and he
would go for it. If the plan shows the CJ at least a third of the time and the partner is not, the location at
the side or behind the CJ that the plan shows is searched. Such a transition is a leap on the change of plan,
because the new plan does not show the old place, and not more often than once every 6 seconds. On foot, he's
now just following CJ or getting out of the way. Side seats are searched in an arc around the CJ (1.5, 2 and
2.6 m, side-to-back, front-never), not among three points: in the GARAG3A garage they were busy, and the frame
rules drove him around the room at 8 m. With the CJ, it only goes if the CJ for the plan goes at least 2 m,
not 1 m. The jerk at the beginning of the walk came from the direction: until the CJ did not pass 1.7 m, the
partner stood behind him by a random shift of several centimeters or by the look of the CJ and circled him at
a speed of 5 m / s. Now it is the direction of the first half meter of its path (`WalkOff`).

  Stop plan (user selection 04.11) Choosing place plan after plan, mod didn't know what the next ones would
show. But in the cutscene files (`anim/cuts.img`: `.cut`, `.ifp`, `.dat`) everything is known in advance: the
spline cameras and the path of each person go all the way to the end of the scene. As long as the CJ stands at
about one place (not going beyond 3 m and not going 2 m per plan), `BuildPlan` immediately reviews all plans
for this stop. Places are taken from the side and slightly behind the CJ (below) plus where the partner is
already standing. For each place and plan, it is considered how much the plan shows it and whether it does not
interfere (`RateShot`: there is no one in the place, the camera is not in it, in someone’s frame it is no
longer than a quarter of the plan, the wall is checked by the obstacle map). Then the best chain of seats for
the entire stop is selected: the most time in the frame and fewer noticeable transitions. Going through the
change of plan costs 4 per display if the partner is not visible on one side of the glue, and another 8
seconds plus a second per meter if you can see on both sides of the glue. Such transitions are a leap in the
direction of a change of plan. A new stop is counted anew when CJ has reached it, because the walls and floor
are taken from what is painted around it. If there is no chain, the choice of plans works. Run without a game
(`E:\GTA COOP Tests\scripts\analysis\csanim\csplan.py`, the same rules, but no walls), the first version with
seats in a circle: 143 cutscenes, 1346 plans, 5785 with legs. The companion is visible 51% of the time, which
is 72% of the time the CJ is visible. The model has a 31% choice according to plans. Transitions of 1.7 to the
stage instead of 3.2.

  The place where it already stands, goes into plan only on the floor of this stop and no further than 3.5 m
from it. It used to come from anywhere, and even with an increase for not having to cross. In SWEET3A, the CJ
is waiting for its exit under the floor, and the partner remained there for another 28 seconds when the CJ had
already risen, and in INTRO1A was 7 m across the room (check 05.11, partner logs vs. cutscene files). The same
run: further 3.6 m from the CJ 181 s instead of 578 s, on the other floor 2.5 s instead of 109 s, you can see
the same number of transitions 231 instead of 243. The first place of the stage waits for CJ to land: in 12
cutscenes out of 148, the first keys of his track carry it from afar (INTRO1A: 17 m in four frames), and the
seat was chosen by the stairs, 6 m above the room.

  Where the CJ was looking, the mod didn't know before assembly 31. `CCutsceneObject::ProcessControl` moves only
the position (`SetTranslateOnly`), the cutscene object matrix looks the whole scene north, and the body turns
the root of the animation. Therefore, "side of the CJ" (`BesideCj`) was 1.5 m on the X-axis of the map, and
"face to the same" meant face to the north. If the CJ was looking along the X-axis, the partner was standing
right in front of him or behind him: 37s at 1.2m in front of the CJ in the INTRO1A conversation, 64s at 343s
in the five 05.11 test cutscene. The present course is taken from the root keys (`FacingAt`): turning the root
turns the X axis to where the person is looking. On 883 walk sections of all cutscenes, this coincides with
the direction of the stride (0.9 on average). The game stores a quaternion paired to a file (`LoadAnimFile`)
and `FacingAt` reads it from memory.

  With the correct course, the plan locations lie only on the side and slightly behind (`BuildPlan`: 1.5, 2 and
2.6 m, 60° to 150° from the CJ's face, both sides). A moment 50° in front of a CJ closer than 4 m is
considered a moment in someone else’s path (`InFrontOf`), and `Stays` does not leave a partner in front of
him. The transition costs 4 from the display instead of 0.5 s, and visible from both sides of the gluing 8 s
instead of 4 s: at the previous price, he for 100 with INTRO1A changed 9 seats and walked around the CJ. Run
without playing, 143 cutscene: before CJ 1% of the time instead of 22%, side 59% instead of 43%, behind 41%
instead of 35%, transitions 0.9 to the stage instead of 1.6, stops 1.5 instead of 2. You can see him 36% of
the time instead of 52%: he used to be seen just because he was standing in front of the CJ, facing the
camera. If there is no one around, he looks where CJ is, otherwise at the people he's standing with.

  Route to the whole stage (assembly 32). Choosing a place on the change of plan, the mod saw only what was
drawn around the CJ in this frame, and did not know the map: for 2 from the plan it went 8 m, three planes
stood 8-10 m from CJ, and the first place INTRO1A hung 0.4 m above the floor (check 05.11). The game files
have everything in advance: a map of the place (what the IPL arranges, DFF mesh), the paths of CJ and people,
the camera. Scripts in `E:\GTA COOP Tests\scripts\analysis\csanim` count on them the partner's route for each
cutscene: `csworld.py` reads the map, `cssite.py` builds a grid of 10 cm (the height of the floor that stands
on it, where the person will fit, steps up to 32 cm), `cspath.py` counts the route. Partner stands while CJ's
around. When the CJ goes beyond 2.6 m (assembly 34) or already goes there, it follows to the place where the
CJ stops, the shortest way bypassing furniture and people, at a speed of 1.05 m / s, acceleration and braking
of 0.9 m / s2. If the CJ has bypassed him, he calmly turns around. It skips only where the cutscene itself
carries the CJ, or if you don't walk. While the CJ is set (SWEET3A, GROVE1A), it waits where the CJ comes out.
The total is `src/client/features/story/cutscene-partner/cutscene_ways.txt` and sewn into `coop.asi` (resource
`CUTSCENE_WAYS` in `coop.rc`): time, place, course. All games read one text on the clock of their CJ, pose
`POSE_WAY`, only its number is on the network. If there is no route (CJ in the car the whole scene, CJ at the
table on a date), the previous choice of location works. Getting in the car is more important than the route.
No game, 143 cutscenes: the route is 119, 4 m from CJ 3% of the time, in front of him 4%, 1.3 passes to the
stage, 8 jumps for the whole game, the partner is visible 45% of the time when CJ is visible.

  Selections throughout the scene (assembly 33). The place was chosen for the next seconds and only from a few
points on the side of the CJ, so in the conversation of the two partners remained behind the scenes. Now
`cspath.py` evaluates all floor locations 4 m from the CJ path (40 cm grid) every 0.25 s and selects the
entire chain of parking and crossings at once (dynamic programming, 0.5 s step). A second, when it is seen
next to the CJ, is 2, a second in someone else's frame -4, in front of the CJ -3.5, further 3 m from the CJ
-1.2 and more, the transition in the frame -3.5, behind the frame -3 (build weight 35). A man walking straight
at him is worth -30. In plain sight, it only comes out if CJ has left (the same conditions as before) or if
people are on it. Behind the scenes can move at any time: so he manages to the next plan. Partially closed by
another person is considered a fraction of its visible part: over the shoulder it is visible. What is between
the camera and the CJ is not considered a wall: outside the walls are not painted. Transitions with a stop
shorter than 2 seconds are merged into one, a step shorter than 1.5 m is not made. While the CJ is under the
scenery, the partner is waiting outside the frame (-0.8 per second in the frame). Each transition is checked
along the entire length: if it goes through the CJ, through a person or through someone else's frame, it moves
0-4 seconds or bypasses the path of the CJ, and if it interferes anyway and the CJ remains nearby, it is not.
The interior number in the IPL is stored along with the flags (256, 512, 1024), so the lower byte is taken:
otherwise the land of many streets was lost. The interior of the stage is taken from `main.scm`
(`cs_areas.txt`). There's no route on dates so that your partner doesn't stand over the table. In BCRAS1 and
BCRAS2, there is no land under the CJ in the map files, there is the previous choice of location. No game: the
route is at 129 cutscene, the partner is visible next to the CJ 55% of the time when the CJ is visible
(assembly routes 32 by the same yard of 28%), further 4 m from the CJ 1.4% of the time, in front of him 1.9%,
2.2 passes to the stage, jumps 1.

  Check assembly 33 (assemblies 34 and 35, 06.11). On 25 remarks to the first 67 cutscenes. A run without a game, a host log and a partner's log showed such reasons.

  Turn. The course in the parking lot was counted from a point of 2 m in front of the CJ or between the CJ and
the interlocutor. With each CJ move, that point went around the partner: 8 rads one way in SWEET5A, 26 rads
per scene in BCESAR5. Now the parking lot has one course (`mean_heading`): the average direction for people
near the CJ, and if the CJ is one, the same direction where he looks. The partner turns only if the scene has
gone more than 63 degrees for a long time, and not more often than once in 4 seconds. The conversation leads
the head (`Attention`, the course is taken from the current, not the last change of plan). The end of the
transition immediately turns him to a new course. The turn on the spot is in steps (`WayAt`), because without
them he was spinning on his heels. After the jump on the change of plan, the course is put immediately,
otherwise it was already spinning in the frame.

  Proximity. The penalty starts at 2 m from CJ (`W_NEAR`), from 3 m it is stronger, with the first frame partner
next to CJ (`W_START`). Separately fined is another person’s seat (`W_OTHER`: for Truth in GARAG1B, Pulaski’s
in SCRASH2), the wall between him and CJ (`W_WALL`), and a frame without CJ (`W_ALONE`: alien footage in
SYND_4B). If the CJ is behind the scenery, at her door, the partner is standing next to him there and comes in
next (TRUTH_1). Transition in sight and behind the scenes is more expensive: for the sake of the best shot, he
no longer walks in circles (FARL_5A, BCESA5W).

  Cutscene Objects. `CCutsceneMgr::SetupCutsceneToStart` lifts the stage by 1m. For people, this is the height
of the root, and tables, chairs and cars in the calculation lay a meter below and under the floor did not
interfere. Because of this, he was standing in a truck stand in TRUTH_2 and walking into the car in SYND_3A
and BCESAR5. Now objects stand at their height, including where they will arrive at the end of the scene, and
cars that don't drive enter the map. The transition through the CJ, the person or the driving car and a long
round is not done: another start time or an adjacent place (`CONTACT`) is sought, free next 15 seconds and no
further than 3 m from the CJ. If there is no other, and they go to it, the transition is still made: in
SYND_4B CJ otherwise stood in it 9 s. Transitions of one plan go together and can bring it to place ahead of
time. If in the first 4 seconds after coming to this place someone comes, the transition is considered to be
interfering: in TRUTH_1 CJ otherwise 3 s went through it.

  Camera (assembly 35). A place in the camera was considered outside the frame and cost nothing: a depth of less
than 0.5 m fell out of the frame check. In assemblage 33, it stood 328 s, and in SMOKE2A, the 24 s camera was
30 cm away from it and covered the entire frame. Now the `at_camera` is testing the same thing as the
`AtCamera` in the game: the camera is closer than 1.3m and at the height of his body. Such a place is
considered a hindrance, both when choosing a parking lot, and when checking the transition. If you do not walk
away (CESAR1A: along the car, the camera behind the back), it is transferred on the nearest change of plan.

  Together with the CJ (assembly 35). It had previously moved only when the CJ had already gone beyond 2.6m. Now, if the CJ takes more than 1.2 meters (`going`) in 3 seconds, the transition can be started in plain sight, and the transition from 2 meters costs almost nothing (`W_WITH`). A step or two CJ doesn't do that, otherwise it starts roaming again (BCESA5W). Request WOOZI1A and FARL_4A.

  Paul. Catalina's cabin is painted without a floor: the CJ is 90 cm above the ground, and the scenes CAT_3 and CAT_2 were considered to be under the scenery. CJ up to 1.3 m above the map now stands on the floor, which is not in the files, and the floor around it rises to its level (`RAISED`) if there is no painted surface above this place at the height of CJ: on the porch of Sweet the floor does not rise, otherwise the partner would hang above the street. The CAT_3 didn’t have a route either because `cat_3.dat` contained a “7\t” and the camera file was not understood.

  Stand. Two errors. `HoldMoving` took time to `Live`, the clip in `Live` put `checkedAt` later, the difference
came out huge, and the clip just launched was considered non-playing and changed through the frame. The first
clip of the scene, launched into the frame of the production, was not taken in any of the 67 cutscenes, and
after half a second, another one began instead (a jerk at the beginning of each cutscene). The reason is not
found. `Hold` runs the animation frame later, and it doesn't have this error, so `HoldMoving` does the same,
clears the tasks at the beginning and checks the first clip after 150ms.

  The first frame. `Analyse` is waiting for a drawn frame, and the partner's game is waiting for the word of the host, so he appeared already in plain sight. Now, up to this point, the route itself gives the place (`EarlySpot`): both games put it in the frame of the beginning of the cutscene. If the CJ is sitting in the car, the partner for the first frames stands near the route, under blackout, and then sits down.

  The car. The CJ's output was only checked on a change of plan, and in BCRAS2, the 7-c partner sat in an empty car. Now `CjGotOut` watches the CJ's pelvis every 100ms and half a second out of the cabin considers it a change of plan. There is no way out as an animation: the partner is put next to the car.

  Scene animations (`kSceneActs`). Players' requests for separate cutscenes are CAT_3 (`handscower` when the CJ steps away from the door) and W2_ALT (`Coplook_loop`, folded arms). Acts only in the parking lot, goes instead of clips, blocks are loaded in `Prepare`.

  The city in the scenes of OG Loka disappeared only in `/cs`: the script puts `SET_AREA_VISIBLE 3` for STRP4B1, and the table attributed the interior 3 and STRAP4A, which goes on the street. The rest of the scenes with this recording after `LOAD_CUTSCENE` have been checked on `main.scm`, they are correct. The Burger Shot textures in STRAP1A were not found.

  Check assembly 36 two (assembly 37, 06.11). Players watched all 147 cutscenes and sent 10 comments and a request for a smooth walk. The reasons were found on the logs and files of the game. What counts as a mistake, now counts `cscheck.py`: he converges with the remarks (transfer in the middle of the plan at exactly 30 and 10, without a floor at 39.0% in the frame at 13).

  Items in parts. The root track of a table, sofa or chair lies well below the floor, the real place is set by the daughter bones (`chair`, `bench`, `kb_couch04`). The calculation put the whole mess in the root, and the table SYND_2A was 2.4 m under the floor: the partner was in it. Now `parts_at` collects each piece along the bone of her DFF frame (`World.parts`, frame names from the expansion 0x0253F2FE), with a stage shift and a rise of 1 m. At the table and chair, this gives exactly the height of the floor. Items with the number (`CsChair-2`) on the map did not fall at all, their model without the number (`model_of`). The `csstuck.py` had the same error, so it showed zero: the true build digit of 36 was 93 seconds inside the items.

  Slit. Bypassing the floor went through any gap wider than the body, and the partner in RYDER1A started behind the yard fence and passed through a gap of 87 cm. Now the gap is already 90 cm (`WIDE` ) closed, unless the CJ itself goes through it, and spaces near the walls can still be occupied.

  Move in the middle of the plan. Track CJ can jump to change place when it is not in the frame (STRAP2A: 80 s, in terms of one OG Lock). The beginning of the new part of the route was a transfer, and the partner disappeared in plain sight. He now stands until the next change of plan, and if he doesn't, until the end of the stage (`plan_scene`). The post-transition transfer was also waiting for the end of the transition and could go for the change of plan (SWEET2B): now the next shift is taken.

  Half of Catalina's house. The floor of the room is not in the DFF, it is only in the boxes of collision `ce_catshack` (top 63.05 m with the ground 62.2 m). The previous rule raised the floor to CJ level in a 6 by 6m square around it, and the partner hung 55cm above the grass outside. Now the top of such boxes is considered to be the floor (`BOX_FLOORS`, `parse_col`), on the outside it stands on the ground, and on the step rises by CJ.

  To CJ at the end of the scene. The crossing had to be completed before the end of the route, so no one moved in the final seconds. Now the transition to the CJ can be terminated (`going`). If the CJ leaves the stage and the partner is further than 2.6 m, he follows him without regard to the frame (`leaves`), and behind the scenery door, where there is no map, follows the trail of CJ (`walk_after`, BCESAR2).

  See if he's there. The weight of the frame is one for all scenes, and in tight scenes it was more profitable to stand outside the frame: in SWEET4A 0%, in SWEET1C the first minute. If the scene shows it less than a third of the time with CJ or the first time later than 15 seconds (`SEEN_WANTED`, `SEEN_BY`), `plan_seen` considers it still with a frame weight of 2, 3 and 4 times more and takes the best option that does not put it in front of the CJ (+8% of the time), in an alien form (+5%) and in the camera. SMOKE2A is the same as it was when players asked not to close the view. There are scenes where there is no room at all: in FARL_3A close-ups in a cramped room.

  Smoothness. The course followed the keys of the route in jerks up to 5 rad / s. Now `EaseHeading` leads it critically damped spring: in place 0.3 s, on the move 0.15 s, no faster than 2.5 rad / s, the same for any number of frames. The turn on the spot played walking on the spot, now it's the game `PED/Turn_L` and `Turn_R` (the root in them does not turn, the course sets the mod), they go until the course catches up with the route. Entrance to and exit from the step is 0.4 s (`kStepBlend`). In the routes, the corners of the transitions are rounded (`round_path`, only on the free floor), on the move the course is not faster than 2 rad / s (`TURN_WALK`), a turn more than 45 ° is made on the spot before the step (`turn_first`), and at the end of the transition, a turn more than 75 ° is postponed to the parking lot so that it does not go sideways.

  BCRAS1 machine. The head bag (`csheadsack`) is skinned like a human, and took up a seat: 4 seats, 4 people, and the partner sat down only with 15.5 seconds. Man is now a skeleton of 20 bones (`HasBody`, `RpHAnimHierarchy::numNodes`).

  Burger Shot textures. Building `burger01_law` in the IDE is in section `anim`: it needs file `LAw.ifp`. As long as the cutscene is running (`ms_cutsceneProcessing`), the game does not read IFP (`CStreaming::RequestModelStream`). In `/cs`, the player stood far away, the file was not in memory, and the entire scene was visible to the building’s LOD, on both screens and only in 29: the file request passes immediately after it, and the building is already loaded into 32. In a real game, this will happen to a partner if he is far from the host. Now `Prepare` loads the block before the cutscene (`kMapBlocks`, scenes with animated objects nearby).

  Selection of routes. All scenes are counted, but the new route only gets into the file if the scene is in the notes, if the parking is the same, and only the form of the transitions has changed, or if it corrects the error of `cscheck.py` and does not worsen anything. The rest of the scenes were arranged by the players, and their route is the same.

  Figures on 130 scenes, Assembly 36 and Assembly 37, no play: transfer in the middle of the plan in sight of 8 and 0, inside objects 93 c and 1.5 c, in the air 81 c and 2.4 c, stands while the CJ leaves, 125 c and 114 c, seen together with CJ 49% and 51%. The new route of the first partner is taken in 73 scenes.

  The second and third partner are counted again around the new places of the first: with the old routes, two would stand closer to 0.7 m to each other 622 s. What to take in each scene, decides `cspick2.py`. The new route goes to the file if the old one, along with the other two, does not give less cause for complaints. The old one remained in 23 scenes for the second and 27 for the third, among them `CAT_3`, where the new 7.4 hung 80 cm above the ground.

  The same figures for the second and third, assembly 36 and assembly 37: transfer in sight of 7 and 0, 12 and 0, inside objects 25.5 c and 1.5 c, 39.5 c and 3.5 c, in the air 5.8 c and 3.2 c, 30 c and 2.4 s, seen together with CJ 32% and 38%, 29% and 39%. Two closer 0.7 m to each other: 162 s and 146 s.

  Verification of assembly 37. The frame weight of `plan_seen` bought what you couldn’t buy: an adjacent room visible through an open door (`SWEET1C`) and a transfer at each change of plan (`SWEET4A`). Now, along with it, fines for the wall (twice as fast), for transfer, for the face of the CJ and the appearance of someone else are growing. In the Catalina house, only collision boxes are considered gender: the flat ends of the logs of the logs lie 33 cm above the ground. The CJ passes through the wall of the model here, so on the steps the height is taken from his feet in both directions, and you can not stand on them and close to the wall (`s.steps`, `lattice`): the partner remained there 85 cm above the ground when the CJ already came out. `cscheck.py` counts the transfers in sight (`tele`) and the time behind the wall from the CJ (`wall`).

  Figures of the same file before and after this check, no game. The first partner: transfers in sight 15 and 10, behind the wall 254 c and 213 c, visible 51% and 50%, stands while the CJ leaves, 114 c and 122 s. The second and third are recalculated in 44 scenes: transfers in sight 24 and 7, 38 and 9, visible 38% and 37%, 39% and 34%, two closer 0.7 m to each other 146 s and 174 s.

  Second check of assembly 37. What the calculation doesn't know is handwritten: `cshand.py` rules the routes of individual scenes over the calculation and starts after each count. At `SWEET1C`, the open door is in the room from 21 to 66 seconds, the map does not contain it, and the place is behind the sash: now it stands behind the CJ, on the plan through the doorway one step to the side and exits before the CJ, because the door closes behind the CJ. At `CAT_2`, only the ground is painted under the front room of the house, at 63.05 there is one collision box: while the CJ is at the door, the partner is standing outside on the grass and cannot hang. At `SWEET4A`, he's walking off the porch behind everyone at their pace, no transfer. In `TRUTH_2`, the edge of the saucer was considered sex. At `INTRO1A`, he's walking next to the CJ on the right because the camera is behind the CJ. The frame measure of `cscheck.py` does not count people who close a partner: `csspots.py` counts with them, `csview.py` draws a shot from a cutscene camera based on game models.

  Third check on assembly 38. Corrections only in the route file, `coop.asi` the same. Of the 1,155 passes, 514 ended in a reversal: `steer` left more than 1.3 rads for later, and `stand_keys` waited for a second, and the partner stood with his back to the people. Now `settle` in `cshand.py` passes along all routes: the last second of the step goes to turn at a speed of 2 rad / s, the speed by this time falls, so it does not go sideways, and the rest of the turn follows immediately. Only the courses change, the places are the same. After this reversal, more than 1 rad remained 52 of 491. While CJ is not on stage, there is no partner either: at `SWEET6A` to 28.55 s and at `RYDER3A` from 17.1 to 45.88 s, everyone is waiting at the same place where CJ, under the floor. It can only appear on a change of plan, so `RIOT_4C` and `GROVE1A`, where CJ appears in the middle of the plan, are untouched: `csearly.py` shows such scenes. At `TRUTH_2`, the door of the shed is ajar 54 cm, and the crew led the partner through it by a transfer of 10 m: now he enters behind the CJ, stands behind him, goes to the van and waits between the poles. The van from the 34th second is a meter further, this is also taken into account. At `FINAL2A`, the route goes beyond the end of the stage (`cut_at`), so it doesn't stop at the end. `csend.py` shows where the CJ is still walking at the end, and the partner is already standing.

  Assembly 39, protocol 53. Everything that has changed in the first partner is done for the following: in 53 cutscenes, where the first, second and third were counted again around him (`cspar.py --ranks 3 --base`), and in scenes with manual routes written by hand. The old route is left where the new one was in another partner or person (`cspick2.py`). Transfers in plain sight in these scenes are replaced by walking or shifted to an adjacent plan shift where one side is not visible (`move_jump` in `cshand.py`). The route of the first one did not change. Both texts are stitched into `coop.asi` (`RCDATA` in `coop.rc`, `Embedded`, `NextLine`) as a microphone icon. Each game reads a route on its own clock, and it does not go over the network, so everyone should have one: the file can be lost or left from the old version, but the resource does not. The new route is a new assembly and the new `kProtocolVersion`, otherwise the different routes will connect. If there is no resource, the table is empty and the previous location selection works. The old file in folder `coop` does not read the game. `windows.h` in `ScenePartner.cpp` comes with `#undef near`, `far`, `STRICT`: these are the names in this file.

  Seat (`Cabin::Holds`) is no further than a meter from the middle of the car. The open door extends what is
drawn: CJ, pressed to the hood in `PROLOG3`, the mod considered sitting and put a partner in the air at the
other wing. Now the mod believes the CJ is standing next to the car and the partner is walking along the
route. Without the game, comparison with the assembly routes 33 on the same scenes: turns on the spot 102
instead of 358 (82 rad instead of 636), in the camera 11.5 s instead of 328 s, inside objects 0 s instead of
9.5 s, in people 5.5 s instead of 11 s, then 4 m from CJ 2.1% of the time instead of 2.5%, passes 305 instead
of 282, 915 m instead of 851 m (it often goes with CJ). The partner is seen near CJ 49% of the time when CJ is
visible (it was 53%), in front of CJ 2.2% of the time (it was 1.9%), obscures people 0.8% of the time (it was
0.4%): it stands closer to CJ. The route has 130 cutscenes (CAT_3), BCRAS1, BCRAS2 and dates have the same
choice of location.

  Hands and gait (assembly 33). In `Hold`, the teammate and his copy of the hands are empty on the whole stage
(`SET_CURRENT_CHAR_WEAPON` and `CPed::SetCurrentWeapon`: the team from the player only chooses the slot). The
gun comes back from the owner's state until the game begins, so every frame is checked. `ReleaseAll` returns
the weapon to its player, if he has it, the copy will return the owner's condition. The gait is one for all:
the clip `coopwk02` goes by itself 1.06 m / s, this is the speed of routes. Random selection changed her
inside the scene, and three quick clips (up to 1.45 m/s) at a step of 1.05 looked slow.

  Walking now bypasses what stands on a straight line (table, sofa, stage man): if the straight is busy, the
host looks for one turn away from the middle of the path (0.8, 1.4 or 2 m sideways, on a third, half or
two-thirds of the way), both parts of which are free, and takes the shortest path. The pivot point goes to the
partner along with the place (`CS_SPOT`, protocol 40), and both games lead him through it (`GoingAt`,
`GoingWay`). If there is no bypass (the wall between the rooms, another floor, the street and the room), it
moves to a new place on a change of plan, when the camera still shows another.

  While the scene is on, the second CJ is lit as CJ cutscenes (`CPhysical::m_fContactSurfaceBrightness`,
+0x12C). The pedestrian takes his lighting from the ground only when it hits it
(`CPed::ProcessEntityCollision`), and in the car from the car. After the night scenes of The Green Sabre and
the transfer to Angel Pine, the partner stood indoors dark until he got in the car (02.11). Now, at the end of
the scene, he gets his own lighting back.

  If the plan does not show the CJ, but the people of the scene on another floor or further than 8 m from the
CJ, and next to the CJ of the second CJ would not be visible, the place is searched next to them (`Elsewhere`,
`ChooseAmong`). In "Nines and AKs," the CJ stands under the floor of Sweet's house for the first 10 seconds
while the camera shows the others upstairs and all the seats next to him are discarded.

  In the fourth check 31.10 of the second CJ in the first cutscene of "Nines and AKs" was visible only in 2
plans out of 11: in conversation with Sweet all attempts failed five plans in a row, and he stood for half a
minute where the camera did not look. Now, if it was not visible in the previous plan (`g_seenBefore`), after
all the attempts there is another one (`g_lastChance`): the same last one, but the filling of the floor is
with a margin of 10 cm instead of 20, and the place should be visible at least a twentieth of the plan. It is
also found near people (`ChooseAmong`). If the CJ is clamped with furniture and there is no free cage within
1.5 m of it, filling does not limit anything: before in this case, no place of the last attempt was suitable.

- If CJ is sitting in the cutscene car, the partner sits in it in a vacant seat. Cutscene machines are loaded
  with separate models with the prefix "cs" to the name of the transport ("csgreenwood" is Greenwood,
  "cscopcarla92" police car), and there are no seat points in them. Therefore, the mod finds the real model of
  transport by name and takes the number of seats on its doors
  (`CVehicleModelInfo::GetMaximumNumberOfPassengersFromNumberOfDoors`). Doors game counts when the model is
  loaded, so that the model is loaded once and released immediately. A large object of the scene, which is not
  a transport (palm trees in CAS_3, a courtyard in STEAL_5), is no longer considered a car: before the CJ next
  to him “was driving”, and the partner sat in the passenger position in the air. Motorcycles, boats and
  planes don't count either.
- In the car, the CJ is considered only if its pelvis inside the cabin: inside the painted size of the car with
  a margin of 0.3 m on the sides and 0.6 m in front and behind. A CJ at the door or behind the trunk (BCESA5W)
  is not considered a passenger. The people of the scene in the fashion salon consider the same. If there are
  as many seats as there are (four in a four-door SWEET2B car, two in a Bravura in FINAL2B), the partner does
  not sit down, but stands by the car. If the car leaves for the plan, it stays in the same place or out of
  the frame. He was the fifth person to sit in a seat.
- The partner sits on the second place of the CJ row, and if it is occupied, in the other row. Models of
  cutscene cars are rotated by someone, and the hood is longer than the trunk, so another row is searched for
  on the roof: both rows are under it, and the other row lies from the CJ towards the middle of the roof (the
  roof here is all that is drawn 15 cm from the topmost point). The row, taken in the wrong direction in a
  meter, was on the hood. Place occupied if the pelvis of someone from the scene is closer to 0.55 m sideways
  and 0.6 m along. Place is not suitable if the camera is closer than 0.9 m to his body, or if he closes more
  than a third of the CJ or actor from the camera: in SYND_3A, the camera shoots the CJ through the window of
  the empty front seat, and the partner there closed the CJ. If only such places are free, the partner hides
  on this plan, and then returns to his place, rather than transplanting.
- Face partner sits at the front of the car. The cutscene models are turned around, and on the model axis, he
  sat with his back forward in SYND_3A. The front of the mod takes on the shoulders of the CJ (bones 21 and
  31: CJ in the car looks forward), and if the skeleton is outdated, on the hood: from the roof to the edge of
  the car in the direction of the hood further than in the direction of the trunk. In a review of 15 cutscenes
  with machines, both methods coincided everywhere.
- In the car, he stands each frame in her coordinate system with passenger animation `CAR_sitp` and turns his
  head to the one who is speaking (pose `POSE_CAR`, cutscene object number in field `act`). The place is
  considered to be along the pelvis of the CJ and the actors (bone `BONE_PELVIS`), not their root: the root of
  the CJ walks as he turns in a chair to the interlocutor. The skeleton is updated only while it is being
  painted, so the bone further than 1.5 m from the root is considered obsolete, and then the root is taken.
  The skeleton of his partner for landing is not readable: the old skeleton carried him from the car further
  and further with each frame, up to thousands of kilometers. The chosen place is held until the end of the
  cutscene until someone from the stage sits on it. In a review of 15 cutscenes with cars, the partner rides
  in 36 plans (BCRAS1, BCRAS2, SMOKE1B, SMOKE2B, SYND_3A, RIOT_4B and prologue), in full cars SWEET2B and
  FINAL2B is outside, and in the air and on the hood it is no longer.
- If none of the seats fit, the partner stands next to the CJ at the level of his floor, where no one is
  standing, with walls even if the geometry is already there. In the first seconds of the interiors of the
  floor is not yet, and before the partner on these plans, too, hidden. Non-strict seats are searched for up
  to 8 m from CJ, strict up to 6 m: in the crowd around CJ, free space in the frame is only behind it.
- The partner now hides only when the cutscene is hand-marked (DATE6A: a date on a playground without a room
  where he would hang in the void) or on a plan where the camera stands in the only vacant spot in the CJ car.
  In a full review of all 145 CJ cutscenes (1359 plans, 8754s) it was tucked away 14s, only in DATE6A.
  Previously, in one 38 cutscenes of Los Santos, it was 274 s.
- How many partners are visible, the review counts for each plan: the middle of the body in the frame, and there
  is no wall between him and the camera (`SeenShare`, "seen N%" in the log). Of the 8,225 seconds when he
  stands on his feet, he is visible 35% (a review of the rules on seats in the car). The previous count gave
  “out of frame 35%”, but took into account only plans where there was no room for a partner in the frame. He
  did not notice that the chosen place was closed by a wall or that the camera for the plan was leaving him.
  In 13 difficult cutscenes, an honest count was 8% according to the old rules and 37% according to the
  current ones. The rest of the time falls mainly on close-up conversations and cramped rooms, where any place
  in the frame obscures the speaker. On the set in SWEET1A, the camera is a meter or two from the actors, and
  they pass through each vacant seat for 18 seconds.
- The walls between the camera and the character are tested by the collisions of the world. The obstacle map
  stores only the highest point in the cage, and the porch canopy covers everything under it (house in CAT_2,
  Sweet's door).
- The manually assigned seats are placed in `cutscenes.txt` near the routes and are also sewn
  (`CUTSCENE_PLACES`): the line "CATSCENA x y z course pose [animation]" or "CATSCENA hidden." From such a
  cutscene, the fashion takes only these places and also checks them on each plane.

Downloading the cutscene itself drops the player out of the machine (`CCutsceneMgr::LoadCutsceneData_preload`)
and back down the CJ planting the mission script. The partner does not have this script, so the mod remembers
his car and place before downloading and after the cutscene, puts him back if the space is free. The pede, who
is still sitting in the car, first drops the scene and only then puts it in place: the transfer of the sitting
pede moves the whole car.

The character is frozen. So that the game does not consider it falling (fall interrupts animation), it is
marked with standing, and the lighting takes from the CJ: the frozen ped stores the light of the place where
it was before. The animation is re-run after each transfer and if something interrupts it. While the screen is
completely darkened, the game does not update the matrix of the view, so the analysis waits for the first
drawn frame. The partner at the beginning of the cutscene is immediately transferred to its place so that the
collision loads around him. After the cutscene, he returns to himself, and outside the mission, he also
removes the blackout that the cutscene leaves at the end. If the end of one cutscene and the beginning of the
next came in one frame, the first is still removed before the second one loads.

There is a booth (test bot, outside the repository) and `tools/cs_map.py` to check places. Interior numbers
for the stand give `tools/cs_areas.py`: the interiors lie in binary IPLs inside `gta3.img`, and the room is
taken from the nearest objects around the cutscene point (interiors are close: DATE3A and DATE4A diners are 21
m from each other). The booth includes interiors up to `LOAD_SCENE` as mission scripts, otherwise its
buildings won't be loaded. The game saves the first and last frame of each plan, everything that the analysis
saw, the path of the camera and the tracks of people before gluing and the geometry around the CJ, and the
script draws next to the frames a plan from above. In addition, the stand records the camera of each frame,
and the verification script draws the same plan in the Ariane map editor with a silhouette in place of the
partner: so the places are checked with the map collected independently of the game (there you can see the
interior itself without people and the props of the cutscene).

The money that the host earns for the mission is also received by the partner (`G_MISSION_REWARD`).

A partner who connects in the middle of the mission re-enters the mission state (`G_RESYNC`).

Enemies who hate the player also attack the partner (Player 1 type). His death doesn't fail the mission.

The mission of the host often takes it CJ script into or out of the room and immediately closes the door
("Reuniting the Families": to the motel, then to the roof; Big Smoak Palace, Madd Dog's house, dam). The
partner standing next to him stayed outside and couldn't get in. Now, if during the mission, the host was in
another area, and the partner is no further than 40 m from the place where the host was, after 3 seconds, the
partner is transferred to him. Through the door that the partner walks by himself, he manages earlier.
Reviving a host in a hospital is not considered a mission (20 seconds after his death).

The plot progress partner gets a fresh picture of the world of the host. The host notices that the mission is
passed (`CStats::LastMissionPassedName`) and sends the world 30 seconds or immediately after the script
follows: a call after the mission often opens something, such as a rocking cage after Sweet's third mission.
The world used to leave twice, after the mission and after the call, and the partner booted twice. Playing a
host at a quiet moment (not in the mission or in the car) captures the world with the tag "progress", and the
server sends such a picture to partners who are already in the game. The download holds the frame for a second
or two. After each mission it was passed, it fell at random, and players took it for a lag, and when there was
a blackout in front of it, for nothing but a black screen. Now the partner loads the host world at the start
point of the next mission, where the screen already darkens before the mission. If there was no mission in 10
minutes, it loads the world in the first quiet moment, just like before. In both cases, it should be safe: on
foot, alive, not in the cutscene, not in its mission, and not earlier than 5 seconds after the host mission.
He must also manage himself, be busy with nothing (Pedestrian Task 0) and not have a wanted list. The casino
table, the hairdresser's chair, food, exercise machines, basketball and dancing did not raise the mission
flag, and the world loaded right below them, and the chasing stars the host world washed. Between the throws
of basketball, the task is also 0, and on 01.11 the world loaded in the middle of the competition: the ball
and the test of the partner disappeared. Therefore, the load is kept and classes, from which there is a
competition (test basketball, dancing, casino tables, racing), and the competition itself, while the call is
waiting for an answer or is going (`ContestActivity`). While the world is waiting to load or load, the
partner's game tells the host (flags `PW_WORLD` and `PW_LOADING` in `G_MISSION_WAIT`), and the host trigger is
waiting too. The partner, who is in the marker with an unloaded world, waits for up to 10 seconds, and the
on-going load up to 15. The mission started during the boot was in a world that the boot was replacing. Before
loading, the partner saves the profile, place, health, armor, weapons and the car he drove last, if it is not
more than 60 m. The screen goes black in 0.3 seconds. After loading, the partner returns to his place, the car
again stands next to him, and the screen smoothly opens. The game of the host, having received his `G_RESYNC`,
forgets his former peds and machines.

The world the server is storing may not be the world the host is currently playing. The server created from
the game while the host is sitting in the machine takes `world.b` of the previous session, and the host can
download another save. While the host is on the menu, the server keeps the partner waiting (`S_WORLD_NONE`)
and gives him peace only when the host downloads the game. To the partner who got the stored world and those
who played when the host downloaded another save, the server sends the next host upload, not just a “progress”
snapshot.

Side classes start their streams `main.scm`, and not story missions: rocking (`GYM`), racing and driving
school tournament (`TRACE`), hothing (`HOTR`), Kick Start (`KICKS`), Blood Bowl (`BLOODR`), triathlon (`TRI`),
schools of flight, bike and boat (`PSCH`, `BIKES`, `BSCHOO`), Ammu-Nation shooting range (`SHOOT`), freight
(`TRUCKS`) and quarry (`QUARRYS`). `CanPlayerStartMission` responds to these streams as if it were a single
player: the host always has a partner, until the host has a mission. The host isn't waiting for a partner at
their markers. Part-time jobs in cars (stream `R3`: taxi, ambulance, firefighters, police, pimp, train),
hidden races (`ODDVEH`: BMX, NRG-500, Chiliad) and house purchases the game does not check with this issue, so
they have worked for both before. Cesar's races run the same streams as his story missions (`CESAR`,
`BCESAR`), so they stay behind the host. The flow of trips around the country (`TRAINSL`) is also sideways.
Side and Date Flow (`GFAGNT`): The host used to wait for a partner at a friend's door, and the partner didn't
go on dates. Each game does not show a friend to another player, either in the car or in the car. The date tag
(`LOCATE_STOPPED_CHAR_ANY_MEANS_3D`) also works on the passenger, so the two who arrived together had a date
in both games. The general rule shows another game of a person scripting as soon as he gets into the car. A
friend would get in the car in every game, and each player would have two (05.11). Now people streams `GF*`
are labeled `MarkLocal(..., inCarToo)`. A partner without his date does not see a host girl and vice versa.

**Smoak's courier** (`Courier.cpp`, 29.10). "Big Smoke's Cash" is run by two streaming scripts. `BCESAR2` is
held in the afternoon on Mondays and Fridays: a Patriot with six bags of money, a bag flies off the car every
time it is damaged, for a bag of $300. `BCESAR3` goes at night on Wednesdays and Saturdays: a motorcyclist
with a parcel, after his death the parcel lies with a pickup (up to $2,000). Both run stream `MAIN`, and their
`GOSUB` in code `main.scm` asks `CanPlayerStartMission`, so the partner they immediately run out, and the
courier is only the host. He is shown as a host mission: the courier's texts (without a call prompt), his tags
on the radar from the tags that were before it began (`g_courierBlips`), fallen bags as mission items
(`Objects.cpp`, handle in `$33264..$33284`, the car bags are not shown) and parcel as a mission pickup
(`Pickups.cpp`). When the script removes the parcel (`CPickups::RemovePickUp`), the copy of the partner also
disappears. Checks “players nearby” in the courier scripts (`LOCATE_CHAR_ANY_MEANS_*` with `$PLAYER_ACTOR`)
the host counts for the partner: the partner picks up the bags and does not let the courier leave. Each
courier payment (`ADD_SCORE`) is received by partners 300 m from the host (`G_MONEY`). The courier's machines
are only injured by the player (`SET_CAR_ONLY_DAMAGED_BY_PLAYER`) and the battering machine of the partner
counts as a battering ram of the player (`CVehicle::CanVehicleBeDamaged`, 0x6D1280), as well as hits of the
partner from weapons (`AsPlayersHit`). The fashion courier gives the courier people to the partner, like the
rest of the host people. People of other side scripts each game keeps (`Peds.cpp`), because these scripts go
to both, and the courier goes only to the host.

The mission of racing, driving school and flight school is triggered by a sidestream
(`LOAD_AND_LAUNCH_MISSION`) and the mod remembers who launched it (the team puts the mission in the mission
buffer, 0xA7A6A0). It’s not just the missions that raise the “on-mission” flag: TRACE and PSCH first keep it
dimmed, PSAVE1 on the save menu, and the parker and occasional phone calls all the time. Maud remembers who
raised the flag. Such missions and flags for a partner are not the mission of the host: before he received
timers and the prize of the race of the host, a black screen and a hint of a call that he did not have, and
could not start anything of his own.

The very missions of these classes (missions `main.scm` 3-10 and 113-133: games on the console and billiards,
shooting range, gyms, schools, races, part-time jobs, cargo transportation, quarry) mod recognizes by the name
of the script and the host missions for the partner does not count. In the past, a partner had their texts and
rewards (host taxi fees) repeated, and his own classes while they were walking were banned.

The teammate's lesson is only in his game, the second player does not see his markers and timers unless he is
called (below). Money and skills for him remain in the profile of the partner. What the occupation changes in
the world (school, trucking and quarrying as property) will disappear the next time the host world loads.

Two can be in the same school at once: `DSKOOL`, `BSKOOL`, `BOAT` and `DESERT5` go to each of their own and
put the lesson machine at one point. The script machine with a foreign mod player transmits as usual, and a
copy stood inside our lesson machine: they were scattered, they burned and exploded, and the lesson failed
(04.11). `ClearSpaceForMissionEntity` such a copy does not remove (`bIsLocked`). Now, while our player is
going to school (`OwnSchool`), there are no copies of the machines that hold the script of another game for
him (`Vehicles::SetApart`, collision copy see below `F_NO_COLLISION`): removed `m_bIsVisible` and
`m_bUsesCollision`, so it does not hit our car and does not knock down the cones of our lesson. Without
collision, it is not affected by gravity (`CPhysical::ApplyGravity`), and it still follows the states of the
owner. Also not shown are those who sit in it: a copy of the player (`KeepApart`, without a signature
overhead) and the driver of the demonstration race (`NetPed::apart`). The mark is still on the radar.
Everything returns with the end of school, with the player gone, and when the copy becomes ours (`Release`).
Those who did not go to school, see the car and the student as before. The only sign is ours: the story
machine of the host at the school is also not visible to the one who is studying there. The sound of the motor
remains in such a copy. In stadiums and races (U), both games are also played in the same place, this has not
changed and the game has not been tested.

Collisions of a copy of the machine follow the owner's car (`F_NO_COLLISION` in condition,
`SET_CAR_COLLISION`). In Robbing Uncle Sam, while the CJ is on the forklift, the script freezes the van,
removes the collisions and replaces it with a hollow box `FAKE_MULE_COL` into which the crates are driven. My
partner's copy of the box was solid inside a replica van that had yet to collide. The box pushed the van out,
and the boxes on it remained in the air where the van actually was (04.11).

**Joint jobs and races** (`G_SIDE`) Part-time work in the car or race (missions `TAXIODD`, `AMBULAN`,
`FIRETRU`, `COPCAR`, `PIMP`, `FREIGHT`, `BCOUR`, `MTBIKER`, `STUNT`, `CPRACE`, `BURGJB`, `TRUCK`, `TRIA`,
29.10 still `KICKSTA`, `BLOOD`, `QUARRY`, `SHRANGE` and the change of the parking driver) can be done
together. It's still in the game of whoever started it, whether it's a host or a partner. In the first 20
seconds, he sees a box called "Call... for a shared assignment in a taxi?" and can press Y, and the second
player gets a window called "...calls you to a joint assignment in a taxi" with the answer Y or N.
Notifications in the chat call the task the same and do not put a verb in the past tense after the player's
name: Nick does not say who is playing for him ("Nick accepts the invitation"). An unanswered invitation is
rejected after 20 seconds, and rejected immediately by someone who is busy with their mission or the mission
of the host. The owner’s game sends the same thing that the host sends to the partner during the story
mission: texts and hints, radar tags (tags that were before the start of the lesson do not go), destination
markers, checkpoints and spheres, timers and counters (`MissionSource` receives them from the owner). Her men
and cars are visible to the second player and so, the fire on her cars and the damage on her people are also
transmitted, so you can help immediately. When the class is over, the owner sends `SIDE_END` with the money it
brought him, the second player removes his marks and receives the same amount. Levels of part-time work are
received only by the owner: they are issued by the script in his game. Both receive the full award (see paras.
"My own progression." His mission or his occupation with the second player takes him out of someone else's
(`SIDE_LEAVE`), except for his taxi in someone else's (below). Tested by two games in both directions (tests
c05a and c05b): a host taxi with a partner and a partner taxi with a reward host.

The owner immediately agrees (`BoardSide`: 15 seconds, attempt twice per second). He used to get there himself
(27.10). Further 30 m it carries 3 m to the right of the owner. In a taxi, ambulance, fire and police car, in
a race, stunts, on Chiliad, at the courier and at the pimp (the only seat in his girlfriend's car), he gets
the same car next to him and gets behind the wheel (`Players::VehicleBeside`: the same search for a seat as
the partner's bike in the plot). The passenger in the owner's car first drops off next to him. In a robbery
van and in a truck, he sits on a vacant passenger seat, to which the people of the mission do not go
(`Peds::SeatClaimed`). If the owner is on foot, the second one is just standing next to him. The freight train
does not take a passenger, and it does not carry anyone.

Stadium arenas are interiors, and there the consenter carries the same (`StadiumJob`: Kickstart, Blood Ring
and racing). The car that he is placed next to receives his interior (`SET_VEHICLE_AREA_VISIBLE`), otherwise
it would not be visible in the arena. Kickstart starts on foot, and the stadium race puts the player in the
car only after the marker, so try to put the second until the owner sits down (up to 60 seconds). In the
quarry, the second receives a car only when the owner sits in a bulldozer or dump truck, and the same (up to
120 seconds). While the second in his car 300 meters from the owner, it is not tolerated. At the end of the
joint race, the second one leaves the arena at the exit of its script (`LeaveArena`, 2 m from it): there are
no doors at the arena, and the stadium script only displays its player. The stadium marker is a few meters
from the exit, and to stand directly on it would mean to start your race.

**Both work** (`Jobs.cpp`, 27.10: “If a host calls a partner to perform a mission together, they must both do
so”). The script of part-time work is in the game of the owner, and there a copy of the second player on his
car is considered the second player of the task. Intercepting the owner's commands does this in two ways.
Condition `LOCATE_*` of its player (`$PLAYER_ACTOR`) after the team receives the answer “yes” and when the
second one is near (`jobs::Answer`, like billiards). And before a team takes a machine or a player from a
variable, that variable gets a machine or a copy of the second command (`jobs::BeforeCommand`). The task
reissued after the script command would be the second event of the script at the pede in the same frame, and
which one will win is not determined.
- `AMBULAN`. The patient, who was approached by the ambulance of the second (close 10 m, model 416), is
  remembered for her. If the owner's ambulance is nearby, he goes to her as before. Place checks
  (`GET_NUMBER_OF_PASSENGERS` / `GET_MAXIMUM_NUMBER_OF_PASSENGERS 34@`) and `TASK_ENTER_CAR_AS_PASSENGER 77@
  34@` go with her number at `34@`. "He left the patient" (`NOT LOCATE_CHAR_IN_CAR_CHAR_2D $PLAYER 77@ 20`)
  and "stands at the hospital" (`LOCATE_STOPPED_CHAR_IN_CAR_3D` without a crown) are measured on it while the
  patient is sitting in it. The script keeps the patients further than 50 m from the player frozen without
  collision (`FREEZE_CHAR_POSITION_AND_DONT_LOAD_COLLISION`), and this check responds "near" and for the
  second. Delivery, time and money is further considered by the script itself.
- `FIRETRU`. The burning cars are also frozen further than 50 m from the player
  (`LOCATE_CHAR_ANY_MEANS_CAR_3D`), now from the second. The fire that the second extinguishes at its copy is
  also extinguished by the owner (`Fire.cpp`, `FIRE_OUT`), and `IS_SCRIPT_FIRE_EXTINGUISHED` counts it.
- `COPCAR`. The perpetrator goes out to fight when either of the two is near (`LOCATE_CHAR_ANY_MEANS_CHAR_2D`)
  and `TASK_KILL_CHAR_ON_FOOT` gets the one closest to him (`$PLAYER_ACTOR` per team). The script moves the
  stuck car of criminals to the road only when both are further than 90 m. Killed by the second offenders
  count themselves: the script is viewed only by `IS_CHAR_DEAD`, and the damage of the second is transmitted
  through them. Each of the 12 criminals is told to die sitting down (`SET_CHAR_FORCE_DIE_IN_CAR`, 0x2CDB55).
  With this flag, the `CTaskComplexDieInCar` does not brake the car and does not release the body, and the
  machine's AI does not look at whether the driver is alive: the car of the killed driver was driving further,
  and in single-player play (player complaint 08.11, user decision: as with any killed driver). In the
  network, the command is missed (`CriminalsDieSeated` in `Missions.cpp`): the car brakes, the driver's body
  falls out after 2 seconds, passengers remain seated. A copy of the second player's killed does the same
  thing: `TASK_DIE` pedas in the car give the same `CTaskComplexDieInCar`, and a copy also gives the script.
  The copy does not receive the “die sitting” flag, so the peds, which are put on it by other missions
  (`MUSIC3` two, `CASINO5` one), the body of the owner sits, and the copy falls out.
- `BURGJB` (27.10: "Two people clean houses and put things in 1 truck"). Things in houses are picked up by the
  game itself (`CPlayerInfo::FindObjectToSteal`, flag `bIsLiftable` 0x2000 in `CObject` + 0x140) and with the
  thing in hand, the player leaves through the door of the house (`CEntryExit::ProcessStealableObjects`
  carries it to the street area). The script only finds the nearest thing (`GET_CLOSEST_STEALABLE_OBJECT` does
  not change anything in it) and when the player puts it with the entry button 6 m behind the van
  (`GET_OFFSET_FROM_CAR_IN_WORLD_COORDS 50@ 0 -6 0`, squared 3.5 m), adds `48@` and removes the thing. Payment
  at the garage is considered to be `48@` : 20 × N2. In the second player, his game opens the house
  (`ENABLE_BURGLARY_HOUSES`, as `BURGJB` himself), with the thing at the rear doors of the van (`BOXBURG` 609
  closer than 20 m) shows the window "Put the thing in the van?", after the entry button removes the put thing
  and sends `SIDE_LOOT`. The owner's game adds `48@` (`jobs::Stashed`), plays the same sound and chats as many
  things in the van.

  The house is furnished with the grain of its place (`srand` in `Interior_c::Init`), so the furniture and
things in both games are the same. Each item for theft has a record in `g_interiorMan` (`m_Objects[32]` to
0x1C: object, model, interior, place, "stolen"), and the house in the new environment does not put stolen
(`Interior_c::PlaceObject`). The thing that one player picks up (holds it up, and it has moved more than 0.3
meters) goes out of the other’s house (`SIDE_TAKEN` with a model and a place): it becomes invisible, without
collisions, it cannot be lifted, and the record is marked stolen. It cannot be removed: the house removes its
furniture when the player leaves (`Interior_c::Unfurnish`) and the second deletion drops the game.

  Half of the houses for theft are closed for the whole game: at the entrance with the flags `BURGLARY_ACCESS`
(0x1000 ) and 0x400, the constructor `CEntryExit` when loading with a probability of 50% puts the opening
hours 0-0 (never), and `CEntryExit::TransitionStarted` and the mark at the door (`CEntryExitManager::Update`)
look at the clock. It falls in each game in its own way, and the house where the owner entered, the second was
unmarked and did not let (28.10). While the robbery is joint, at such entrances in both games the hours are
0-24 (`OpenAllHouses`), after the end - again 0-0. The exits from the inside are connected to the entrance
(`LinkEntryExit`) and are always 0–24. Once the world is loaded, the inputs are recreated and there is nothing
to return (`jobs::WorldLoaded`).

  House doors appear and disappear with the building (`CEntity::CreateEffects`, `RemoveEffects`), and the clock
falls out again each time it appears. Opened once at the beginning, the doors of the neighboring street and
the street after the robbed house again were unmarked (30.10). Now, while the robbery is joint, the doors are
inspected twice a second, and the open ones are remembered along with the middle of the door: the cell of the
departed door is later taken by another, and only their own are closed.

  `BURGJB` wears a balaclava to his player (`GIVE_PLAYER_CLOTHES_OUTSIDE_SHOP … "BALACLAVA" 17`) and the second
player does not wear it (28.10). Now the game joins it in the same way, on top of its appearance, as the
mission clothes of the host (`BurglaryLook`: its appearance is preserved in `g_ownKeys`, in profile he goes),
and removes after the robbery immediately, even in a van, as `BURGJB` removes it to his player
(`BUILD_PLAYER_MODEL` in the car also works). Previously, the withdrawal was waiting for the second to get out
of the car, and after the cancellation of the robbery, the balaclava remained on it (29.10). The "host has no
mission, its clothes back" check (`RestoreOwnLook` in `PartnerUpdate`) removed the balaclava 8 ms after
putting on while the robbery balaclava is wearing, she is silent (28.10). Garage cache `BURGJB` opens when its
player is 20 m from the garage and the van is there (`LOCATE_CHAR_ANY_MEANS_3D`), and takes the van with this
player behind the wheel (`LOCATE_CHAR_IN_CAR_3D`). Both checks in the script once, and the owner's game
answers "yes" when there is a second player, in the second case in the car (`jobs::Burglary`): the second
brought the van and stood at the closed gate (28.10). Marks and entrance at the door of the house the second
did not receive and with an open clock (28.10): at such a door, his game once a second writes to the magazine
everything that checks the game before the mark and entrance (flags, clock, communication, area,
`ms_exitEnterState`, `ms_bDisabled`, taken control, `CanPlayerStartMission`), the line "Joint burglary, a
house's door by us".

  The occupants of the house (`InteriorGroup_c::SetupHousePeds`: each brain `BURGLAR`, created as `PED_MISSION`
without a script) appeared in both games, and a copy of the stranger stood in the same bed: the host was
forked, one hung in the air. Now, outside the story mission of the host, each game leaves its own
(`Peds::MarkLocal` in interception `CPed::SetCharCreatedBy`, if the peds are from the interior group:
`GetPedsInteriorGroup`). In Home Invasion, the colonel's family is passed on as before.

  Residents’ brains wake up at a full noise scale (`$21172` = 100) or from a shot and wound, and the person who
wakes up calls the police or picks up a weapon (`$7172` = 1). The player's `BURGJB` scale is based only on the
owner's. In the second player, his game is the same (`jobs::Noise`): the volume of sound in the player's place
× 1.6, over 20 accumulates, 1% per frame goes away. This is its own strip `BURG25` (`ShowOwnCounter`, the
scale of the owner from his block of meters is not taken). Full 0.2 seconds or awakened tenant: “the hosts
woke up”, 10 seconds to leave the house, then 3 stars, and came out on time – nothing. Those who wake up in
one house wake up residents of the same house in another (`SIDE_ALARM` with a place, the house is checked by
distance up to 50 m: the interior of each house hangs above itself). The owner fills up a pile of noise `41@`,
the second one has its own.
- `STUNT` (Rings on a bike at a skate park): the ring takes `LOCATE_CHAR_IN_CAR_3D $PLAYER ring 2.5`, only by
  its player, and the second ring does not count (28.10). Now the owner's game answers "yes" and when a copy
  of the second player passes through the ring on a bicycle (`jobs::Stunt`): the ring is counted, time is
  added. His assignment with the rings of the game joining, which his `ODDVEH` runs while he is on BMX, is
  extinguished on the first team (`HookStartNewScript`). Before it, it was hanging among the scripts, and
  `SideUpdate` output from the overall task into the same frame. The script, which stands at
  `TERMINATE_THIS_SCRIPT`, is no longer considered an occupation (28.10). Rings `STUNT` rotates itself, 10°
  each frame (`SET_CHECKPOINT_HEADING`, other scripts do not). The host had a new direction in each update,
  and the game of the second set the ring again four times a second, so it was spinning in jerks (29.10). Now
  the ring, which has changed only the direction, is not recreated, and rotates in the second in the same way,
  at 10° per frame (`TurnMarked`).
- `PIMP` (pimp). The girl is picked up by the player's car in which he started work (`40@`): the player in the
  car is closer than 7 m to her (`LOCATE_CHAR_IN_CAR_CHAR_2D`), the car is standing, and the girl sits in it
  (`TASK_ENTER_CAR_AS_PASSENGER … 40@`). Then the trip to the client goes with this machine and the player in
  it (`IS_CHAR_IN_CAR $PLAYER 40@`, you can get out for 20 seconds), and the second player could not pick up
  the girl (29.10). Now the car of the second, the first to arrive at the girl, becomes for this trip the
  machine of work: `40@` - its copy at the owner, and checks "players in the car" and "player next to the girl
  or client" are answered by the second player (`jobs::Pimp`). The next girl to be picked up by the owner's
  car, `40@` is his again. Checks of the area (where customers appear) remain with the owner.
- `TAXIODD`. Passenger at the taxi is one, and he gets out 88 meters from the player, so there is no common
  passengers: everyone carries their own. His `TAXIODD` second player, started by the 2 key in someone else's
  taxi, intercept the launch (`HookStartNewScript`) no longer extinguishes (`Side::parallel`). The second, who
  was put behind the wheel of his taxi, did not press the 2 key, drove to the owner's passenger, and he sat
  only to the owner (28.10). Now, after 1.5 seconds after getting behind the wheel of a taxi (`TAXI` or
  `CABBIE`), your part-time work begins on your own, as it starts the flow `R3`: `$728` = 1, `PRINT_BIG
  'TAXI_M'` and `LOAD_AND_LAUNCH_MISSION_INTERNAL 121` (`StartOwnTaxi`). While he is walking, the texts, tags
  and timers of the taxi owner are not shown, and the money goes both ways (`SIDE_PAY`). What is received from
  another (`Side::received`) is deducted from one’s earnings, otherwise the money would go in circles. When
  the taxi runs out, the second sends `SIDE_RESYNC`, and the owner sends him all his own again (`ForgetSent`).
  Next to the owner (about 60 m) passers-by creates only his game, so the second one appears when he leaves.
- `KICKSTA` (Kickstart, 29.10). The crown is taken by a check of `LOCATE_CHAR_ANY_MEANS_3D $PLAYER crown 1.5`
  and the next check of `IS_CHAR_IN_MODEL $PLAYER 468`. The owner’s game answers “yes” when the second player
  passes through the crown (`257@` = 10, `jobs::Kickstart`) and the points for it go to the total score. The
  rest of the model checks (the countdown when the player tears off the motorcycle) remain with the owner. The
  corona script draws each frame (`DRAW_CORONA`), and the second one could not see them. Now the owner's game
  sends them along with the task markers (`G_MARKER`, view `AREA_CORONA`: place, size and color; the number is
  taken in place because the team has one for all the crowns), and the game of the second draws them
  themselves (`CCoronas::RegisterCorona`). Markers in the message now up to 40, crown 33. The owner's
  Kickstart and Blood Ring competition is cancelled when the other accepts the invitation: the score is now
  shared.
- `BLOOD` (Blood Ring). The control point (+15 seconds and +300 to the health of the owner's car) is taken by
  one check `LOCATE_CHAR_ANY_MEANS_3D $PLAYER`, and the owner's game answers "yes" to it when the second car
  passes through the point (`jobs::BloodRing`). The key in the arena and the owner's broken car remain with
  the owner.
- Racing in stadiums (8-Track and Dirt Track, `CPRACE` in Arenas 7 and 4) is two-way, like the rest of the
  races: the second rides side by side in his car, and the result remains with the owner.

  A two-man race (0.3.3, `features/story/race`). Previously, the second was driving nearby, but did not compete.
Now the owner's game considers him another rider. It keeps a copy of the second on the route `CPRACE` (arrays
`454@`, `536@`, `618@`, on a circle of `250@` points, circles `224@`, taking radii `322@` – `324@`) and puts
it according to the rule of the script itself: first finished, then more laps, then further along the route,
then closer to the next point. Four times a second, the second gets its seat, its owner's seat, and its next
point (`G_RACE`). His game puts his checkpoint and tag there and draws a spot on the right. Checkpoints and
markers of the owner’s route are no longer sent to him: they show where the owner is going. At the finish
line, the second recognizes his place in the chat. If the race ended earlier (the owner finished and `CPRACE`
closed), the place is counted by who was where.

  Anyone who has not entered the race but is riding with or near the rider (no further than 150 m in the same
area) sees the rider's seat on the right (31.10, Silent X request). The rider's game sends out a spot twice
per second that shows her screen (`229@` + 1 of `256@`, `G_RACE` 3).

- `QUARRY` (career). A copy of the world’s second-biggest bulldozer is a real machine: it pushes the owner’s
  stones and bombs, and the script counts them where they lie. The owner's game counts two checks of his
  player's distance for the second (`jobs::Quarry`). In the "Crash" (`34@` = 5), the stone in the pit is
  counted only if the player is closer to 30 m (for two stones 50 m), now the second. In "Sabotage" (`34@` =
  7), a bomb taken to safety explodes when both the player and the bulldozer are further than 21 meters from
  it, and next to the second it does not explode. The ramming of the bomber dump truck ("Destruction", `34@` =
  16, `91@` ) by the second machine is counted as a battering ram of the player (`QuarryTarget` in
  interception `CVehicle::CanVehicleBeDamaged` ). Cargo transportation and crane remain with the owner. His
  `QUARRYS` in the second, while he is in someone else's career, does not begin (`CanPlayerStartMission` for
  sidestreams). On a marker at the office, he took control and turned off the screen, the mission launch
  immediately ended, and the screen remained black.
- Parker (`valet.scm`, streaming script). The shift is also a joint activity: it is recognized by the stream
  `VALET` and the flag `$7480` (`jobs::ValetShift`). Her texts, tags and parking spot marker go second, like
  other classes. The script drives one guest car at a time (`$43736`) and can be driven away by either of the
  two. When a copy of the second is driving, `GET_DRIVER_OF_CAR` of this car in the owner's game gives it to
  the player, otherwise the car was considered stolen and abandoned. `GET_CAR_CHAR_IS_USING $PLAYER` gives the
  car to the second, and the script's parkers do not take it (`jobs::ValetAfter`). The machine counts itself:
  the check `LOCATE_STOPPED_CAR_3D` is a copy of the car that stood where it was put by the second. Cleaning
  the cars on the way and at the end of the shift, the second car does not touch (`IS_CHAR_IN_CAR` and
  `LOCATE_CHAR_ANY_MEANS_CAR_3D`, `jobs::ValetAnswer`). His `VALET` while he is in someone else’s shift is
  turned off: his check “players at the hotel” (`IS_CHAR_IN_AREA_2D`) answers “no”, otherwise guests and
  parking attendants at the hotel would be in duplicate.
- `SHRANGE` (Ammmu-Nation dashboard). The dash script is triggered as soon as the player enters the room and
  waits for it in the marker (stage `$32280` = 0). Joint shooting becomes with the beginning of the set and
  remains it until the loss, exit on the button or the end of all levels (`jobs::RangeWaits`: waiting shooting
  range for the lesson is not counted). The script puts the player in the middle booth (track 1), two
  opponents shoot on the sides, each track has its targets in 8 parts (`$32388`). In the first two rounds, the
  track wins, which will first smash 3 targets, whoever smashes them. In the third, all three shoot at a
  single mid-range target, and each broken piece goes to the one about whom `IS_CHAR_SHOOTING` says he shot
  (the first one asks the opponent). He puts the owner in the booth, one step behind on the left
  (`BoardSide`), and gives him the owner’s weapon: a pistol, shotgun, Uzi or AK level. His weapon from the
  same cell returns afterward. Its copies of targets (`Props`) become solid, and the shot on the copy goes to
  the owner's game (`G_PROP`, `OP_HIT`: item number, damage, weapon). She hits her part of the target as a
  shot by her player (`RangeHit`, `CObject::ObjectDamage`). Part breaks only if it's now having collisions,
  just like the gunshot from the owner himself. The broken part becomes invisible, and the copy of the second
  hides with it. If the third round within 250 ms after such a breakdown asks `IS_CHAR_SHOOTING`, the owner's
  player "shot" and the opponents did not. Repeated shots of a copy of the second in the game of the target
  owner do not break, otherwise hits would be counted twice. The second shooter, while he is in someone
  else's, does not begin: `$7452` = 1 (the seller does not respond with fire to shots), `$104` = 1 (the latch
  of the flow `SHOOT`, which he removes on the street), and his waiting shooting range ends (`$32296` = -1, as
  when leaving the room). After the joint approach, the second shooter will start only by going outside and
  returning. Rivals remain the people of the owner's game, the second does not see them, and his hits on the
  targets of neighboring tracks are not considered. The launch of his dash while the player is engaged in
  someone else's activity is extinguished on the first team (`HookStartNewScript`). The mission flag `$1636`,
  which `SHOOT` raised before launch, is now lowered: the game used to think it was a mission before
  rebooting.

  The script creates targets without a field (the object has 13, “seen everywhere”), and `Props` considered such
objects to be street objects and did not send them to another player in the dash room: the second shot at an
empty booth (#92). Now the targets of the joint round (`RangeTarget`) are measured by the area of their
player. The agreed, moved to the booth from afar, the game holds in place until the floor of the room appears
below it (no longer than 5 seconds): the floor is loaded several frames later, and the second player fell
through under the first (30.10).

**Delivery tag in freight.** Script `TRUCK` puts the delivery location label at `76@` at the beginning of the
second stage when the player is seated in the tractor, and again when the uncoupled trailer is hooked. If at
the time of landing the trailer is not on the coupling, the second stage begins with the “lost” trailer, and
the hookup puts the label again on top of the live one. The first tag remains on the map forever, gets saved
and with the world of the host goes to the partners. In a single game, this is rare, and two trailers are hit
by the second player's car and his truck: his game puts his in the same place. The mod in front of
`ADD_BLIP_FOR_COORD` script `TRUCK` removes the live label from `76@` (`ScriptCommand`). In a loaded,
missionless world, he removes the tags left over from past games: a tag on coordinates without an icon, sized
4 from `CHANGE_BLIP_SCALE` scripts, at one of 75 delivery points (`102@`, `103@`, `ClearTruckLeftovers`).

**Competitions** (`Contest.cpp`, `G_CONTEST`). Basketball (point test), billiards, blackjack, roulette, wheel
of fortune, club dance, competition at the meeting of lowriders, Kickstart, Blood Ring, racing (with 8-Track
and Dirt Track) and triathlon can be played in competitions. Everyone plays at home, the mod only reads the
result. The activity is visible by stream or side mission: `BBCHAL`, `DANCE` off-mission, `LOWGAME` inside
`LOWR`, `POOL2`, `KICKSTA`, `BLOOD`, `CPRACE` (race number `$1408`), `TRIA` (`$7248`). The scripts of the
casino tables work next to the table (the brains of the objects), so the game at the table is visible by their
own flags: `$36868` for blackjack, `$37760` for the wheel, `$13956` for roulette. Result: Points (`$7696`,
`$21184`, `$4072`, `$33048`), money won or lost at the table (a game without a bet does not count), time to
finish and place. At `CPRACE` the finish is stage `49@` from 13, place `450@`, time from start is `$27840` (in
the race against the clock there is time itself). The `TRIA` finish is his victory melody or "Loser!"
(`RACES_8`) on the line, time from `767@`, place `942@`. At `BLOOD`, the win is `91@` = 1, the time from
showing his timer. Local mission variables are read from `CTheScripts::LocalVariablesForCurrentMission` and
are zeroed out by the mission launch. The challenge is made in the first 20 seconds (U), in the window of a
joint race or triathlon next to Y. Consent binds to the competition the current game of the second player or
the next game started within 3 minutes. Both games send the state of their game, and when both are finished,
each announces the winner. Story dances and lowriders (`MUSIC1`, `SWEET6`) are not counted: they are led by
`Minigames.cpp`.

The scoreboard of the competition (`contest::Board`, draws `Overlay.cpp`) is at the top of the screen: the
lesson, the score of both (who is in front, the green), and while your game has not started, how many 3
minutes left and how to start it. After 10 seconds there is a big victory, defeat or nothing and the score of
both. There used to be a single line, and the time and hint were only in the chat (29.10). The scoreboard is
always in the center and not wider than 40% of the screen, the long hint and name are transferred. At first,
the long scoreboard shifted to the right of the chat column and was not in the center, and then, sandwiched
between the chat column and the same indentation on the right, looked compressed. Now the overlay is drawn
after the chat, and the scoreboard covers the ends of the longest lines of chat while standing. The window in
the center of the screen (call, wait) is under the scoreboard. For test frames, the scoreboard in any state
shows `contest::Preview`.

The challenger to a dance or basketball is transferred to the caller (`Players::GoTo`, room door as `/tp`): to
his place on the dance floor, where the club (`BAR_AMBIENCE`) itself starts the dance when the player is
standing in a marker, or one and a half meters away on the court. Everyone used to play where they were and
there was no other player (27.10). The two on the same marker are dancing at the same point and the copy was
inside their player. While we have `DANCE` and the copy is closer than a meter, it is shown at 1.2 m to the
right of our player (`DanceSide`) and each game sees another to the right of its own. Someone who is already
25 m from the caller, do not tolerate anywhere: the transfer with the ball in his hands knocked the partner
out of the script of the court (01.11). The `BBALL` test starts only with the “Look Back” button with the ball
in hand (`BB_09`, Script button 19). The teammate threw the ball for three minutes, the test never began, and
the competition was canceled (01.11), and on 03.11 the challenger again did not know about this button. Now,
the challenger begins the test as soon as he has the ball in his hands: to the question `BBALL` about the
button 19 (`IS_BUTTON_PRESSED 0 19`) his game answers "pressed" until the test begins (`contest::Answer`, the
answer after the team, like a billiard). The camera doesn't get that button. Another `BBALL` creates the ball
only when its position is not visible on the screen (`NOT IS_POINT_ON_SCREEN`, so that the ball does not
appear in front of the eyes). The second player watched the first player play at the same ring, and he didn't
have the ball until he turned away (03.11: "the ball is not" 20 seconds). In the session, the ball's placement
for the `BBALL` is considered off-screen.

**Billiards together** (`Pool.cpp`, `G_POOL`, Protocol 23). In `POOL2` (Mission 8), there is a two-player mode
from the split-screen game: on the second-down START bet screen (`IS_BUTTON_PRESSED 1 12`) puts `178@` = 1,
the beats go in turn on the pad of `180@`, and the opponent from the bar (`214@`) becomes the second player. A
billiards call (U on the bet screen) now means a game at that table. When the second player accepts, the table
owner's game answers "yes" to this test (`pool::Answer` in interception of teams), and the game goes together
without a bet, and the competition is removed: the winner is called by the game itself (`PL_08` / `PL_09`). If
the bet has already been made or the owner has left the table, the second receives `OP_FAIL` and plays his old
way.

The owner's game hides the opponent from the bar and puts a copy of the second player in his place
(`RemotePlayer::avatar`: a copy takes the place, turn and animation of this pedal). The animation is selected
by `anim::Main`: first the animation of the script, then the strongest on the whole body. `TASK_PLAY_ANIM`
runs the animations of the table (block `POOL`) by name, without a group, and all of them are partial (flag
0x10). Previously, only full-body animation was taken, and the copy stood right with the cue while the
opponent aimed and beat (28.10). 20 times per second, the owner's game sends the second his camera, the
opponent's seat and animation and the objects of the game: objects from `MissionCleanUp` (balls, the
opponent's cue, the aiming cue), except for his cue, which is visible in the hand of the owner's copy. The
game of the second takes him to the bar, puts his CJ in the place of the opponent with the same animation,
creates copies of the balls without collisions and holds the owner's camera (`SET_FIXED_CAMERA_POSITION`, then
each frame `m_vecMoveLinear`, `m_vecTrackLinear`, `m_fFOVNew`). Each frame she sends its own buttons, a
gamepad sign and mouse movement. The subject rotation is read by the command `GET_OBJECT_QUATERNION` only at
the object with the model in the world (`m_pRwObject`): the team takes the model matrix
(`CEntity::GetModellingMatrix`), without the model it is zero, and `CQuaternion::Set` dropped the owner's game
at the beginning of the game (0x0059C3E4). Without the model, the rotation is taken from the direction, and
the object in the second one is not shown, as well as the owner. `SET_OBJECT_QUATERNION` in the second one is
safe: it puts the matrix through `CPlaceable::SetMatrix`, which creates it itself. The view comes once in 50
ms, and the ball during this time rolls up to half a meter: the second each ball smoothly travels from the
shown place to the new in 60 ms (`MoveMirrors`, turn too, with normalization), and the new or moved beyond the
meter is put immediately. Copies stand in the area where the second player himself is now (`CGame::currArea`):
the area from the owner's view is the area of his opponent, and the script person can hold the area of the
street, and the created and "visible" balls in the bar were not drawn (28.10, twice). His opponent, the second
player from the bar (he is held by the script table `POOL` in `1@`), hides wherever he waits: in the pictures
he stood in the frame in the place of the opponent. The second query asks for ball models with priority: a
request without flags lives one streaming update (`CStreaming::PurgeRequestList` at the end of
`CStreaming::Update` shoots everything that the game does not need, the mission and not priority), and there
were no balls for 20 seconds (28.10).

The opponent's cue (`107@`) passes into the hand of a copy of the second player. The hidden opponent lost and
cues: `CTaskSimpleHoldEntity::SetPedPosition` each frame makes the object in the hand as visible as its owner,
and the invisible pede (`PreRender` is not called, bones are not updated) puts the object not in the hand, but
in the point of the pedal. Both had a second player aiming and hitting empty-handed (28.10). Now the owner's
game takes the cue away from the opponent (`DROP_OBJECT`) and gives it copies (`TASK_PICK_UP_OBJECT`: right
hand, no animation, as does `POOL2`). When asked by `POOL2` whether the opponent is holding his cue
(`IS_CHAR_HOLDING_OBJECT 214@ 107@`), she answers “yes” and the script does not take the cue back, and her
animation cue (`POOL_*_O`) still puts on the time of the opponent’s animation. At the end of the game, the cue
returns: the copy drops it, and `POOL2` again gives it to the opponent. In the list of items, this cue is
marked, and the second game holds a copy of it in the hand of his CJ, playing the animation cue from the view
of the table (time goes on at the same speed as the animation of the opponent). Copies of the balls are placed
like other copies of objects (`Props`, `Objects`): the object is removed from the world, receives a matrix
from the quaternion (`Quat::ToMatrix`) and returns (`CWorld::Remove` / `Add`). The owner sends a quaternion of
his matrix (`Quat::FromMatrix`). With `SET_OBJECT_QUATERNION` and a transfer without a new place on the world
lists, the second had balls created and "visible" but none on the table (28.10, three checks). A copy of the
second in the owner and the CJ of the second in itself frozen, and the angle of rotation itself in the matrix
did not fall: CJ beat across the table. The rotation is now placed in the matrix (`CPlaceable::SetHeading`).

`Props.cpp` does not transmit such a copy (`PoolTableCopy`): the owner already owns the cue. The second
player's weapon is hidden (`HIDE_CHAR_WEAPON_FOR_SCRIPTED_CUTSCENE`) as `POOL2` hides it from both players.
Protocol 28.

The second player stands in the place of the opponent, that is, next to the opponent from his bar. The script
of the table of his game (`POOL`, the brain of the object) on F ran his `POOL2` on top of the party of the
owner: his balls, his camera, his animations at the copy (“crooked by animations”, “balls do not beat”,
28.10). While the second one is at the other’s table, the game responds to this script “not next to” at
`LOCATE_CHAR_ON_FOOT_CHAR_3D` (`pool::Answer` now sees commands of all scripts, not just missions), and the
prompt and launch of its game in the second one does not appear. In the course of the second (`180@` = 1), the
owner's game responds to checks on the buttons of the second drop (`IS_BUTTON_PRESSED 1 …` ) and
`IS_PC_USING_JOYPAD` on his pad, and after `GET_POSITION_OF_ANALOGUE_STICKS 1` and `GET_PC_MOUSE_MOVEMENT`
writes on the weekend variables of his sticks and accumulated mouse. If the second leaves in the middle of the
game, his punches pass to the owner's pad, and the opponent from the bar is seen again. Without a table view
for 10 seconds, the second one leaves the game on its own.

During the game, `POOL2` lowers his table by 10 cm (`SET_OBJECT_COORDINATES` tables `163@`, at the end raises
back) and puts the balls on it. At the second table was at the height of the map, and copies of the owner's
balls (both the batch and the batch alone of `Props.cpp`) lay inside the table: created, visible and on the
screen, they were not visible (28-30.10). Now the game, where the `POOL2` is, sends the table position to
everyone at every shift and once a second (`OP_TABLE`). Others put their table of the same model within a
meter, and after 3 seconds without news return it to its former place. As long as the player has his `POOL2`,
his table does not move. `WorldObjects.cpp` billiard tables no longer transmit: the lift of the table at the
end of the game is not a change for him (the subject is again where the card puts it), and the table lowered
could remain lowered.

The second player leaves the game in the same way as `POOL2` lets his two at the end: his CJ is placed on the
floor at the opponent's waiting place (shift from the table 0, -1.8, as `225@`), and the freeze is removed
only when a floor is found under this place (`CWorld::FindGroundZFor3DCoord`, not lower than 2.5 m, wait no
longer than 5 seconds). He used to be let go where the opponent was in the last form of the table, and the
partner fell through the floor of the bar and fell to the ground under the interior (29-30.10, twice). If the
CJ's own area does not match the visible one, it aligns: Interior collisions are loaded across the player's
area (`CColStore::LoadCollision`) rather than the visible one.

The two parties are going for money (0.3.3). In the Two Mode, `POOL2` returns the bet when the second drop
presses START, and in the end pays no one. Previously, the mod was responsible for the second START drop
immediately after the consent of the second player, and the bet could not be chosen. Now the owner selects the
bet on his betting screen and confirms it (`0 16`). Maud responds to this “no” press and the next in the same
START cycle, the second “yes” drop, and the game starts with a bet of `155@`. The bet goes to the second
(`G_POOL` 9). If the second has less money, the bet becomes everything that he has, and the owner learns about
it. At the end, each game withdraws or adds a bet to its player.

While the owner chooses a bet, the second player does not offer his own game table: after agreeing to the
call, he waits (`pool::Expect`), and the check of the place at the table (`LOCATE_CHAR_ON_FOOT_CHAR_3D` script
`POOL`) receives a “no”, as during the game. In the first betting test, the second player in those seconds
started his game at his table. The party together then gave his CJ cue to the owner over the cue of his own
`POOL2`, and that cue disappeared from the world: the task of "holding the object" after finishing with the
object in hand puts him `m_bRemoveFromWorld` (`CTaskSimpleHoldEntity::~CTaskSimpleHoldEntity`). His `POOL2`
fell on his next call to the cue (31.10). If the second player still has his own game, he does not get up to
the other person’s table (`G_POOL` 10), and the second player is hit by the owner. Before you give the cue in
the hand, the mod releases what was in it (`CPed::DropEntityThatThisPedIsHolding` 0x5E0360 without removal).

The `POOL2` line of sight consists of ten small bits (`347@`, `SET_OBJECT_SCALE 0.3`) that are created anew
each frame. When the owner's game sends items, they don't have models yet, and with the "there's a model"
test, they leave unseen: the second aimed without a line (30.10). Now visibility is taken only from the flag
of the object, and the size is in bits 2-7 of the view field in 32 parts (old games read only bits 0-1 and put
copies in `CObject::m_fScale`). The previous batch of `POOL2` leaves on the table temporary objects, if you go
on the bet screen, and removes them only the next batch. When the table was lowered under someone else's
party, a second triangle of balls hung over it. Now, before moving the table, temporary objects 2 m away from
it are removed (`CObject::DeleteAllTempObjectsInArea`), as does `CLEAR_AREA` at the beginning of `POOL2`.

The screen of the host mission (blackout, timers and counters) does not lie on top of the partner’s class:
before the timer of his class was replaced by the host timer, and the passengers of his taxis left in the
dark. The weather that his occupation sets (flight school, boat school) does not rewrite the weather of the
host. Items in the hands of his occupation (whom in billiards) show `Props.cpp`, and the subjects of the plot
mission of the host - `Objects.cpp`. The side mission of the host, which passes the plot step (flight school
is DESERT5, driving school is DSKOOL), also sends partners the world it has discovered.

Purchased house General (`Property.cpp`). When a player buys a pickup truck for sale, streams `BUY_PRO` and
`BUY1` – `BUY3` trigger BUYPRO1 with the house number at `$6940`, and the latter shows the house with the
camera and applies the purchase. The buyer's game notices the launch of BUYPRO1 and sends `G_PROPERTY` with
the number. The Ordinary House (3–31) is another game that uses quietly, like BUYPRO1, but without a camera:
the purchase flag `$2912`, pickup removal, radar save icon, door (`SET_CLOSEST_ENTRY_EXIT_FLAG`) and garage.
Three story purchases (Wang Cars, Zero Store, Verdant Meadows) launch a chain of missions. The host repeats
the purchase of a partner by BUYPRO1 himself, when he is on foot, not indoors and he has no mission, and the
host gives his purchase to the partner in peace. After each download of the world, the partner applies his
purchases again, because the purchase before the snapshot of the world would not hit him. BUYPRO1 is not a
mission for a partner: its texts and wide screen do not repeat. The "on task" flag (`$ONMISSION`) can come
from a save raised when no one lowers it. Keeping it in the house keeps it raised too, but PSAVE1 lowers it a
second after loading. Quick preservation of the build or saving at the time of death leave it forever: mission
icons and save floppy disks disappear, the plot does not start, and the partner sees the host “on the mission”
without end. If a minute after loading the flag is raised, and the mission is not, the mod lowers it. For the
same reason, at the start of the game, the mod removes the retention of scripts if the download went past
`CGame::InitialiseWhenRestarting` (it could be intercepted by another plugin): otherwise, no script was
running.

The dimming of the host and the subsequent opening of the screen, which came to the partner in one pack, lost
the opening: the alpha dimming shifts only at `CGame::Process`, after the messages, and the “dark screen”
check has not yet seen it. The partner's screen remained black until the next load of the world (the host
skipped the station stage in "Wrong Side of the Tracks"). Now the screen is also considered dark, the dimming
of which is only requested. The dimming state of the host sends when changing and every 2 seconds, not every
200 ms.

The partner leaves only the cutscenes of the plot missions of the host. The host's cutscene (date: `DATE6A` at
the bar, buying a house) was played at the partner's side where he stood: CJ in the air on a white background,
with no girlfriend (#100).

The partner who connected in the middle of the cutscene leaves both the loaded, but not yet begun cutscene
(its beginning used to come one thing and was lost), and the beginning with the interior in which it goes.
Voting for a pass does not wait for a partner, whose cutscene never went: after 3 seconds of voting, his game
agrees on its own. The partner in the plane, helicopter or boat, parachute or with a backpack does not load
the cutscene of the host: the loading drops the player from the car
(`CCutsceneMgr::LoadCutsceneData_preload`), and after the cutscene he was in the air or in the water without
it.

`DELETE_CHAR` removes anyone given it (`CTheScripts::RemoveThisPed`). A mission that frees up seats in a
player's car for its people removes its passengers. In "Sweet's Girl," the host rode as a passenger at a
partner's, his CJ was removed, and the host's game fell. Player, own or copy, this team is now just landing.
If the host has left in the middle of the mission and the server remains (dedicated server), the partner
removes the dimming, cutscene, and host tags.

The landed host was out of the car, and Sweet and the girl (host group, `SET_GROUP_MEMBER $44`), whom the
mission puts in places 0 and 1, followed the leader. The host sat down again, the mod sat down the group, and
it was repeated: "Nps did not sit down, but left" (28.10). Now, the team transfers its player, who is sitting
as a passenger, to an empty seat from the end (`HookDeleteChar`: a back seat that the mission does not touch),
and drops off only if there are no seats available. In addition, the player who is travelling as a passenger
with another player gives way to the seat occupied by the person of the mission (`Players::YieldClaimedSeat`,
once in 0.5 seconds): a copy of the person of the other game is already sitting or sitting on that seat
(`Peds::SeatClaimed`), or the person of his game is going to him (task `TASK_ENTER_CAR_AS_PASSENGER` with this
door). The player is transferred to a place that no one has occupied or exits. So missions where a host drives
its car with mission people work when a partner drives the car and the host rides with it. If the mission has
as many people as there are seats (three in Drive-Thru), the host has no place, and he has to sit behind the
wheel himself.

The weapon that the host mission gives to the partner used to wipe out his own type, and when the mission took
him, it disappeared. Now his partner's weapon returns, and if the mission left him fewer rounds, there's more
left. The plot purchase of the host (BUYPRO1 for Wang Cars, Zero store, Verdant Meadows) sends the partners a
new world, like a past mission. Preservation homes and homes sold on radar are also contact points, but they
are no longer considered to be mod mission markers: a host standing by such a house is waiting for no one.

The mission often transports the CJ immediately after the cutscene, sometimes for miles ("Smoke1B",
"Synd_2B"), and the partner after the scene was returned to his former place. Now the host reports such a
transfer (`CS_FOLLOW` to `G_CUTSCENE`) and the partner stands next to it and enters the interior through the
same door. The distance to the host is not important: in 0.3.0, only the partner was 60 m from the host (when
changing rooms) or 300 m, and he remained out of town after The Green Sabre, in the Reuniting the Families
motel and outside the aircraft at Stowaway. The hold there is room 9, where the script carries the host
motorcycle command `SET_CAR_COORDINATES`: it moves only the machine, the CJ matrix catches up with it during
processing, and `HookScriptProcess` did not see the transfer, so that the teared partner remained on the lane
(#106, disassembled 04.11). In the room is now considered and the transfer of the car in which the CJ sits. A
partner who sits with a host does not remove such a transfer from the motorcycle: while the host is driving,
he is driven by a car (`Players::ApplyState` changes the area with it). The parachute in the plane is one, and
the mission asks only if the host has one: in the mission, the host who took it parachutes both (Pickups.cpp).
Only his own occupation, shop or mini-game (`CTheScripts::bMiniGameInProgress`) and death can not tolerate a
partner, and each such refusal is written in the log. During the mission, host `/tp` also lets you into the
host room. If the mission has transferred the host to a car or plane (at the end of Freefall CJ is already in
the air), the partner sits down with him as soon as he has a copy of this machine (up to 3 seconds). It used
to be placed next to the host, and it fell in the air.

Usually, `CS_FOLLOW` leaves when the host is back in control. If the scene on the engine is immediately in a
new place, and the CJ of the host is on the ground or sitting in a ground car, the message leaves at the
beginning of the scene. Otherwise, the partner, who remained in the same place, did not see the scene. In
INTRO1, the mission takes the host 1.6 km, and after the INTRO1B cutscene, there is a 19-second scene ("Ah
shit, here we go again"), with the partner standing on Grove Street with his camera all the time.

The transfer is shorter than 150 m (but longer than 30 m) also goes to `CS_FOLLOW` if the partner was 60 m
from the previous place of the host or if before the transfer the CJ was attached to the machine
(`m_pAttachedTo`). In The Da Nang Thang, the script places the CJ out of the water on the deck of the ship
(about 50m up and sideways), and after a helicopter crash drops it into the water near the ship. The partner
stayed in the water or in the city (28.10).

The scene on the engine, which began immediately after the transfer of the partner to the host, the host can
end in another place, leaving by car: in Fender Ketchup, he leaves the garage and closes it, and the partner
stayed inside (30.10). The trip is not a script transfer, and the second `CS_FOLLOW` is not. Now, if the scene
began no later than 2 seconds after the transfer, and the host to its end is further than 10 m, the partner
stands next to him: the car on the right is 2.5 m, the pedestrian is 1.5 m.

Missions with typewriters on radio control keep the CJ in frozen transport outside the world: in the New Model
Army (`ZERO4`) it is a BMX in the air in area 10, in the `ZERO2` van under the Zero store.
`FREEZE_CAR_POSITION` puts `bDontApplySpeed` (+ 0x40, 0x2000), the game itself does not freeze. The transfer
there dropped a partner in the bay with 4 stars of the closed city (30-31.10). While the host CJ is sitting in
such a transport, `CS_FOLLOW` is waiting, the /tp request is refused, and /tphere is not executed.

Mission tracing restrictions (`SET_MAX_WANTED_LEVEL`, police not intervening, `SET_WANTED_MULTIPLIER`) apply
to the partner while the mission is underway (`G_MISSION_POLICY`), but only near the host. About 30 missions
put a multiplier or a maximum of 0, and many shoot stars (`CLEAR_WANTED_LEVEL`), and the partner at the other
end of the map was not wanted while the host mission was on (04.11). Further than 150 m from the host or on
the street, when the host is indoors, he has his own rules of search, and the removal of stars by the mission
does not affect him (`KeepOwnRulesFar`, `FarFromHost`). War zones stay on the host. After the mission, he has
his own again. With them come the density of people and machines (`SET_PED_DENSITY_MULTIPLIER`,
`SET_CAR_DENSITY_MULTIPLIER`: "Home Invasion" puts 0 in the house, and the partner's game filled the house
with its people while the host carried out the box), the level of open cities (stat 181, `STAT_CITY_UNLOCKED`)
and the reset of the wanted mission (`CLEAR_WANTED_LEVEL`), and war zones (`SET_DISABLE_MILITARY_ZONES`,
`CCullZones::bMilitaryZonesDisabled`): "Black Project" turns them off to its player, and the partner in Area
69 `CCullZones::Update` gave 5 stars, and it was impossible to pass the mission together quietly (#106). Stars
that the mission gives CJ (`ALTER_WANTED_LEVEL`, `ALTER_WANTED_LEVEL_NO_DROP`), and receives a partner 150
meters from the host: previously they received only the host (#100). The same value goes away no more than
once per second: The Da Nang Thang repeats the command every frame of the entire mission, and it was 190
reliable messages per second. The partner's world is older than the end of "The Green Sabre": without the open
suburbs, he received 4 stars for forbidden territory
(`CGameLogic::SetPlayerWantedLevelForForbiddenTerritories`), and with them the new host world never loaded.
The world of the host, which waits only because of the stars, now loads in a minute anyway, and what prevents
the loading, the log writes once in 30 seconds. During a gang war, the game doesn't give stars for any crime
(`CWanted::ReportCrimeNow` asks `CGangWars::GangWarFightingGoingOn`). Host warfare is only for the host. The
host reports it (`G_GANG_WAR`, on a change and every 3 seconds), and the partner's game 300 meters from the
host considers the war to be his own wanted. In the past, a partner was awarded a star for every murder in a
host war (#56). His partner's war (see para. "Gang Territory" is in his game, and he doesn't get any stars for
it. The host game marks the attackers in the host war on radar (`CGangWars::StartOffensiveGangWar`,
`CPed::bPartOfAttackWave`, + 0x474 bits 12), while the partner did not have tags (#100). This feature is in
the pedal state (`X_WAVE`), and the copy of the attacker receives the same mark while alive and while the sign
is standing.

The partner remembers his density of people and machines at the first rule of the host mission, and it comes
while the partner has the first cutscene of the mission. The Katscene keeps both densities at zero, and the
real ones are stored at `CCutsceneMgr::m_fPrevPedDensity` and `m_fPrevCarDensity`. The remembered zero
returned at the end of the host mission, and the partner had no people or machines until the world rebooted
(30.10). Now, when the cutscene is loaded, the values stored by it are taken, and its rule returns after the
cutscene is removed at the end of the mission.

Most story missions put densities at 0 for the location of their scenes, some for most of the mission. The
density is one across the map, and the partner traveled through empty streets away from the host until he
returned to it (04.11). Now on the street further than 150 m from the host or when the host in the room at the
partner have their own densities (`KeepOwnRulesFar`). 150 m is a little further than how many copies of people
(120 m): its population does not fall into the scenes of the host. Indoor host rule always remains ("home
invasion"). Densities are written only when changing places, so your partner's classes do not lose their
values. As long as the cutscene is loaded, they don't move.

Cutscene subtitles are taken from the mission text table: from `CS_LOAD` comes the name of the host table, and
the partner downloads it (`CText::LoadMissionText`). A copy of the cutscene from the partner prints its own
lines, and the host string comes also over the network along with its moment in the cutscene (`G_TEXT`, view
`TXT_CS_LINE`). The partner shows the host string only when his copy passed this moment by 0.3 seconds and did
not type such a line: his copy can go seconds later (in the 6 s test). In 0.2.7, the host of the cutscene line
did not forward it so that it did not go twice, and the partner who did not find the key (another translation)
was left without subtitles (#55). Blackouts of host scripts that are not mission are no longer sent to the
partner.

Mission commands are seen by the host where `CRunningScript::Process` is called by the command handler from
the table (0x469FF2:`push ecx; mov ecx, esi; call edx`, the call goes through the stub
`CoopScriptCommandStub`). Up to 0.3.1, the mod intercepted `ProcessOneCommand` (0x469EB0), but no one calls it
in 1.0 US: its code is built into `Process`. That interception saw only the commands of the fashion itself
(`Cmd` performs them through it), and everything that was kept on the mission commands was silent: map edits,
freezing of the partner's car, resetting of the wanted list, clothes and Home Invasion drawers. The CLEO only
changes the address of the table (0x469FEE).

The people of the stage move like a host. Animation files loaded by the mission (`REQUEST_ANIMATION`) are also
stored by the partner (`G_MISSION_IFP`): earlier, the gesture per second ran out before the file could be
loaded. Facial Conversation (`START_CHAR_FACIAL_TALK`, `STOP_CHAR_FACIAL_TALK`) is repeated on a copy of the
speaker (`G_MISSION_FACE`). The mod looks for the landing and landing of its player in the whole task tree: in
the scene it is the step of the sequence of the script, and the copy used to be on the seat without the
animation of the door. For the same reason, walking on a task (the copy comes from the joystick that the mod
substitutes) the mod recognizes by the simplest active task, and not only by the top: in the scene, a copy of
the host slid to the van standing. The head turns of the scene (`TASK_LOOK_AT_CHAR`) are repeated on copies
(`G_MISSION_LOOK_AT`). Animations of the person who gets in or out of the car don’t go through copies: the
copy makes it a task, and the animations of the door went twice (Rider from the van). A person whom the scene
moved further than 2 m, while he has animation with his whole body, the copy carries immediately, and does not
pull after him.

The replica got into the car before its owner: missions force their men to walk (`SET_NEXT_DESIRED_MOVE_STATE
4` in front of `TASK_ENTER_CAR_AS_PASSENGER`), and the copy task runs. The mode considered the sitting copy in
the state of the owner to be thrown out of the car and carried out outside, and when the owner sat down, he
transferred it back to the seat: “They first sit down, then teleport” (ambulance, Drive-Thru, 28.10). Now the
copy that is already sitting in the car, where its owner sits, and sits. Passenger at any seat
(`TASK_ENTER_CAR_AS_PASSENGER … -1`: sick ambulance) the game chooses the door only at the car, and the mod
sent the place 0, and the copy went to the front door, and then moved back. Now, the passenger is not
transferred until the owner has a door (`m_TargetDoor` or the door of the approach task): a copy follows him,
and sits in his place.

Copy models are requested from `KEEP_IN_MEMORY` (`RequestCopyModel`) as the game streams its machines and
people. Every frame `CStreaming::StreamVehiclesAndPeds` releases models of the police, FBI and army that are
not required by our wanted list, and for an unloaded model it is a withdrawal of the request, and
`PRIORITY_REQUEST` does not save from it. The "Robbing Uncle Sam" soldier was not seen at all by his partner,
their Patriots driving without drivers (disassembled on 04.11). The flag `MISSION_REQUIRED` is not suitable
here: it would have to be removed, and you can remove it from the script that requested the same model.
`KEEP_IN_MEMORY` is not necessary to remove, the downloaded model becomes normal.

The partner requests a human mission model with a priority (`PRIORITY_REQUEST`): a request without flags lives
one streaming update (`CStreaming::PurgeRequestList`), and a model that the partner does not have anywhere
else had to wait for luck. The drug dealer in Clearing the Hood (`BMYDRUG`) was not seen by his partner for
almost a minute (28.10). Also now requested is a script machine model (`VehicleSpawn::script`) and a human
mission weapon: in Running Dog, the partner did not have a vagos, and in Mike Toreno, a van with Mike and
bikers (people in the car wait for their car to appear). Passers-by and regular cars go without priority.

If the mission allows a person to pass through the nearest object of the model
(`SET_CHAR_USES_COLLISION_CLOSEST_OBJECT_OF_TYPE`: Colonel "Home Invasion" lies in his bed), the copy gets the
same (`G_PED_NO_COLLIDE`) as soon as it appears: the bed pushed it out, and a copy hung over the bed. The
person the mission pinned to the machine (`ATTACH_CHAR_TO_CAR`: the girl at the lowrider meeting sits next to
the CJ), the copy attaches to the copy of the machine the same way (`G_PED_ATTACH`) when both are in place and
the copy has already left the seat. While it is attached, the machine puts its position, and the animations go
as usual. It used to be inside a car, pushed out and thrown into the air. The game attaches a copy of the
player (CJ of the host as an arrow on a helicopter or in a car) for the duration of the team as an ordinary
person (`m_nPedType` 4). For the player, `CPed::AttachPedToEntity` switches the camera of his game to shooter
mode (`CCamera::SetNewPlayerWeaponMode`), and the partner has a host sight ("The Da Nang Thang", "Reuniting
the Families", 28.10).

When the CJ host shoots the mission (the team has a weapon: the minigan in the door of the helicopter "The Da
Nang Thang", the machine in "Reuniting the Families"), the partner becomes the second shooter (`GunnerUpdate`
in `Missions.cpp`, 0.3.3). His CJ is attached to a copy of the same machine with the same weapon, on the other
hand: sides 1 and 3 are switched, the shift of x changes the sign, and the place in the middle (x about 0) is
shifted by 0.7 m to the right. He has his own camera, shooter. The attachment goes to the host by the same
`G_PED_ATTACH`, and the host game attaches a copy of the partner. When the host is removed, the partner is
removed immediately, before the transfer of `CS_FOLLOW`, which follows. The weapon of the machine is taken
away, the weapon of the same slot and the weapon in his hands are returned, and his own is written in profile
for this time. The landing key removes the partner to the end of this part and returns to where he was
standing like a Zero machine gun. While the CJ host shooter, his car remains online at any distance
(`ScanTraffic`): "The Da Nang Thang" in one frame carries the helicopter at 840 m, a copy of the partner was
removed directly below him, and the new received a different number, and the second shooter on it no longer
sat down (04.11). If the machine still disappeared under it, the game resets `m_pAttachedTo` without
disconnection: collisions remain off, so there is no gravity, and the shooter's state is not removed. It was
hanging in the air and couldn't walk. `GunnerOff` now causes `CPed::DettachPedFromEntity` itself, which brings
back collisions. `CPed::AttachPedToEntity` gives the machine's weapon, whether its model is in memory or not,
and the weapon animation unit (the machine gun has `flame`, `default.ide`) comes only with the model. Shooting
without him dropped the partner's game at `CAnimManager::BlendAnimation` 25 ms after landing (05.11, output by
code and log, `crash.log` no). Therefore, the landing of your CJ and a copy of the host is waiting for the
model (`GunModelReady`), like the Zero turret. After the scene, the partner is no longer placed next to the
host, if he is on a machine gun or flying.

If the person is not yet mutual (created and attached in one frame away from the partner: Johnny on the hood
in Fender Ketchup), there is nothing to send, and before that it was lost: a copy of the partner ran after the
car, went into it or stood on the roof (30.10). Now the host remembers such people while they are on the
machine, and sends the mount as soon as the person becomes common. If a person disembarks without
`DETACH_CHAR_FROM_CAR` (at the end of that mission, `TASK_DIVE_FROM_ATTACHMENT_AND_GET_UP` is filmed), the
disconnection goes away.

The mission of the host changes the map in its game: opens the gates of Chinatown (“Mountain Cloud Boys”), the
safe of the bank (“Small Town Bank”), changes the construction (“Deconstruction”) and the factory (“Yay
Ka-Boom-Boom”). The same commands (`SET_USES_COLLISION_OF_CLOSEST_OBJECT_OF_TYPE`,
`SET_VISIBILITY_OF_CLOSEST_OBJECT_OF_TYPE`, `SWAP_NEAREST_BUILDING_MODEL`) are executed by the partner
(`G_MAP_EDIT`) when he is closer to 150 m. He who has gone beyond 300 m performs them again: the map is
unloaded in the distance and returned the way it was built.

Airport Gateway (`WS_APGATE`) puts main.scm `APCHECK` stream, and each game has its own. As long as the player
of this game is closer to 65 m to the goal, the flaps are controlled by `APGATE`. The flap opens when the
player is 10m away from it (only with a flight skill of 180 outside) or approached from the inside, and closes
when he is 20m away. Each game opened the gates only to its player, so the host had them open, and the partner
closed, and vice versa (29.10). Now these checks count another player (`AirportGateAnswer`), and the hint of a
license at the gate outside is left only to its own. Mission host Mike Toreno (`DRIV3`) raises `$7200`, and
`APCHECK` immediately opens the main gate of San Fierro Airport, with the guard lying dead. The teammate
didn’t raise that flag, and now his game keeps `$7200` raised while the host has `DRIV3`. The gates, which the
player is 65 meters away, are still closed and a copy of the other player passes through them.

Parts of the map that the mission loads or removes (`REQUEST_IPL`, `REMOVE_IPL`, `REMOVE_IPL_DISCREETLY`:
cannabis field in Are You Going To San Fierro?, `TRUTHSFARM`) are loaded and removed from the partner
(`G_IPL`). He used to have an empty field, and his fire didn't burn anything. Three calls `CIplStore` from the
script (0x46B6ED, 0x46B715, 0x46B73D) are intercepted, the partner's game searches for the same part by name
(`CIplStore::FindIplSlot`). After resynchronization, the host sends them back.

The mission moves, turns, or freezes the CJ's vehicle and is driven by its partner. The host has a copy, and
the owner's fortunes immediately canceled the command ("Home Invasion" freezes the van at the warehouse). Now
`SET_CAR_COORDINATES`, `SET_CAR_HEADING` and `FREEZE_CAR_POSITION` for such a car go into the game of its
driver (`G_VEH_SCRIPT`).

The mission asks only about its player: who hit its people, who shoots, who is aiming, who approached the
thing. Analysis of scripts of all missions 04.11 showed that the partner in all this was not counted
(`features/story/mission-rules`, mission list in parsing `missions.md`). Now, this line is asked again for a
copy of each other: the handler of the same command starts again, and `$PLAYER_ACTOR` at this time is equal to
a copy, so the rules of the game are decided. There can be as many players as you want. For all missions,
`HAS_CHAR_BEEN_DAMAGED_BY_CHAR`, `HAS_CAR_BEEN_DAMAGED_BY_CHAR`, `IS_CHAR_SHOOTING`, and
`IS_CHAR_SHOOTING_IN_AREA` work with the mission player. `IS_PLAYER_TARGETTING_CHAR` looks at the target
reported by the partner's game (`G_PLAYER_AIM`): `m_pTargetedObject` or the target at the mouse's sight. The
individual rows are in Table `kSpots` with the address `main.scm` and the command number: in the changed
mission, the address will not match, and it will remain as it is. The table includes only rows, after which
the script does not move the CJ and does not take control: such a row would do this with the host, wherever it
was. The Ice Cold Killa phone, Interdiction smuggling, Green Goo bank, and Black Project gates are taken by
the partner. Guards of the aircraft carrier in Vertical Bird also notice the partner
(`HAS_CHAR_SPOTTED_CHAR_IN_FRONT`, four lines: the script changes only the states of the guards). The package
in T-Bone Mendez and the booths in Deconstruction are more complex, where the entire piece of script goes for
that player (`Stretch`): for the duration of these commands, `$PLAYER_ACTOR` is equal to his copy. In
"Deconstruction," players in machines are checked in turn, one pass each.

The `DONT_REMOVE_OBJECT` command removes the item from the mission list (`CTheScripts::MissionCleanUp`) and
`HostScan` reads the list. Objects created and shot in one frame (deconstruction, Small Town Bank doors) did
not reach the partner. This item is now announced immediately (`AnnounceKeptObject`). `main.scm` is also
announced before `ROTATE_OBJECT`, `SET_OBJECT_HEADING`, `SET_OBJECT_ROTATION`: the ship's door in The Da Nang
Thang was opened only at the host.

Dam and Blast charges, the Amphibious Assault bug, the Black Project panels, and the Cop Wheels motorcycles
are similar: a piece of script goes for the partner, and the `SET_PLAYER_CONTROL` inside it is skipped so that
the host does not lose control. The landing key is the partner's game telling the host (`G_PLAYER_AIM`)
`ATTACH_CAR_TO_CAR` of the machine the partner is driving goes into his game. In Catalyst, the partner is on
the train: each game has its own car, so `G_PED_ATTACH` only speaks “on the train”, and the partner’s game
looks for his car next to the copy of the host. The fire accumulates throw power, and the box creates a host
game (`HostThrow`) in the next free place of the mission array, so that the mission catches and considers it
as its own.

The effects of the script (`CREATE_FX_SYSTEM` and its variants on a person, machine, object) are repeated by
the partner with the same commands (`G_MISSION_FX`): the name of the effect, the place and what he is sitting
on. There are no more than 24 effects at the same time, there are 32 in the game array. At the end of the
mission, they're extinguished.

What the mission does with its game, sends the partner a message `G_MISSION_ENV` and returns at the end of the
mission: SAM installation Area 69 and aircraft carrier (otherwise they fired on the partner), darkness
`SET_DARKNESS_EFFECT` and the siren of the aircraft carrier (events 1161 and 1162 of the team
`REPORT_MISSION_AUDIO_EVENT_AT_POSITION`, the bank of sounds comes with the mission speech; then both are only
if the partner is next to the host and in the same interior) and the damage multiplier from the fire CJ.
One-time pickup of weapons in the story mission (fire extinguisher, rifle, rocket launcher, glasses,
parachute) takes and another: the mission then asks if the host has weapons. A partner receives his plane or
helicopter in any story mission if there is no space in the host car. The second arrow on the turret is
invulnerable, like a CJ host.

Photos of the partner are counted on the host mission (Badlands, Photo Opportunity, Architectural Espionage).
His camera only marks copies, so after a snapshot, his game tells the host which people and host items he has
taken off (`G_PHOTO`) and the host puts them a "photographed" flag.

Musical mini-games of the plot are played in two (`Minigames.cpp`, `G_BEAT`, `G_BEAT_SCORE`). The notes and
their buttons are in the music file itself, so two games with the same track get the same notes. The dance is
judged by its playback, the lowriders by the playback of the host (below). Network latency only shifts when
the points reach the host.

- The Lowriders ("Cesar Vialpando"). When the host mission prepares the track (`PRELOAD_BEAT_TRACK`, host in the
  car), the partner in the 60 m puts his hydraulics car next to him, 4.5 m to the right, or, if he does not
  have one, Savanna with hydraulics and sits in it himself. On `PLAY_BEAT_TRACK` host, he runs the same track
  and arrows (`BDISPLY`), and his movements judge mode according to the rules of `LOWGAME`: a window of
  35/50/100 ms, a word after every four notes (the combo gives tenfold points), vibration. The points of each
  note are added to the host score (`$4072`) once against the same opponent. The arrows and the judge of the
  partner follow the position of the host track: the host sends it 10 times per second (`BEAT_TIME`, position
  of `CAEStreamThread::GetTrackPlayTime`), and the partner substitutes it in `CAEAudioHardware::GetBeatInfo`
  (call of 0x4D8FEF), smoothing out the network surges. His path partner starts with the same delay as the
  position of the host, so the arrows coincide with his music. Previously, each game went on its own playback:
  the arrows of the two players diverged, and the game whose stream of music hung, froze. Like a CJ host, a
  partner’s girlfriend (HFYPRO) sits in their car: on the door of a Savanna, in another car as a passenger. A
  copy of it is also attached to a copy of the machine (`G_PED_ATTACH` runs in both directions). It moves
  according to the rules of `LOWGAME`: the mood rises to a level for six good notes and falls for two bad
  notes, for every new movement it jumps, after a missed note it stumbles. At the meeting point, the partner
  has the same cameras as the host, shifted to his car: the first frame, then five points in a circle every 5
  seconds or on the camera button. The HUD and radar are hidden like a host.
- Dance ("Life's a Beach") When a host mission launches `DANCE` without an opponent, the 40m partner runs the
  same script at its 1.8m to the right of the host, with the same track and with five stationary host cameras
  shifted to its place (they are set by the mission, but the partner does not have one). Its current account
  goes to the host, and the gain is added to the host’s account (`$21184`, to pass 2500). The new thread is
  recognized immediately after the command: at the top of the list, with the name "noname"
  (`CRunningScript::Init`) until it is executed `SCRIPT_NAME 'DANCE'`. The mod used to wait for the name
  "dance." The script number (35) is taken from the bytes of the command itself: the game reads the arguments
  of the new stream through the same `CTheScripts::ScriptParams`, and after the command there was the last
  dance argument (-1), rather than the number. Because of these two mistakes, my partner didn't dance. The
  distance to the place of dance is considered on the plane: at this point z is -100 (the script itself finds
  the ground).
- The end of the mini-game host holds up to 3 seconds until the last points of the partner arrive (`$4068` for
  lowriders, beat 31 `$21180` for dance). If the mission of the host is over, the partner's dance ends on its
  own (`$21192`). Without a partner, it's like a single player game.

The boxes in Home Invasion are also worn by a partner (`Objects.cpp`, `G_OBJ_HOLD`). The host will tell you if
each mission object can be picked up (`bIsLiftable`) and if it is in the hands of the host. The partner picks
up a copy of the box with the usual car entry button. The lifting, carrying and stowing animations (CARRY
block) for both players go to the other as normal player actions (`Anims.cpp`). At the host, a copy of the
partner takes the real box with the same task as the player (`CTaskComplexGoPickUpEntity` with the CARRY
group, as in `CPlayerInfo::Process`), and a copy that does not pick it up in 2 seconds holds it in front of it
(`ATTACH_OBJECT_TO_CHAR`). The partner has a box in the hands of the host hanging on the chest of a copy of
the host. The host puts the box where the partner put it, and the copy that still holds it releases it in
place (`DROP_OBJECT`). The object of the mission is visible only in its interior, so along with the position,
the interior is transmitted: a box taken out of the house, the partner disappeared in the hands of the host.
The host box in the hands of the partner goes into the interior of a copy of the partner. If both took the box
at the same time, it stays with the host, and the partner drops the copy. The mission of the host considers
the partner box to be its player’s box: the responses `IS_CHAR_HOLDING_OBJECT` and
`LOCATE_CHAR_ON_FOOT_OBJECT_3D` for `$PLAYER_ACTOR` are swapped through `CRunningScript::UpdateCompareFlag`,
so that `NOT` and the chains `AND` / `OR` work as usual. The truck in GUNS1 the box is loaded by the one who
carries it: the place check (`LOCATE_CHAR_ON_FOOT_3D` on the team signature) looks at the copy of the partner,
and then the button is checked by the push of the partner in the last second.

The noise and the noise of each player is different. The clue (`CHud::SetHelpMessage`) of the host's story
mission is no longer received by the partner: it is about what the host player is doing ("click ... to take
the box" from his drawer). The exceptions are mini-games, where the rules are general, and scenes on the
engine: a hint during the scene refers to the fact that the partner is watching with the host (the
hairdresser’s tips after the haircut). The partner sees his clues: at the drawer that can be taken, his game
shows GUNS118 once, and when he carries the drawer to the back doors of the van, the host sends him RYD1_30
(`G_MISSION_HINT`). The noise of "Home Invasion" (`$26820`, GUNS1_5) in a partner is his counter
(`COnscreenTimer::AddCounter`), which is visible while the partner is in the house, and not while the host is
there: the partner does not take the host strip from his counter block. The host block comes 4 times a second
and wipes the counters, so the recording of your counter is transferred through it as is, to the same slot.
The newly added counter game shows with a blink of the first appearance, and the strip blinked constantly. The
value of the partner considers himself, as GUNS1 considers for his player: the sound level where he stands
(`GET_SOUND_LEVEL_AT_COORDS`) multiplied by 1.6. Anything above 20 accumulates, and every frame goes out 1%.
The value of the host for this strip is not written to the partner. If the partner's strip is full, it reports
to the host (`G_NOISE`), and the host's play immediately after the mission has counted its strip
(`CSET_VAR_INT_TO_LVAR_FLOAT $26820`) makes it full for the next check: the colonel wakes up from the noise of
the partner.

The doors of the mission vehicles (the back doors of the van in Home Invasion) are not transferred in the
state of the vehicle. Therefore, mission commands `OPEN_CAR_DOOR`, `CLOSE_ALL_CAR_DOORS`, `POP_CAR_DOOR`,
`FIX_CAR_DOOR`, and `OPEN_CAR_DOOR_A_BIT` are repeated on a copy of the machine (`G_CAR_DOOR`). The host
remembers the last commands for each door until the end of the mission and checks the network id of the
machine once a second: the machine that was not in anyone’s interior ceases to be a network and then appears
at the partner again, under a new id and with closed doors (van while the host was in the house). A new copy
of the team leaves again.

While the host has a story mission, the game of the partner of his people in the interiors does not add: the
interiors of the mission contain those who set its script. No people doors (`PopulateInterior`), no people at
the furniture: the TV and chairs at home the game fills the population density (`CPopulation::Update`), and
Home Invasion returns density 1 while the host is outdoors. A partner in the house saw people the host didn't
have. The people at home (`InteriorGroup_c::SetupPeds`, his family by the furniture) are created by the game,
and when the player is standing under the house on the street. During the host mission, the partner's game
does not create them (call 0x59690F), and the host does not give the partner such people an interior in which
he is not himself (zone 0 and height more than 500): the host they appeared while he loaded the van under the
house, and the partner in the house saw copies of them.

Clothing that the mission wears on the CJ host is also worn by the partner (`G_MISSION_LOOK`). In Home
Invasion, it is a balaclava, and with it, the game itself includes the gait of the groove
(`CPlayerPed::ProcessAnimGroups` chooses group 69 if `CPedClothesDesc::GetIsWearingBalaclava`). Before the
first mission clothing command (`GIVE_PLAYER_CLOTHES_OUTSIDE_SHOP`, `GIVE_PLAYER_CLOTHES`), the host remembers
the clothes of its CJ, and after each `BUILD_PLAYER_MODEL` sends keys that are different from the ones
remembered. The partner puts them on top of his appearance and reassembles the model (in the car later,
already on foot). When the mission removes the thing or ends, the partner returns to his appearance. The
profile of the partner is his own clothes, not the things of the mission.

## Shared world: pickups, objects, fire, territories, and cheats

** Pickups** (`Pickups.cpp`). Both worlds are loaded from the same save, so the pickups match in model and
location (the position is stored in eighths of a meter). Gathering by your player catches an interception of
`CPickup::Update`, and another game repeats it the way the game itself does:
- The pickup truck that returns (arms, first aid kit, armor on the street) disappears at the same time.
- The rest are deleted (`CPickup::Remove`) and recorded as collected (`AddToCollectedPickupsArray`) so that the
  script waiting for them, such as a host mission, goes further.
- horseshoes and oysters are counted in statistics there, as do `PickedUpHorseShoe` and `PickedUpOyster`
  (counter, and horseshoe also luck), but without $100 and messages: the money gets the one who found;
- The photo taken by the camera (`CPickups::PictureTaken`) is counted there as a picture, and the proceeds of
  the asset taken here are zeroed there.
- The graffiti painted over here (call `CTagManager::SetAlpha` out of `CWorld::SprayPaintWorld`) is painted over there.

The host sends the world as it is: painted graffiti and horseshoes, oysters and pictures that are still lying
(`EV_STATE`). Its preservation could be older (during the mission and in the car, a picture of the world is
not taken), and after reset, the partner’s graffiti was again clean and the collected was in place. Graffiti
partner paints silently, without “tags x of 100”, and collected by the host removes without statistics: it
counted the finder. `SET_TAG_STATUS_IN_AREA` mission of the host (`CTagManager::SetAlphaInArea`: “Tagging Up
Turf” erases its graffiti) is performed by the partner (`EV_TAG_AREA`).

The mission erases the graffiti of each couple when it comes to it and erases what the partner has already
painted over. One player painted over the next graffiti while the host mission was still on the first one, and
the graffiti was clean again, and the tags x of 100 score was one less (player comment, 02.11). Now, graffiti
painted over by the partner while the story mission of the host is underway, after erasing, is painted back
silently, in both games (`g_partnerTags`). The mission sees it painted over and moves on. The list is cleared
when the host’s mission is over, so when it is repeated, the initial erasure cleans everything up. After a
failure, the partner's graffiti remains painted over until repeated.

Pickups that appeared in only one game are announced (`CPickups::GenerateNewOne`): dropped from the dead,
dropped backpack, pickups of the mission host. The pickups of scripts that go on in both games are not
announced: they are already there. There are no save floppy disks, i icons and houses for sale: each game
reacts to their scripts. Conservation points are served by stream `PSAVE1`. Maud sees it as a side job, like
gyms: everyone keeps their game, no one is waiting for anyone in a marker. The "on task" flag that he raises
for the duration of the menu is not considered a mission for a partner, so the partner no longer goes out the
screen while the host is saved. Keeping a partner at that point sends his profile to the host.

** Mission objects** (`Objects.cpp`). The host game knows that it created the script for its mission:
`CTheScripts::MissionCleanUp` lists this, Type 3 objects. She announces them to her partner, sends them their
movements and ends. If such an object is already in this place with the partner (he came with the host world),
he becomes a copy, and the second one is not created. Copies of the partner are frozen and nothing in his game
damages them: his player's kick goes to the host (`G_OBJ_DAMAGE`), and the host applies
`CObject::ObjectDamage` to his object on behalf of his player, because the mission counts the player's hits.
When a host object crashes (without visibility and without collisions), the copy is broken in the same way. So
the shot bottles are visible to both, and anyone can break them. The object of the mission, attached to the
host to the car (box in the van, tarp on the train), this machine does not touch. His partner had a solid copy
of it lying inside a copy of the car and lifting it (in "Robbing Uncle Sam" the van hovered). Now, as long as
the object is attached to the host, the copy has no collisions, and the size of the object
(`SET_OBJECT_SCALE`) goes along with it.

Objects of the main.scm itself, which he creates at the start of the game (`DONT_REMOVE_OBJECT`), are not
included in the mission list: the garage gate in Fender Ketchup (`$10632`), the warehouse shutter in You've
Had Your Chips (`$10656`), the gate of Area 69. The mission of the host shifted them at home, while the
partner kept them closed (30.10). Now, when the host's story mission moves such an object (`SLIDE_OBJECT`,
`SET_OBJECT_COORDINATES` with a global variable), it is announced immediately, before the shift, at the place
where the partner is standing, and the partner's game takes its own (`FindTwin`). Then its movement is the
same as that of the mission objects. The partner's game is looking for its object in 15 m (flag `OBJ_SCM`),
and not in 25 cm: its world could not yet be the host world. The shutter in "You've Had Your Chips" opened
with a copy, and his own remained in place, and he hit an invisible wall (31.10). It is written in the log at
what distance he found his own.

Flags of the object go along with it (`G_OBJ_CREATE`, `G_OBJ_SYNC`): no collisions (`OBJ_NO_COLLISION`,
`CEntity::bUsesCollision`) and it is aimed at (`OBJ_TARGET`, `CObject::bIsTargetable`, 0x200 in + 0x140).
Small Town Bank turns off collisions at ATMs (`SET_OBJECT_COLLISION 72@ 0`) so that bullets pass to the object
in front of them (`MAKE_OBJECT_TARGETTABLE`, Health 1000): on its health mission and breaks the ATM. The
copies of the collision remained, the partner's bullets stuck in the ATM replica, and he could not break them
(31.10).

** Objects in the streets** (`WorldObjects.cpp`) Lanterns, signs, fences, urns, hydrants, trees, boxes and
glass each game creates from the same map records (`CDummyObject`), so the subject is recognized by the model
and place of its recording. The game every 100 ms looks at such items 150 m away from its player
(`m_nObjectType` 1, with a card record) and reports (`G_WORLD_OBJ`) when the subject:
- broken: another model (`m_bRenderDamaged`), cracked glass, broken into pieces (no visibility or collisions).
  Another game breaks its own in the same way (`CObject::ObjectDamage` with the same weapon, glass `CGlass`)
- He is shot down and moves: until he freezes, his posture and speed go through an unreliable channel, and the
  copy in another game moves its physics between them, and on the spot where he lay down, freezes. An object
  that hardly moves (swings on a slope, sways on water, and the game no longer makes it static) went into the
  network ten times a second all the time while it was lying. That one's gone once a second now.

The item created again (left the zone and returned) more than 2 seconds after the record of the breakdown,
simply remains broken: invisible and without collision. The recording used to be `ObjectDamage`, and the
flashlights that had been broken a few minutes earlier would crumble again in front of a driving car (30.10).
The breakdown that just came from another game is shown completely as before.

The record lives until its subject is 150 m away from its player (`seenMs`), and only then for another 5
minutes. It used to run out 5 minutes after a break, even if the broken flashlight was lying next to the
player. Then the game considered it a new breakdown and sent it again, and the lantern of another game,
already whole, crumbled itself: in the host log, one lantern “breaks” exactly once every 5 minutes (31.10, the
fourth check).

Lanterns in SA fall to pieces (the effect is 200 in object.dat), urns and parking machines fly away intact.
The subject is usually pushed by both games: each has a copy of the second player, and a copy of the machine
goes slightly ahead of the real one and touches the subject earlier. Therefore, the movement considers the
game of the player who is closer to the object, and if both are approximately equally close, the game of the
host; movement from the other game is then not accepted. Records live for 5 minutes: the game that creates
such an object later (it was far away or gone from memory and came back) will get it the way it was left. No
items are involved that are moved by the game itself (`m_pControlCodeList`: garage gates, crossings, cranes),
doors and explosive: explosion `CObject::Explode` records the player of this game. Checked by the bot: the
flashlight, hit by the host machine, is also broken at the partner, and the parking machine lies at both at
the same point (his flight was considered a host game, 33 messages).

Only the game of the machine or the person who really hit it breaks the object. Damage from a copy of someone
else's car or someone else's pedal (`CObject::ObjectDamage` with such a copy as the culprit) the subject of
the card does not accept. A copy of the car goes a little away from the real one and touched the poles, past
which the real one drove. Her game sent out a breakdown, and the driver's poles fell on their own. A no-damage
push (a pillar or traffic light overturned, and `ObjectDamage` was not caused or caused without a culprit) was
not cut off by this, and the pillars fell again (28.10). Now the object that was changed in this game for the
first time, when the closest thing to it (a car 8 meters, a pedestrian 2 meters) was a copy of someone else's
car or someone else's person (`CopyNear`: a car that is now being played by another game, a copy of another
player, a person from another game), is not sent out at all (`Thing::byCopy`) and remains here as it is. If
there was a hit in that game, she'll report it herself.

**The garage gates at the house** open only to the player of their game (`CGarage::Update` looks at
`FindPlayerCoors` and `FindPlayerVehicle`), and the gates that rise up become a wall as soon as they begin to
close (`m_bDoorClosed`, `CObject::ProcessGarageDoorBehaviour`). At the beholder, the gate was closed, a copy
of the other player's car was pushed out, and the synchronization was pulled back, and she was shaking
(29.10). Now, while the other player is at the gate the way the game requires his (in the car closer to 10 m,
on foot closer to 4 m), they are open here: the closing go back, the open do not close, and the closed only
rise, without issuing and cleaning the storage machines.

** Garages by houses** (`Garages.cpp`) Garage shelter removes the cars left inside when it closes behind the
player (`CGarage::StoreAndRemoveCarsForThisHideOut`) and exposes them when the player comes back
(`RestoreCarsForThisHideOut`). Storage (`CGarages::aCarsInSafeHouse`, four cars per house) is part of the host
world and its preservation. The world of the partner is loaded again and again, so the game of the partner of
the machine does not remove and does not expose itself: before both games exhibited the same machines
(doubles), and the machine that the partner removed disappeared with the next load of the world. When the
garage closes with the partner, his game asks the host to pick up their cars that are left inside
(`G_GARAGE`). The host game puts them in storage (`CStoredCar::StoreCar` per copy), and the partner game
deletes them. When the garage opens with the partner, the host game gives him the storage records and clears
its seats, and the partner’s game puts the cars at his place: where the partner stands, the ground is loaded.
It loads the models before that (`RestoreCar` does not create anything without the model), and what it failed
to put back to the host. The host partner's world image is also saved, and `CGarages::Save` closes open
garages and removes cars before saving it. For the picture, it is disabled, otherwise the gate slammed in
front of the host. Checked by the bot: the machine from the vault of the test world exhibited at the partner,
along with the bot machine went to the host, returned, and in both games one copy at the same point.

Garages shared (player selection 02.11). Previously, the host machines only came out of the host, and the
partner’s car only from the partner, and the car left by another could not be taken away. Now, when the garage
opens with the partner, the host game gives him all the storage records, and his own too. When the garage
opens at the host, the partner's cars come out: interception `RestoreCarsForThisHideOut` no longer removes
their recordings from the locations. The partner’s entries list (`g_partnerCars`) is now only needed to clean
the host: it cleans all four places and fills them with machines that are inside, and the recording put by the
partner while the host gate was open was erased. The poster goes off the list, otherwise the next cleaning
would return it to the vault while the car is in the garage, and the car would come out twice.

After driving up to the closed garage, the partner's game counts the occupied places by its copy of the vault
(`CGarages::CountCarsInHideoutGarage`), and this is the host world at the time of booting. On the copy, the
garage was full, and the gates did not open with the inscription "You cannot store any more vehicles in this
garage!!", although the host had seats (01.11: "Partner can't keep cars in the garage"). With shared garages,
this would almost always be the case: a partner picks up the host cars, and a copy still counts them. Now, the
host game tells you how many machines are in each home’s vault (`GARAGE_COUNT` with each change), and the
partner’s game takes that number.

While the garage is open at the partner's, his cars are in the partner's game, and they are not in the host's
vault. If the host at this time was saved and left the game, they disappeared, and with shared garages among
them and the host machine. Now, while saving the host, copies of these machines are also recorded in storage,
and immediately after recording the garages (call `CGarages::Save` to `GenericSave`) are removed from there.
If the record of such a machine has already come from a partner, and the copy has not yet been deleted, the
second time it is not written (the same model is closer to 2 m).

** Workshop doors** (`Garages.cpp` ). `CGarage::Update` moves the door of Pay 'n' Spray, a bombshop or tuning
studio only for the player of his game: Pay 'n' Spray of the other game stood open while the player inside was
closed, and the tuning studio did not open in front of the second player's car. Now the door in both games is
controlled by the player who uses the garage: whose car is inside, and if there is no one inside, then who is
closer within 30 meters (first in the car, then on foot, with equal host). Both games come to the same answer.
The other game moves its door like a script garage (type 19): with sound and collision, but no paint, bomb,
board, fugitive reset, and taken control (`G_GARAGE_DOOR`).

Doors that the script opens and closes (mission garages, theft warehouse `BURG_LK`, garages running the
script) during the story mission of the host belong to its mission and move with the partner in the same way.
“Home Invasion” opens a warehouse for the van: the partner’s warehouse was closed, a copy of the van was
driven into the gate, and the partner fell through. Outside the mission, each game moves them with its own
scripts, and during the general part-time they move behind the game of the one who leads it (`SideLeader`): in
the theft, the warehouse gates were not opened when the van was led by a partner (01.11).

The car inside the workshop belongs to the player only if he is driving: `FindPlayerVehicle` counts the
passenger. A passenger's Pay 'n' Spray would repaint a copy and charge $100, a bomb shop would put a $500
bomb. The menu of the tuning studio also belongs to the driver: the passenger of the other player’s car
received his own, both paid, and the parts of the copy instantly became the same as the owner’s. The script
atelier of such a passenger does not go. Without it, the passenger had the camera of the car, and in a cramped
room the walls of the studio pressed it inside the car (02.11). While the driver-other player is standing in
the studio (his camera `MODE_FIXED`, the car in his area), the passenger sees what he is: the camera is in
place and direction from his state (`PassengerShopCamera`).

** Fire** (`Fire.cpp`). Fire on the ground from explosions and Molotov cocktails lights up in each game
itself: these events are transmitted. Only fire is transmitted on the player, pede, or machine that is owned
by this game, and the fire of the mission of the host (`START_SCRIPT_FIRE`). Another game lights the same fire
on its copy or at the same point, of the same force (`CFireManager::StartFire` at the point lights any with a
force of 1, so the force and flame are put after: `CFire::CreateFxSysForStrength`). It does not spread, does
not injure the copy (copies are refractory, protection is removed only for the time of arson) and goes out
when the owner's fire goes out. The copy that the player of this game extinguished (from a fire engine or fire
extinguisher, no further than 30 m), the owner's game is also extinguished (`FIRE_OUT`). That's how a partner
helps a host on a firefighter's side job.

The fireman’s part-time machine is set on fire by `START_CAR_FIRE`: it is a script fire (`CFire` bits 1), it
does not go out on its own. A copy of the fire on the car was lit by a regular one (`CFireManager::StartFire`
on the target removes this bit), and in ordinary fire on the car, the game does not look at the time and gives
it 3-4 seconds, and then 60 m from the camera extinguishes immediately (`CFire::ProcessFire`). The part-time
machine is refractory and with full health, it does not light up itself, and the partner had it standing
intact (28.10). Now `FIRE_START` carries the sign of script fire on the machine (Protocol 27). With one bit,
the copy was still not visible (28.10), and now such a copy is lit by the same feature as the owner's game
(`CFireManager::StartScriptFire(pos, vehicle, 0.8, 1, 0, 1)`). The copy of the fire of the part-time job, which
went out on its own, without `FIRE_STOP` and without our player’s water, is lit again (up to 20 times), and
the magazine writes how long she lived and what happened to the slot. The extinguished copy of the script bit
saves, and the game does not give such a slot to a new fire, so the bit is removed when the copy leaves
(`Drop`), and at the end of the session: copies of machines remain in the world of ordinary machines.

** Explosions, shells and water cannons (`Fire.cpp`) An explosion (a grenade, a rocket, a Rhino projectile, a
bomb, a mission explosion) takes place in the game of the person who staged it, and another game arranges the
same on behalf of its copy of the creator (`CExplosion::AddExplosion`, `G_EXPLOSION`). The game misses its
explosion of the same breed from a copy of an alien creator (a rocket that was released by a copy of our
partner): the real one will come. Cars, boats, planes and objects explode in each game in their
synchronization, and a cocktail flash lights its fire. Projectiles (garnets, cocktails, gas, rockets, homing
missiles, bombs, missile traps) are visible in flight: another game releases the same one from the same point
at the same speed (`CProjectileInfo::AddProjectile`, `G_PROJECTILE`), and its explosion is missed as any other
alien. The anti-aircraft missile is on behalf of the player who was shot so that its explosion was missed. The
explosive charge flies in the same way and lies with the other player where it fell, until a copy of the
thrower detonates it with a detonator (this explosion is missed) or a real explosion comes. Previously, the
other player saw the charge only in the hand. The jet of the fire truck and SWAT van
(`CWaterCannons::UpdateOne`, `G_WATER_CANNON`) also hits from a replica machine, extinguishes the fire and
knocks it down there. A police helicopter machine gun fired only in a helicopter game
(`CHeli::ProcessControl`, call `FireOneInstantHitRound` to 0x6C7773) and the other player saw no tracers or
hits. Now, each queue runs over the network (`G_HELI_SHOT`: machine gun point relative to the helicopter and
target) and is repeated from a copy with a flash, tracer and sound. The shot is not recorded on anyone, so
each game only hurts its own: copies of people and machines of the other game are missed, and each bullet hits
once.

** Trains** (`Trains.cpp`). The train belongs to the game where it appeared (accidental or missionary), and it
sends its view, path, direction, rail distance and speed. The other game holds the same mission train
(`CTrain::CreateMissionTrain`) and each frame gives it distance and speed. The type of train is not stored in
it, so the mod remembers it when it is created: the `CreateMissionTrain` makes random trains. Cars are locked
and the cockpit is open until the train is controlled by the player and the host has a story mission.
Previously, all copies were locked, and the partner could not get on the train at all (29.10). The cockpit
replica drives it in his game with his buttons, and the owner's game puts his train its distance on the rails
and speed (`TRAIN_DRIVE`) and keeps the train as a mission train so that `CTrain::DoTrainGenerationAndRemoval`
does not remove it away from his camera. Random trains next to the host give the game host: the partner does
his game only as long as he is further than 600 m from the host (`CTrain::DisableRandomTrains`). A copy of the
train goes to the nearest node of the same path as the owner. `CreateMissionTrain` without a node took the
nearest node of all four tracks, and path 3 goes a few meters from the main line through Los Santos: the copy
went to another path, where the same distance on the rails is another place (“Catalyst”: the partner saw only
the tarp). Car models are loaded in the background, and a copy appears with one of the following states.
Previously, six blocking loads in a row (with a full streaming queue clearing) gave a frieze every time the
train came closer than 300 m.

The game only makes a screeching train brakes for its player behind the wheel:
`CAEVehicleAudioEntity::ProcessVehicle` calls `ProcessPlayerTrainBrakes` (on the brake of the drop 0) only at
`m_IsPlayerDriver`, and for someone else's train only `ProcessDummyTrainEngine`. The partner didn't hear the
host slow down (30.10). Now, for the head car, which is ruled by a copy of another player, after
`ProcessDummyTrainEngine`, the game considers the same creaking as its formula with the buttons of that
player: the drop and the status of the train are replaced by one call. The sounds lie in the player's engine
slot (40), so while the player is driving, this is not done.

**Gang territories** (`Zones.cpp`). The map of territories follows the host. When the gangs, color, or radar
changes in his game (blinking during an attack), his `CZoneInfo` changes his partner, except for the drug
counter, which each game turns itself. The host is playing next to the host of the war, and the killing of the
partner is in its provocation (see para. "Damage." `StartDefensiveGangWar` - The partner does not start the
defense. He starts his attack himself (`G_TURF_WAR`) if the host is further than 100 m, the host has no war
and mission, and the plot of the war allows: interception `CGangWars::StartOffensiveGangWar` in the game of
the partner asks about it what the host reports in `G_GANG_WAR` (flags "wars are allowed", "the plot holds the
war", "the mission is going"). The host at this time nullifies his provocation so that the second war does not
start at him. The war is led by the game of a partner: waves, victory, the capture of the area. The host
receives the beginning, the area during the battle (bytes `CZoneInfo`, which are checked: only the gangs and
the color of the area can be changed) and the end, and in the chat writes who started the war and whether the
area was recaptured. If the host starts a mission, the partner's war ends (`CGangWars::EndGangWar`) and if the
partner exits, the host returns the gang-colored area (`FillZonesWithGangColours`). The partner at the
entrance to the world of the host throws off the inherited from the preservation of the state of war.

**Chit codes** (`Cheats.cpp`). Cheats that change the overall world (weather, speed, traffic, passers-by,
themes, machine behavior) operate in both games. The game where the cheat is entered finds it in the same way
as `CCheat::AddToCheatString`: the hash of the keys dialed, from the longest chain of new to short
(`CKeyGen::GetKey(str, len)`), the moment it counts the matching cheat (a call `CStats::IncrementStat` within
this function), and sends the number and the new state. The beginning of the function itself is not
intercepted: GInput reads from its first command the address of the cutscene flag and fell, receiving an
intercept jump instead. The other game uses it (`CCheat::ApplyCheat`) and the switch only if its state is
different. Personal cheats (guns, money, health, tracing, cars, body, skills) remain where they were
introduced. Themes (beach party, ninja, “Love Conquers All” costume, “Funhouse”, village) change the clothes
of the person who used them, and the ninja also gives the katana. The second player returns his clothes
(`CPedClothesDesc` and `CClothes::RebuildPlayer`) and his melee weapons.

** Objects** (`Props.cpp`). A game where a person has an object in his hand (`CTaskSimpleHoldEntity`: tray
from a diner, a can from a machine gun, a cigarette) or is attached to it (`CPhysical::m_pAttachedTo`: open
parachute), reports the model, bone and displacement, visibility, size and animation of the object itself. The
other game makes a collision-free copy, and each frame puts it on the same bone of its copy of that person.
The released item, his game lasts another 30 seconds. There are also barriers and burning frames of police
cars (`CRoadBlocks::CreateRoadBlockBetween2Points`): they are created only by the player's wanted game, and
another saw the police behind an empty road. These objects have an area of 13 ("visible everywhere"), and the
distance to another player is counted as outdoors. Copies are created with type `OBJECT_MISSION2` as external
script objects: for the game it is a mission object, but `CPools::SaveObjectPool` saves only `OBJECT_MISSION`,
and copies no longer fall into host saves.

** Speech of the People (`PedSpeech.cpp`) The game whose person speaks
(`CAEPedSpeechAudioEntity::LoadAndPlaySpeech`) sends the bank and sound number, while the other plays the same
replica of his copy of that person in one of his speech slots. They don’t say copies of their own words. The
bank that this game does not have (other sound files) is not played. The loudspeaker of a police helicopter or
boat (`CAEPedlessSpeechAudioEntity`) speaks only in the police logic of his game, and the copy does not
perform it: the other player did not hear the pilot (29.10). Now the machine's game sends the context of the
replica, while the other one says a replica of the same context from its copy (`CAudioEngine::SayPedless`).

While the partner is not in the world of the host (the host passes the prologue, the world is not yet loaded),
rebounds, graffiti, pictures, cheats, purchases of houses, territory and doors between their games do not go:
these are events of different worlds. The door of the interior, which the host mission closed while the
partner is inside (the dens in Clearing the Hood), the partner remains open from the inside: the exits of the
area in which he stands are always open.

Fire, objects and mission objects of the player whose game has started anew or who has come out, the other
game removes. She recognizes a copy of the fire by the slot, the purpose and the place: the extinguished flame
gives the slot to the next fire (Molotov cocktail lights several), and a copy was considered someone else's
fire.

Trains do not synchronize cars: the player who sat in the cockpit declared the train a car, and another game
created a car off the tracks. Rail distance and speed from the network must be finite:
`CTrain::ProcessControl` returns the distance to the track one length at a time and hangs on a huge number.
The partner creates his random trains further than 220 m from the host (at this distance, the host game
deletes its own), and the game sends the train only when the other player is closer than 300 m.

Side-objects (billiard balls, dash targets, quarry stones) are only in the game of the person who is engaged
in them: they are created by his mission script, and another player saw a game of billiards on an empty table.
Now items from the mission list of this game (`CTheScripts::MissionCleanUp`) go to another game in the same
way as those thrown from their hands: no collisions and where the original lies (`Props`, the view "lies"),
while the other player is closer to 150 m. Items of the host's story mission still go through `Objects.cpp`.
The targets of the joint shooting range of the second player are solid (`RangeCopy`): his shots on them go to
the owner's game (`PropCopyHit`, see). "Joint jobs and races."

The bar and dumbbells of the gym each game creates itself on its bench and counter when its player approaches
them: these are the brains of objects `GYMBENC` and `GYMDUMB` of `script.img` (`CREATE_OBJECT`,
`ATTACH_OBJECT_TO_OBJECT`, on approach `TASK_PICK_UP_OBJECT`). A copy of the barbell or dumbbells of another
player in the hands of his copies and his on the counter were seen by two: two barbells at the bench, four
dumbbells (08.11). Now its projectile of the same model (`KMB_BPRESS`, `KMB_DUMBBELL_R`, `KMB_DUMBBELL_L`),
which lies on a rack closer than 3 m from the visible copy, is hidden (`HideOwnWeights`). It is again visible
when the copy is not near or when our player picks it up himself: the script first removes it from the rack.
The copy, which the owner put back on the rack (the released object his game leads another 30 seconds), the
game does not show if next to his projectile of the same model. If two are engaged on the same bench, each in
his own game, the copy lies on the same bench with the barbell: it is not corrected.

## Individual progression

Each player on the server has a profile in each host game (`profiles/<game>/<nickname>.bin`): muscle, fat,
endurance, lung volume, weapons and driving skills, combat style, money, clothing, hairstyle, tattoo, maximum
health and armor, as well as part-time rewards: running without fatigue, fast recharging, fire resistance,
getting out of prison and hospital without losses. The host’s world brings the host’s rewards, and the profile
returns the host’s own.

The host is swinging in its preservation. The partner after downloading the world gets his profile on top of
it. Changes are sent to the server every 30 seconds. When the host saves the game through the save menu
(`C_PcSave::SaveSlot`), the partner immediately saves his profile and remembers his weapon (`G_HOST_SAVED`),
and the server gets a fresh world. The profile server records with a reset to the disk (in case of a power
outage), and before the network stream waited for the disk: sending all states stood up every time the partner
sent the profile (every 30 seconds), and players saw jerks. Profiles are now written in a separate thread.

Profile is the partner's last state, not the preservation: he died after saving the host, the host booted, and
the partner was left empty-handed, and the host with a weapon (11c). User Decision 04.11: Only the weapon is
rolled back. On `G_HOST_SAVED`, the partner writes his weapon to `coop_saved_guns.bin` in his user folder: the
file is needed because the download happens on another day. The host takes a new download number and sends it
(`G_HOST_LOADED`, again every 15 seconds). If the number is different from the file and the game is the same,
the partner receives the weapon from the file when he is on foot, alive and no mission
(`BackToWeaponsOfSave`). Reconnecting to the same boot does not change anything. Dead partner doesn't rewrite
the file. If the file is not there or it is from another game (the host is saved until the partner arrives),
the weapon with which the partner came comes into the file: otherwise, the death and download of the host did
not return anything.

The profiles of each host game have their own. Previously, the new game moved all profiles to
`profiles\old\<date>\`, and the partner who returned with the host to the old save started from scratch
(01.11). The host game is known by its client: the downloaded save slot is taken from the file name
(`ms_LoadFileName`), and which game is in which slot is recorded in `coop_games.ini` next to the saves. The
new game (its prologue comes from the first seconds) receives a new random number, and when saved through the
menu, the number is written to the slot. The host reports the server number (`C_GAME_ID`, protocol 34), and
the server sends the connected partners a profile of the game. Savings made up to or without a mod are
considered Game 0, and have the same place as Game 0: `profiles\<nickname>.bin`. The profile comes with the game
number, the partner returns it with each save of the profile, and the server does not record the profile sent
for the previous game of the host.

The wardrobe is on the list of purchased (`CShopping::ms_bHasBought`): `wardrobe.scm` picks up the goods of
stores through `HAS_PLAYER_BOUGHT_ITEM`, `clothes.scm` notes what has already been purchased. There are no
other real readers in this team. The list is in the save (block 22), so each download of the host world puts
the host list to the partner: the host’s clothes were in the wardrobe, and there were no own ones (04.11). The
version 6 profile stores the list with keys, because `shopping.dat` can be different for games, and after
downloading the world replaces the list with the list of the world. Host things don't get to your partner. If
there is no list in the profile (first parish, profile older), the partner brings the list of his game in the
same way as his CJ: from memory, from `coop_own_bought.bin` next to saves, from the newest save. The block in
the file is determined by device (price modifiers, number of goods, bytes for goods, then the block label),
another number of goods means another `shopping.dat`, then the list is not read. There is no game left, there
is a list of the world. What a partner wears, in this case, is always marked by the purchased: otherwise the
wardrobe will not return him to these things, and the host purchased in the world has never been recorded
before. In the new host game, nothing is brought in, the list starts with the starting clothes.

The new host game starts partner profiles anew: connected partners get an empty profile and use it in the
world of the new game. The teammates who were playing in the old world at the time are also waiting for the
end of the prologue and move on to a new game. In the new host game (`IsFollower`, or an empty profile came
after the present), the CJ is not worn on the partner from a single game: he starts with the clothes and body
with which the game begins. Previously, an empty profile was considered the first arrival, and a partner
brought old clothes, fat and muscles to the new game (31.10).

The game takes the jump in stats after the profile for rising respect and shows a hint of "Respect -
Upgraded," even on top of the cutscene. This mod prompt is removed in the first seconds after the profile
(`Progress.cpp`, GXT key with hash 3B797709).

Appearances (clothes, fat, muscles) are sent to other players, and the character is rearranged
(`CClothes::RebuildPlayer`). During this call, the focus is a copy: Cheat Menu replaces the call
`ConstructPedModel` inside it (0x5A834D) with his, which builds clothes `FindPlayerPed()`, and each saw the
other in his own clothes, and skins were shown (#106, disassembled 04.11). Each new ped of the remote player
(after downloading the world, death) is rebuilt anew. Poole often gives the new pedal the address of the old
one, so the applied appearance remembers the handle pedal, not the address. Previously, after the reboot of
the host world, a new peda with the same address remained with the body that had been assembled last.

Each player has his own personality. The host world comes to the partner along with the host’s CJ, so the
partner puts on his CJ. It is taken from the game in which the partner was before the connection (the game
started by the partner, that is, the world of the host, is not considered his after leaving the session). If
the partner connects from the menu, the CJ is taken from the newest save: block 2, pedal player,
`CPlayerPedDataSaveStructure` size 0x84 with clothing from displacement 8. At the first visit, fat and muscles
are taken along with the clothes, and then the body comes from the profile statistics. The profile of version
3 remembers whether it has its own look. Alien appearance (from the profile of the old version or CJ host,
when your CJ is not) once concedes to his, if the partner during this time did not change his clothes. The
profile is tied to a nick, and with a new nickname, money and skills begin anew.

The progress of side activities is stored in the global variables main.scm, that is, in the world. The world
of the host brought its own values with each download: lessons, medals and partner levels began anew, and
unique tricks were paid again. The profile of version 5 stores these variables by list (`kSideGlobals`:
driving school, boat and motorcycle schools, trucker, quarry, taxi, courier, service jobs, pimp, freight
train, gym, theft) and returns them after each download. The profile comes from the server, so only variables
from the list are recorded. The flight school profile returns only if the host world has already passed
"Learning to Fly" (`$87`): before that, the school is part of the plot.

In addition to variables, the profile stores what lies in the engine itself. These are unique stunt flags
(`CStuntJump` + 0x40 "made" and + 0x41 "found"), school award machine generators and career and business
revenue pickup trucks. The trick is recognized by its start and finish zones: after rebooting the world in the
same game, the pool distributes tricks to other cells. The generator is turned on if the partner has earned a
reward, and in the host world it is turned off. A pickup of income is created where it is not
(`CREATE_PROTECTION_PICKUP`, as in the script work). Like other pickups of partner scripts, it gets into the
world of the host, so income accumulates between reboots, and either of the two can take it, as well as the
host’s income.

Story variables, object numbers, work-on flags, and girlfriends remain from the host world. The script of
friends, seeing a date that is not in the world, after each download would move the statistics of dates anew.

The list of exports in the port of Easter Basin is general (`features/story/exports`). Script `IMPEXPM` keeps
it in global variables: list number (`$4196`) that is delivered (`$4240`), import table (`$4304`), end
(`$4736`). Previously, everyone had their own list in their profile, and the car was crossed out and paid only
for the game of the person who passed it. The second game did the same only if its player was in a marker: its
script took a copy of the machine (`GET_RANDOM_CAR_IN_SPHERE_NO_SAVE`) for its own, so both were paid nearby,
and one from afar (04.11). Now the list belongs to the host world and is not included in the profile. The
counter of handed over cars (stat 213) is also world-wide: premiums for 10, 20 and 30 cars depend on it. The
game, where the change flag rose, sends `G_EXPORT`: the number of the car, the amount of `29@` and the entire
list. Another game crosses the machine, pays the same amount, turns off the machine generator, and if its
player is at the board, translates the script to its board setting, as after surrender (address `0x1D8D5`,
only from the idle cycle `0x1DCAF`). The completed list she translates to the next. The host also sends a list
every 15 seconds when it changes: the partner’s world is older than the host’s. If the player is now renting
the car (`16@` is more than 0), the message of another person waits for the end of the change, but not longer
than 15 seconds. The machine already crossed out here is not counted and paid again, so two of the tokens get
paid once. The game progression (`PLAYER_MADE_PROGRESS` in cells 16, 28, 40 import) is only in the host world.

Part-time work and testing are counted both (`G_SIDE_DONE`). When one player has a completion flag
(`kDoneFlags`: taxi, ambulance, firefighters, police, couriers, pimp, freight train, trucker, quarry, theft,
schools, Blood Bowl, and others), the other receives the same flag and the same reward that a game would give
him: income, increased health or armor, running without fatigue, fire resistance, a car or a pickup of
property. Flags of the world (shared for two) are placed only by the host. When connecting, players exchange
flags from profiles, so that what they have passed before is also counted.

Taxi counts trips twice: in global `$720` (variable 180) and in statistics 150, and nitro gives when at the
end of the trip `$720` is more than 49. The statistics for both are common, and `$720` each game has its own:
the two scored 50 trips, and nobody received nitro (02.11). Now, once a second, `$720` is no lower than 150.
Nitro arrives on the next trip, and the flag of 1491 gives it to the second player. Theft: Running without
fatigue is given for 10,000 stolen in `$7160` (variable 1790), and the same amount is accumulated by the total
statistics 41.

Trucks and quarries count the completed tasks at `$32636` (8159) and `$32684` (8171). The next task is
selected on this counter, and the reward is given for everything (8 and 7). Now the tasks of both players are
counted together (player selection 02.11, `kSharedCounters`): the game sends an increase in its counter to
`G_SIDE_DONE`, and the other game adds it to its own. The first person to finish is rewarded, and the flag of
part-time work is awarded to the second. Everyone misses tasks done by others.

Statistics and important skills are common (`G_STAT_SYNC`, `kSharedStats`). Each statistic has its own rule:
the amount (kilometers traveled, murders, money spent), the maximum (driving skills of a car, motorcycle,
bicycle and aircraft, excitement, part-time levels, records), the minimum (best time) or the value of the
host. Once a second, the game compares statistics with the previous shot and sends changes, and after
downloading the world, profile or connection of the player, the picture is taken again and the parties
exchange all the statistics. The sum adds only the difference, so the same thing is not counted twice. The
statistics tab on the menu for both shows the same thing. Body (fat, muscles, endurance) and weapons skills
each own and live in the profile.

## Localization

The F9 window, chat, tips and mod messages are available in Russian and English. The language is stored in
`coop.ini` (`[player] language`), switched in the F9 window and immediately saved. The new installation takes
the Windows language, and configs without this key, made by the previous versions, remain Russian.

Each client text is written in both languages where it is used: `TR("Подключиться", "Join a game")`
(`shared/config/Config.h` ). `Format` is declared as `printf` , so the compiler checks both lines of the pair
with arguments. `tools/i18n_check.py` in CI searches for the client's Russian text without English, except for
the journal and comments `coop.ini` . The server does not know the languages of the players: it sends its
messages in the chat, reasons for refusal and greetings in two languages at once, and the client shows the
desired one. The ASI bootloader takes the Windows language.

## Chat and voice

The chat opens with the key `T`. The symbol of this click, which Windows has already put in the queue ("t" or
"e" in the Russian layout), when you open the chat, is removed from the queue. If `T` is pressed immediately
after the other letters and continues the cheat code with them (the game stores the last clicks in
`CCheat::m_CheatString`), it leaves the game, and the chat does not open: otherwise codes like `NINJATOWN` or
`AIWPRTON` were not typed. The list of such codes (64, with the letter T not the first) is checked against the
hash table `CCheat::m_aCheatHashKeys`. The letter before `T` should be pressed no earlier than in 1.5 seconds,
and if it is one (`ST...`), then in 0.4 seconds: S with a T in a row can also go back before the chat. Codes
that start with `T` can not be distinguished from opening a chat, for them the chat key can be changed to
`coop.ini`. `Enter` sends a message, `Esc` closes the chat, PgUp and PgDn leaf through the history. Teams:
`/help`, `/players`, `/pm`, `/me`, `/tp` (to a partner) and `/tphere` (to a partner to himself). `/kick` and
`/say` are available to the host. `/tp` and `/tphere` are requests (`Teleport.cpp`, `G_TP_ASK`, 29.10): the
second player sees a window with Y and N for 20 seconds, and only after his consent does the player's game do
what the team used to do immediately, with the same rules for buildings (`Players::TeleportAllowed`). The host
can ban partners from both teams by ticking when creating the game (`partner_teleport` in `coop.ini` and
`server.ini`, comes in `Welcome`, protocol 29).

The F2 window (`pages/players-menu`) shows players with a role, money and ping and picks up the mouse and
keyboard like the F9. The money of other players is reported by their games (`Money.cpp`, `G_MONEY`): with
each change, no more than twice per second, and once every 10 seconds. A button with a $ sign from the HUD
money font (pricedown) opens the transfer bar. The money goes away from the giver immediately and is added
from the receiver when his world is loaded and the receiver's game responds that it has come. If the recipient
leaves before the answer, the money is returned to the donor. The profile of the partner is saved immediately
after the transfer. Previously, F2 had only a list on top of the game. Chat is drawn in its render on D3D9 in
Windows font, so the Cyrillic alphabet in it is full. The input is intercepted through a replaced window
procedure.

Next to the $ button, the pistol button transmits weapons (0.3.3, `features/weapon-gift`, `G_WEAPON_GIFT`). In
the transmission line, your weapon and number of ammunition are selected, and melee weapons, flowers, glasses
and a parachute go entirely. The weapon goes away from the donor immediately and is given to the recipient by
the usual command (`GIVE_WEAPON_TO_CHAR`: cartridges of the same weapon are folded). In one slot, one weapon:
if the recipient has another in this place, the gift is returned to the donor, and the recipient sees in the
chat that the place is occupied. A gift that is not answered before the recipient is released is also
returned. The detonator doesn't pass; it's coming with the explosives already thrown.

The F9 window and other cursored fashion windows take the mouse away from the game: while they are open, the
`CPad::UpdateMouse` does not give the game any movement. In the full-screen game, she has a mouse in the
DirectInput monopoly mode, and the Windows cursor stands still (issue #66: the cursor freezes on the window
with the invitation code). Then the cursor of the mod moves along the same mouse movement that the game reads,
and the button and wheel are taken from there. Previously, the mod put the game mouse in general mode for
window time and back, but after closing the window, it remained uncaptured, and the camera stood up to Alt +
Tab, including after entering the host.

The window buttons are triggered by pressing, and the window closed by clicking gave the mouse to the game
with the button still clamped. The game considered it a new press: “Enable” in the U window gave first a
punch, then an animation (03.11). Now, after the window, the mouse buttons reach the game only after they have
been released (`HookUpdateMouse` in `Patches.cpp`).

The voice is transmitted while `B` is pressed (the key can be changed). The sound from the microphone is
captured via WinMM (16 kHz, mono), compressed by the IMA ADPCM codec (4 bits per countdown, 64 kbit / s) and
passes through the jitter buffer. WinMM devices poll a single mod stream every 5 ms, and the game stream only
encodes, sends and queues what has come. The WinMM call sometimes holds a second (`waveInAddBuffer`, even when
no one is talking), and when questioned in the game stream, the whole game got up by the same amount: in the
host log, it was five times in eight minutes, and this is the 1-2 seconds lag that players complained about.
Loudness may depend on distance. While the player is talking, he has a badge over his head and in the list of
players, and on the left screen it says who can be heard now. While the key is pinched, there is also a “●
Microphone” on. The icon is a microphone image from the resource `MICROPHONE`, which the renderer draws as a
font symbol (U+E000). All three places take it through `ui::TalkingMark()`: previously in part of them
remained "(.)" (27.10). If you can’t read the picture, instead of it, “(•)”, and in the magazine a line about
it.

Mod keys (chat, sit down, player list, microphone) and voice mode change in the window F9, the button "Keys"
(0.3.3). The next key is pressed, and Esc cancels the choice. If the key is already occupied by another mod
action, the actions are changed by the keys. Voice: While the key is pressed, the press turns on and off
(`[voice] toggle`), always on or off. Everything is immediately recorded in `coop.ini`, and the voice module
picks up on and off without reconnecting.

If the microphone does not open, the mod writes about it in a chat and tries again while the key is clamped.
Every 5 seconds, the magazine writes how many frames were sent and what level they have. If Windows does not
give access to the microphone, it records pure silence, and this message also comes to the chat. The test tone
(`[debug] voice_test_tone`) of the microphone does not require: frames with it are sent by timer, so that the
network path of the voice is checked on one machine.

## Crashes

The machine that the game deleted, and in its pool cell in the same frame put a new model, checking a live
copy missed: the pointer led to a live machine of the same model. But the saved number (`handle`) found
nothing, and `GET_CURRENT_VEHICLE_PAINTJOB` of the `ApplyLook` gave the game an empty car. Host play dropped
at `CVehicle::GetRemapIndex` (0x6D0B73) in Small Town Bank (#93, 30.10). The car, whose number no longer finds
her, is now considered missing (`Vehicles::OnPreProcess`).

A copy of another mod player is removed by herself (`Players::DestroyRemote`) and what she was holding was
removed from the world along with her task of "holding the object." The host had a copy of the partner holding
the opponent's cue of his `POOL2`. The partner's game fell, his copy was removed, and the host's `POOL2` fell
after 6 seconds on `GET_OBJECT_COORDINATES` of this cue (0x47D582, empty pointer address +0x14, 31.10). Now,
before being deleted, the copy releases the item (`CPed::DropEntityThatThisPedIsHolding` without deletion) and
it remains with the script that created it.

When departing, the handler (`CrashHandler.cpp`) writes in `coop\crash.log` exceptions, registers and return
addresses from the stack. If no one processed the exception, then the Windows error report went on, and his
window at the full-screen game may not appear: the game was no longer there, and the process remained, and the
server he launched held the port. The next game connected to this old server and received an “incorrect
password.” Now, after writing the report, the process ends on its own, and the game that creates the server
first stops the remaining `coop-server.exe` from its folder.

Some of the departures are errors of the game itself, which happen more often with fashion or depend on the
build. The mod closes them where they occur (`Patches.cpp`, engine protection section):
- The mission can create a model machine that it requested long ago without checking that the model is still in
  memory: a chase in OG Loc asks Manana while loading the track records, then releases and creates three on a
  timer. The model dropped `CVehicleModelInfo::CreateInstance` (0x4C9691) on the host. Intercept
  `CCarCtrl::CreateCarForScript` first loads it.
- High quality shadows at map objects: `CStencilShadows::RenderForObject` only checks the number of triangles of
  the shadow grid, and `CCollisionData::RemoveCollisionVolumes` (the collision file is unloaded) frees the
  grid and leaves numbers. The object, which was still on the shadow list, dropped the game (0x40F64C)
  immediately after the rockets exploded. A shadow without a net is missed.
- The sound stream (`CAEStreamThread::Service`) begins the track from the position on the module of its length
  (0x4F1464). A track whose decoder opened but did not report lengths (broken or empty build sound file) was
  divided by zero for both players at once (the barbershop in Ryder). Now this track starts from the
  beginning: division is replaced by a transition to a short insert on the assembler, which skips it at zero
  length.

The weapon, phone and jetpack in the hands of a copy of another player, as well as a block of his animations
downtime mod requests from streaming and puts through frame-to-two when the model is already in memory. They
used to be loaded at once (`CStreaming::LoadAllRequestedModels`), and this feature loads the entire streaming
queue, and each change of a partner's weapon could stop the game for a second or more (issue #43).

Models, details of tuning, painting, weapons, speech sounds and names of special characters that came over the
network, the mod checks before giving the downloader. Players with different mods may not have the other model
number, and the bootloader fell (`CStreaming::RequestModel` unchecked, `CPickup::GiveUsAPickUpObject`,
`CStoredCar::RestoreCar`). The machine from the host’s garage, which this game can’t assemble, returns the
host untouched. Before creating a copy, the reserve in the game pools (12 pedestrians, 10 cars, 20 objects) is
checked: on a filled pool, `CREATE_CHAR` falls, including at the host mission itself.

The limit adjuster (fastman92) transfers the model state table (`CStreaming::ms_aInfoForModel`) and corrects
the places where the game reads it. The mod takes the address from the same place, from `HAS_MODEL_LOADED`
(0x47EFFF, `ModelLoadState` to `Game.cpp`): in the old place, no model was considered downloaded, and there
were no copies of people, cars and items of the other game (#82). Special characters (`SpecialModelFor` in
`Peds.cpp`) were bypassed by this fix: their slots (290-299) mods were still read in the old place. The slot
always looked empty, the character was requested again every 100 ms, and copies of Big Smoak, Ryder and other
special characters from the partner did not appear (#105, in the log line "The streaming table is at ... a
limit adjuster moved it"). Now the slots are read through `ModelLoadState`.

A pointer to a copy of another mod player is registered in the engine (`CEntity::RegisterReference`). The
machine that the mission deletes is removed by the game along with the people in it, and the handlers of the
network of the next frame touched the released copy. Before changing the model, the copy exits the machine and
resets the tasks: the tasks held the animations of the old model. Before removing the car, everyone who gets
into it stops (the same departure MTA closes at 0x646026).

Reports on the first exceptions the handler writes only for the game code and mod, separately from the raw
ones: the exclusion of drivers and plugins, which they process themselves, ate the limit of reports. Plugins
downloaded after the mod (CrashInfo.SA, modloader, SilentPatch) put their filter on top. The mod on the first
frame returns its top and summons them after itself. The hover detector works for everyone, not just
`verbose_log` (29.10): black screen hover reports (#86, #87) came in without a place where the game was
standing. It copies the stack of the stopped stream and writes the file after the stream went again: the
recording at the stopped stream, which held the block of the heap or downloader, hung forever. To the stack,
he adds both download channels `CStreaming` and a model that everyone reads: a reading that fails, the game
repeats without end (`CStreaming::RetryLoadFile`), and on the outside it looks like a hover. So we found the
25575, the PED animation block: it's not in `gta3.img`, but in `anim\ped.ifp`, and its query
(`REQUEST_ANIMATION "PED"` in the test bot) reads the archive from the garbage position. Falling after leaving
the main cycle (`RsGlobal.quit`) mods do not count as a departure: a bundle of MixSets (SkipShutdown) and
GPS-mode removed the CJ after unloading textures, and in the report of one player there were ten such
“departures”. The log of the last launch stays nearby, at `coop.old.log`. The folded game is in a cycle of
`WinMain` and does not draw frames, so the detector considers it to be hovering: such a record it marks “its
minimized window” (`IsIconic` windows out of `RsGlobal.ps`). Unmarked in #97, the curled-up game looked like a
hover.

Commands `LOCATE_CHAR_..._2D/3D` (0x00EC - 0x00F1, 0x00FE - 0x0103) the game performs in
`CScriptThread::actorNearPoint` (0x486D80) and does not check whether there is such a character: the flags of
the non-existent she read by zero pointer (0x486DDF). The player from #97 had his save script held a character
number that wasn't there, and the game dropped, barely loading the save to create the game
(`DoGameSpecificStuffAfterSucessLoad` → `CTheScripts::Process`). The mod in front of such a command looks at
its first parameter (`CRunningScript::CollectParameters` with the return of the command pointer). If the
character is not there, he takes all the parameters as the team picks them up, and answers "not there"
(`UpdateCompareFlag`). The first five of these cases are written in a log with the name of the script.

`GET_CHAR_IN_CAR_PASSENGER_SEAT` (0x0432) the game does not check either: the handler (0x489EBC) takes the
number of the person sitting in the seat, and on empty space `CPool<CPed>::GetRef` reads the memory far below
the pool. Two of the #99 players had their game fall seven times when they got into the same car: their build
script (at `main.scm` at the Badlands start there are none) asked who was sitting in the empty seat. Mod also
looks at the parameters. If there is no car or the seat is empty, the command answers -1 as
`GET_DRIVER_OF_CAR` for a driverless car, and the first five cases are written in a log with the name of the
script (`PassengerOfEmptySeat` in `Missions.cpp`).

Part-time work in the car starts stream `R3` of `main.scm`: the player in the car of the desired model and the
button pressed (2 on the keyboard). `IS_CHAR_IN_ANY_CAR` and `IS_CHAR_IN_MODEL` are true for the passenger,
and in a single game the player does not ride as a passenger. In the koop, an ambulance passenger pressed 2,
and part-time work began in his game. `AMBULAN` at the beginning drops off everyone who sits next to his
player (`GET_CHAR_IN_CAR_PASSENGER_SEAT`, `TASK_LEAVE_CAR_IMMEDIATELY`) and lets them go
(`MARK_CHAR_AS_NO_LONGER_NEEDED`). The released mission character (`CTheScripts::CleanUpThisPed`; the player
and a copy of the other player are created as mission characters) becomes a regular passerby with the task of
getting out of the car and wandering around. The passenger's CJ exited and left on its own, without control
(Player comment, 02.11). Now, while the player is not driving, `R3` sees him outside the car: the part-time
job does not begin and there is no hint about the button (`PassengerJobButton`). The scripts of this game no
longer release the player or a copy of the other player, and a copy is not dropped from the machine
(`ReleasesPlayer`). When the ambulance was started by the driver, a copy of the passenger left and left the
driver, although the passenger himself drove further in his game. The place of the copy is decided by the game
of its owner.

Procedural objects (rocks, bushes and debris, which `CPlantMgr` lays on the ground, `ProcObjectMan_c`) are
removed only by their manager: when the earth leaves the radius, `ProcessTriangleRemoved` (0x5A3F70) removes
the object from its list and removes it. The object removed by someone else remained on the list, and the
manager called `DeleteRwObject` of the dead object through a table `CPlaceable` that the destructor left
behind. The host of #99 has had this five times at launch with Angel Pine, each time with a `rockbrkq`
breaking stone, seconds after downloading. Who removed it is not yet known: the game itself does not remove
such objects (`OBJECT_TYPE_DECORATION`), the mod, too. The mod replaces the first address of the table
`CObject` (removal destructor, 0x5A2BD0). In an object with `m_bIsProcObject`, he searches for it in the
manager’s lists and shoots it in the same way as the manager (in free, with `nNoTempObjects` counters and
matrices), and the address of the code that deleted the object writes to the log (`ForgetProcObject` in
`Patches.cpp`).

The object, which worked the destructor, and the slot in the pool remained occupied, dropped the game in the
same way: his table is already `CPlaceable`, and `CPopulation::ManageObject` → `ConvertToDummyObject` →
`CWorld::Remove` caused debris from it (0x42480000). The player with the build with mods had this eight times
in the gym (02.11) and once at boot, at `CObject::DeleteAllMissionObjects`. On the same day, his game fell to
`PLAY_OBJECT_ANIM` (0x075A) of an object that no longer exists: the handler reads the flowering at an empty
pointer (0x46EA84). Who leaves such an object is not yet known. The mod catches it in `ManageObject` and
before `DeleteAllMissionObjects`, returns the slot in the same way as `CPool::Delete`, and writes the model,
type, and location log (`FreeIfDead` in `Patches.cpp`). If the deleting destructor `CObject` is called without
releasing the slot, the log goes to who called it. `PLAY_OBJECT_ANIM` object, which is not, the mod passes
along with the parameters (`AnimOfNoObject` in `Missions.cpp`).

A long frame (processing longer than 0.5 seconds or more than 0.7 seconds from the last frame) of the mod
writes in the log with a breakdown: how many its modules took up before processing the frame with the game and
after it (and the longest of them), how much processing of the game itself (streaming, scripts), how much went
between frames (drawing, loading the world). A pause in the menu is not considered a long frame. So complaints
about lags in 1-2 seconds can be seen in the usual log of the player, without detailed.

## Client frame

In the pause menu session, the game does not stop (`no_pause_menu`): the other player's world goes further.
The menu stops the radio when it closes and when it leaves the sound page, hoping for a pause in which
`CAERadioTrackManager::StopRadio` radio machines do not touch. Without pause, the radio stopped until the next
landing, so while the car is playing the radio, this stop is skipped (calls 0x576C55 and 0x5737A5).

WinMain only processes the frame (`RsEventHandler(rsIDLE)` → `Idle`) while the window is in the foreground.
The mod keeps the game running in the background (`run_in_background`), and in full-screen mode after the
alt-tab, she drew in a lost D3D9 device: after returning, the picture was spoiled, and later the game flew out
in d3d9. While the device is lost, the frame is considered without drawing (`Idle` without an argument comes
out before drawing, call 0x748D9B), and the RenderWare device recovers on the first drawn frame, as after a
normal pause.

```
Idle (0x53E920)
 ├─ CGame::Process          Hook at call site 0x53E981
 │    ├─ Coop::PreProcess   Apply incoming messages to replicas and remote input
 │    ├─ CGame::Process     Update the world and process remote CPlayerPed in a switched context
 │    └─ Coop::PostProcess  Collect and send local state
 └─ CDebug::DebugDisplayTextBuffer  Hook at 0x53EBE4 (in menus, 0x53E86C)
      └─ Coop::Render       Chat, player names, voice icons, and notifications
```

The beginning of the mod game notices before the incoming messages of the frame: the messages of the first
frame created copies that `OnGameStarted` modules immediately forgot. `G_RESYNC` after loading the world
leaves before the modules announce their machines and people: the handler of the other game deletes everything
that came before it.

Overlay is drawn from the last 2D call of the frame (`CDebug::DebugDisplayTextBuffer`). The plugin, which
takes this call to itself and does not transmit further, left the player without a chat, an F9 window and a
code for a friend. If this call was not in the frame, the overlay is drawn in front of `RwCameraEndUpdate` of
the same frame (in `Idle` and `FrontendIdle`). In 0.2.7, the spare space was `IDirect3DDevice9::Present`, but
there RenderWare does not have a camera, and on the download screens the game fell in the rendering of text.

F7 hides the interface, the second press returns it (`core::InterfaceHidden`). The mod does not draw
signatures over players, chat, status bar and competition scoreboard (the same `Renderer::SetHidden`,
`OnRender` modules are called). Windows F2, U, and F9 are drawn when opened, and the chat open with the T key
is visible while it is written. The hood of the game hides as it hides itself: for a time of `CHud::Draw` mods
removes the menu flag "HUD" (`FrontEndMenuManager.m_bHudOn`), puts `bScriptDontDisplayRadar` and
`bScriptDontDisplayVehicleName` and then returns all three. The call itself goes further: it runs a store menu
(`CMenuSystem::Process`), tips, mission timers, and a sight. The name of the district is also hidden for a
time of `CHud::DrawAfterFade` (`bScriptDontDisplayAreaName`), and the name of the radio station is not drawn
(`CAudioEngine::DisplayRadioStationName` out of `Render2dStuff`). As long as the chat or window is open, F7
does not work, and the F9 window key assigned to F7 remains its own.

For video recording, `clean_screen` hides everything that the mod draws: the renderer drops its rectangles and
text (`Renderer::SetHidden`), and `OnRender` modules are still called. From it comes the placement of the
partner in the cutscene (`Missions::OnRender` → `scene::Analyse` for the host and `scene::Follow` for the
partner). `borderless` removes the frame from the window (`WS_POPUP`), and a screen-sized window can also
close the taskbar. `window_title` gives the window its header: it captures the game in OBS finds the desired
of two copies. The script for the cutscene recording is in the test repository
(`scripts\record-cutscenes.ps1`).

Two copies of the game on the same computer (`multi_instance`) read the same gamepad. The game takes it
through DirectInput only in the active window, but GInput reads XInput, and XInput gives buttons to any
window. The character of the second window jumped and walked with the first. Therefore, at `multi_instance`, a
no-focus window resets the gamepad buttons immediately after they are polled (`CPad::UpdatePads`, call
0x53BEE6).

While the chat or the mode window with the input field (`Input::KeyboardCaptured`) is open, the buttons and
sticks of your player are also reset there. Keystrokes before the game and before did not reach (they are
eaten by the window procedure), but players jumped, switching the layout (Shift and Alt) or putting a space in
the message when the jump is assigned to these keys (30.10). The character, while the player writes, stands,
the machine rolls itself, and a copy of the other player gets an empty gamepad.

Last-second repetition (F1, and F3 replay from the disc, `CReplay::TriggerPlayback`) in the joint game is
disabled. He only stops his world, and a copy of another player for him is a second CJ who has no clothing
record: on playback, the game fell (`CReplay::RestoreClothesDesc`, 0x45C7D4, 29.10).

The IMG Archives game opens past the Windows file cache (`CdStreamInit` puts `FILE_FLAG_NO_BUFFERING` if the
disk sector is not more than 2 KB). CJ `CClothesBuilder` clothing is assembled from pieces of player.img: up
to 10 models and 18 textures are read one at a time and without storage. A copy of another mod player is
reassembled with each new appearance and each appearance, and every time everything was read from the disk: on
the hard drive, the game stood up for a second or two. The mod removes this flag (0x406BC8) and the same
chunks come from memory.

Reading expectation in the game itself is written with the race. `CdStreamSync` (in this exe moved to
0x156CD80) sees the channel still reading (field `+4`), puts a "wait" flag (`+0x0D`) and falls asleep on the
channel semaphore without a deadline. The reading stream, when finished, resets `+4` and wakes the waiter only
if the flag is already standing. If the reading is over between the check and the flag, there is no one to
wake up and the game waits forever. From the cache, the reading ends faster, and the race window apparently
began to get caught more often: in 0.3.1, players wrote about a dead game on the black screen of the cutscene
(after darkening, the scene synchronously loads its models), about a map in the pause menu that never appeared
(when opening the card in the same way loads all visible radar tiles), and about hovering when loading save.
The mod replaces the wait without a deadline with a wait of 100 ms per cycle (call in 0x156CDAE): after each
slice, it looks at whether it is still reading the channel, and lets go when the reading is over. This case
and reading for more than 5 seconds, he writes in the log. Cache reading is disabled at `coop.ini` (`[game]
stream_cache = 0`).

The pool of collision models (`CPools::Initialise`, `push 10150` in 0x551106) raises to 20,000. The map
occupies him almost forever, and still takes up each model of the machine (`CCollisionPlugin`) and the person
(`CPedModelInfo::CreateHitColModelSkinned`) while it is in memory. Two players have more of these models than
one. `CreateHitColModelSkinned` does not check its `new CColModel`, and with a full pool, the game falls to
`CColModel::AllocateData` on reading `this` (C0000005 at 0x156173D: partner's game 05.11, police part-time
work together). Of all the calls, `AllocateData` is zero `this`, but `crash.log` is not, so it's code output.
Log writes how many records are occupied (`Diagnostics`, Col models: N of M, with an increase of 50). If the
pool has already changed another mod, the mod does not touch it.

The cutscene only begins when all of her models are in memory: `CCutsceneMgr::LoadCutsceneData_loading` checks
each frame and waits. The model, who never showed up, keeps the mission on a black screen, even though the
game is on. Also waiting for models of shooting ranges (`SHRANGE` puts a marker only when it loads targets and
sound) and a map in the pause menu. The host on message 27.10 had no cutscene, no map, no dash marker, and the
partner in the same world had a marker. If the cutscene waits longer than 10 seconds, the mod logs what models
are missing and in what condition, how much streaming memory is occupied, how many models are requested, and
what both channels are doing (`Diag.cpp`). That host, judging by the screenshots, ran out of memory streaming
in his game assembly (the same thing happened later on his other saves): the map in the menu showed only 3×3
tiles around the player, which is held by the radar, and the village police drove around San Fierro, because
the new models were not loaded. In this case, the occupied memory in the log string is close to the limit. The
fashion limit doesn't change. Without a session, he holds almost nothing in memory, and in the session holds
blocks of animation copies: all 133 blocks of the game together take up about 11 MB.

The mission can also stand on the model: Architectural Espionage, after the drawing, asks for police officers
(282), a medic (275) and a businessman (147) and waits for `HAS_MODEL_LOADED` in the cycle. Two players from
the message 01.11, it did not go further, although people and cars outside the host were loaded, and the mod
itself does not touch such requests. If the model the mission asks for doesn’t load in 10 seconds, the mod
requests it again, along with its texture vocabulary (without it, the model doesn’t load), and forward the
others (`STREAMING_MISSION_REQUIRED | STREAMING_PRIORITY_REQUEST`): the lost request rises again, the delayed
one goes first. Immediately, the mod does not load anything: a file that is not readable, the game reads over
and over again, and downloading would immediately hang the game. After 15 seconds, he once writes the model
number, her condition and the state of her vocabulary, and the same streaming numbers (`NoteModelWait` in
`Missions.cpp`).

Two copies of the game on the same PC (`multi_instance`) receive their named kernel objects: `#sacoop<pid>` is
added to the name (semaphore `CdStream` games, total memory modloader). The suffix receives only calls from
the game folder modules. Alien hooks inside the game (capture game OBS, overlays) open their objects by name
from their own process, and with the suffix OBS did not find `CaptureHook_*` and wrote a black frame.

Before a new game and downloading a mode save checks the current folder of the process. The game opens its
files in relative paths, and the folder that the plugin or dialog changed dropped it on the download screen
(`CGame::Init2` did not find `ANIM\PED.IFP`). Mod returns the game folder (`CFileMgr::ms_rootDirName`) and
writes about it in the magazine.
