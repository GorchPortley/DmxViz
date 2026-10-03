# DmxViz User Guide

## What is DmxViz?

DmxViz is a real-time 3D visualizer for stage lighting. Point your lighting console at it over Art-Net, sACN, or a USB DMX interface, and see volumetric beams, gobos, colour and haze rendered in real time. Use it to visualize your rig before the event, test your cues, and explore how different fixtures blend together.

### Who is it for?

Lighting designers, technicians, and hobbyists who want to see how their lighting looks before it's rigged.

### System Requirements

**Windows:** Windows 10 or later; NVIDIA, AMD or Intel GPU with OpenGL 4.3 support (most GPUs made since 2012).

**Linux:** Ubuntu 20.04 or later (or equivalent); OpenGL 4.3 GPU; X11 display server.

**Minimum GPU:** GTX 1050, RX 560, or Intel UHD 630 (runs at 60 fps with 500 fixtures).

---

## Building and Starting

### Build Steps

See [README.md](../README.md) for the full build. Quick summary:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/src/app/dmxviz          # Linux; Windows: build\src\app\Release\dmxviz.exe
```

On Linux, first install: `sudo apt install libgl-dev libx11-dev libxi-dev libxcursor-dev`.

### First Start

When you launch DmxViz for the first time, it opens with a demo show: a sample stage, fixtures, and a recorded Art-Net stream. Watch the beams move as the cues play.

To start with a blank project, click **File** > **New**.

### Command-Line Flags

- `--test-pattern`: Start with a test pattern (no demo show).
- `--screenshot <path>`: Render one frame and save to an image file (headless; needs `xvfb-run` on Linux).
- `--frames <n>`: Render for this many frames and exit (for benchmarking).
- `--verbose`: Print detailed logs to the terminal.

---

## The Workspace

DmxViz arranges your work in panels. Open and close them from the **View** menu. You can drag and dock them however you like.

### Main Panels

**Viewport**
Your 3D view of the stage. Select, move, rotate and scale fixtures and set pieces here. Middle-drag to orbit, Alt+left-drag to orbit, Shift+middle-drag to pan, Alt+middle-drag to pan, scroll wheel to zoom in and out.

**Outliner**
Hierarchical tree of everything on stage: groups, fixtures, decks, truss, etc. Select objects here, right-click to rename, duplicate, delete, group, or hide. Use this to organize your rig into groups (e.g., "Front lights", "Backlight rig").

**Inspector**
Properties of the selected object: position, rotation, scale, intensity, colour, patch address, fixture mode. Change numbers here or drag sliders.

**Environment**
Render settings: haze intensity, visibility, grid, volumetric beam quality, bloom, tone mapping.

**Fixture Library**
Browse fixture types (moving lights, LEDs, etc.), import from Open Fixture Library or GDTF, drag fixtures into the viewport, or double-click to edit.

**Fixture Editor**
Create or edit fixture definitions: geometry (bodies, pan/tilt axes, beams), DMX modes, channels, attributes (colour, gobo, iris, etc.). Save as native `.dmxviz-fixture.json` files.

**Patch**
Assign DMX universe and address to each fixture; see conflicts (two fixtures on the same address). Click "Auto-patch selected" to fill in unused addresses.

**Test Console**
Drive fixtures without an external lighting console. Control intensity, colour, position, beam attributes, and raw DMX channels per universe. Output to interfaces or merge with incoming DMX.

**DMX Monitor**
Live view of DMX data: all active universes, channel values, source names, packet rates. Shows where your DMX is coming from.

**DMX Interfaces**
Add, start and stop network (Art-Net, sACN) and USB (Enttec) interfaces. See input/output status, network interfaces, and serial ports.

**Log**
Application messages, warnings and errors. Useful for troubleshooting.

### View Menu

The **View** menu shows checkboxes for each panel. Click to show or hide. **Reset layout** puts all panels back in their default positions.

---

## Navigating the 3D View

### Camera Controls

Navigate the 3D view using the mouse. The middle mouse button stays free for selecting and using gizmos.

| Action | Control |
|--------|---------|
| Orbit around target | Middle drag or **Alt + Left drag** |
| Pan (move target point) | Shift + Middle drag or **Alt + Middle drag** |
| Dolly (zoom in/out) | Scroll wheel or **Alt + Right drag** |
| Fly mode (first-person) | Hold **Right button + W/A/S/D/Q/E** (Shift for faster) |

**Fly mode keys:**
- **W/S**: forward / backward
- **A/D**: left / right
- **Q/E**: down / up
- **Shift**: move faster

### Camera Presets

Click the preset buttons in the top-right of the Viewport: **Front**, **Side**, **Top**, **Audience**, **Perspective**. Each shows the stage from a standard angle. Or use the menu: **Add** > **Camera preset from view** to save the current camera angle.

### Selection and Gizmos

Click an object in the viewport to select it (or select from the Outliner). A wireframe bounding box appears around it.

**Gizmo tools** (top-left of the Viewport):
- **Move (W)**: drag the red/green/blue arrows to move along X/Y/Z
- **Rotate (E)**: drag the circles to rotate
- **Scale (R)**: drag the cubes to scale

Or press **W**, **E**, **R** to switch tools. Press **F** (or click the gizmo and press **F**) to frame the selection (zoom to fit).

### Snapping

Snapping applies while you drag a gizmo:
- Click **Snap** in the viewport toolbar to turn snapping on or off.
- While it is on, moves snap to a grid (default **0.25 m**) and rotations snap to angle steps (default **15°**).
  Change both steps in the fields next to the **Snap** button.

### Multi-Select and Frame

- **Shift + click** in the viewport or Outliner to add to your selection
- **Ctrl + click** to toggle an object
- Press **F** to frame the selection (zoom to fit)
- **Ctrl + A** selects all (after clicking in the viewport)

---

## Building a Stage

### Adding Objects

Use the **Add** menu in the top toolbar (or **Ctrl+Shift+O** in the Outliner):

**Primitives** (basic shapes):
- Box, Cylinder, Sphere, Plane, Cone

**Truss** (straight segments, corners, arcs, circles, towers):
- Select a profile (box, triangle, ladder) and a shape

**Stage elements**:
- Deck (2 x 1 m platform), Riser, Steps, Wall, Flat (backdrop)
- Floor (base ground plane)

**Other**:
- Reference figure (human-sized shape, helps judge scale)
- Group (folder for organizing objects)
- Import model (glTF, GLB, OBJ, 3DS)

### Moving, Rotating, Scaling

Select an object and use the gizmo tools (see Navigating the 3D View above). Or change **Position**, **Rotation**, **Scale** in the Inspector panel.

Turn on **Snap** in the viewport toolbar to align objects neatly.

### Groups and Organization

Select multiple objects and press **Ctrl+G** to group them. Expand groups in the Outliner to see and select individual fixtures. Right-click in the Outliner to rename, ungroup, hide or lock.

### Tools Menu

The **Tools** menu has powerful batch operations:

**Linear / Grid / Circular array**
Select a fixture, click **Tools** > **Linear array**, set the count and spacing, and copy it in a line (or grid, or circle).

**Align and Distribute**
Select multiple objects. **Tools** > **Align** lets you align them to the same X, Y or Z position (left/center/right, top/middle/bottom, front/middle/back). **Distribute** spreads them evenly.

**Mirror**
Select objects and **Tools** > **Mirror** to mirror them across a plane (X, Y, or Z).

**Drop to floor**
Select objects and **Tools** > **Drop to floor** to move them down to Y = 0 (the ground).

**Hang fixtures on truss**
Select fixtures and a truss, then **Tools** > **Hang fixtures on truss** to place them on top of it automatically.

### Undo and Redo

Press **Ctrl+Z** to undo, **Ctrl+Y** to redo. Or use **Edit** > **Undo** and **Redo**.

### Importing 3D Models

Click **Add** > **Import model** and choose a file (glTF, GLB, OBJ, 3DS). It appears in the viewport and can be moved, rotated, and scaled like any object.

---

## Fixtures

### The Fixture Library

Open the **Fixture Library** panel. It shows all fixture types available in your project. Browse by name, find the fixture you want, and **Add to scene** (or double-click) to place it in the viewport. Then position and patch it.

Your fixtures are stored as JSON files in the project folder; when you open a project, they load automatically.

### Importing Fixtures

The Fixture Library can import from three sources:

1. **Open Fixture Library (OFL)**
   Download `.json` files from [OpenFixtureLibrary.org](https://openfixturelibrary.org). Click **Import** in the Fixture Library, select the file, and choose the mode you want. The fixture appears in the library.

2. **GDTF** (General Device Type Format)
   Download `.gdtf` files from fixture manufacturers. GDTF includes 3D models and wheel images; DmxViz extracts all of it. Click **Import** and select the file.

3. **Native files**
   Right-click a fixture in the library, click **Edit** to open the Fixture Editor (see below). Save your changes, and they're stored as `.dmxviz-fixture.json`.

### Using Fixtures in Your Rig

1. In the **Fixture Library**, click **Add to scene** next to the fixture you want
2. It appears at the center of the stage
3. Move and rotate it using the gizmos
4. Go to the **Patch** panel and assign a DMX universe and address (or click "Auto-patch selected")
5. In the **Test Console** or from your lighting console, control the fixture

### The Fixture Editor

Click **Edit** in the Fixture Library to edit a fixture. The editor is split into sections:

**General**
Manufacturer, model name, description, categories, weight, power, dimensions.

**Geometry**
The physical shape of the fixture: bodies, pan/tilt axes, beams. Pan and tilt rotate around axes; the beam points from the fixture.

**Modes**
DMX modes (e.g., "16-bit", "8-bit dimmer only"). Each mode lists channels in order and defines what each channel does (colour, position, intensity, etc.).

**Wheels**
Colour wheels, gobo wheels, prism wheels, animation wheels. Define the slots (e.g., "Open", "Red", "Blue") and upload images for gobos.

**Preview**
A live preview of the fixture with beams. Use the Test Console to move it and see how it looks.

**Validation**
Checks for errors in your fixture definition. Fix any red warnings before saving.

Click **Save** to write the fixture to disk as `.dmxviz-fixture.json`. Every time you save, you overwrite the previous version.

### Fixture Formats

- **Native format** (`.dmxviz-fixture.json`): The full, editable format. This is what DmxViz reads and writes. All fixtures, whether imported from OFL or GDTF, can be saved in this format.
- **Open Fixture Library** (`.json`): A widely-used open format for fixture definitions. Import OFL fixtures by clicking **Import** in the Fixture Library.
- **GDTF** (`.gdtf`): An archive format with geometry, textures, and 3D models. DmxViz extracts the DMX data and textures when you import.

See [FIXTURE_FORMAT.md](FIXTURE_FORMAT.md) for the complete specification of the native format.

---

## Patching

Patching assigns each fixture to a DMX universe and address. Before your lighting console can control the fixtures, you must patch them.

### The Patch Panel

Open the **Patch** panel. It shows:
- A list of all fixtures
- Their current patch (universe and address)
- Any conflicts (two fixtures on the same address are highlighted in red)

### Patching Manually

Select a fixture in the **Outliner** or **Patch** panel. In the **Inspector** panel, find the **Patch** section. Set:
- **Universe**: Which universe this fixture is on (0–63 by default)
- **Address**: The starting DMX address (1–512)

If you patch two fixtures to the same address, they'll conflict. DmxViz highlights the conflict in red; fix it by moving one of them to a different address.

### Auto-Patch

Select one or more fixtures. In the **Patch** panel, click **Auto-patch selected**. DmxViz finds the first unused address on the current universe and patches the fixtures there. If "Skip used" is checked, it skips over addresses that are already in use.

Click **Unpatch selected** to remove the patch (set address to 0).

### Universe Offset

When you add a DMX interface in the **DMX Interfaces** panel, you can set a universe offset. For example, if you set the offset to 8, the physical Art-Net universes 0–7 on your console will map to DmxViz's logical universes 8–15. This is useful for matching your console's universe numbering.

---

## Connecting a Lighting Console

DmxViz can receive DMX data from your lighting console over Art-Net, sACN, or a USB DMX interface. It can also output DMX (for example, to a physical lighting rig).

### Network Interfaces (Art-Net and sACN)

1. Open the **DMX Interfaces** panel
2. Click **Add interface** and choose either **Art-Net** or **sACN**
3. For **Art-Net**:
   - Select your network interface (the NIC connected to your console)
   - Click **Start** to listen
4. For **sACN**:
   - Select your network interface
   - Click **Start** to listen

Your console will now send DMX to DmxViz. Check the **DMX Monitor** panel to see the incoming data.

### USB Interfaces

1. Plug in your USB DMX interface (Enttec DMX USB Pro, Enttec Open DMX USB, or compatible)
2. In the **DMX Interfaces** panel, click **Add interface** and choose the type
3. Select the serial port the device is on (e.g., `/dev/ttyUSB0` on Linux, `COM3` on Windows)
4. Click **Start**

Check the **DMX Monitor** to see DMX data flowing in.

### Network Interface Selection

In **DMX Interfaces**, the **Ethernet** section shows available network cards. Click **Refresh** to rescan. Art-Net and sACN listen on a specific NIC; if you don't see data, check that you've selected the right one.

For **Art-Net**: Uses UDP 6454. Make sure your firewall allows it.
For **sACN**: Uses UDP 5568 (multicast). You may need to enable multicast on your network.

### DMX Universe Offset

When adding an interface, set a **universe offset** if needed. For example, if your console sends on universes 1–8 but you want them to map to DmxViz universes 9–16, set the offset to 8.

### Merging Sources

If you have multiple consoles or sources sending DMX to the same universe, DmxViz merges them using **priority** (from the Art-Net or sACN packet) and **HTP** (Highest Takes Precedence) for channel level merging. Sources time out after 2.5 seconds of inactivity, so the merge automatically clears old data.

### Checking Your Connection

Open the **DMX Monitor** panel. It shows:
- All active universes
- Per-universe channel values
- Source names (e.g., "CLS GrandMA2" for Art-Net)
- Packet rates (how often data is arriving)

If you don't see your console's data, check:
- The console is sending DMX on the right universe
- The network cable is plugged in (for Art-Net/sACN)
- The USB cable is plugged in (for USB)
- Your firewall allows UDP ports 6454 (Art-Net) or 5568 (sACN)
- On Linux, the serial port permissions (run `sudo chmod 666 /dev/ttyUSB0` if needed)

---

## The Test Console

The Test Console lets you control fixtures without an external lighting console. It's useful for testing, cue-building, and learning how your rig looks.

### Basic Controls

Open the **Test Console** panel.

**Fixture selection**:
Select one or more fixtures from the list on the left. The controls on the right update to show the selected fixture(s).

**Intensity**:
Drag the main fader or click **Full** (100%), **Half** (50%), **Out** (0%) to set brightness.

**Colour**:
For RGB fixtures, click the colour swatch to open a colour picker. For fixtures with colour wheels, click the wheel slot numbers.

**Position** (pan/tilt):
Drag the pan/tilt grid, or click **Centre** to reset to 0,0.

**Beam**:
- **Zoom**: Drag to change beam width
- **Gobo**: Click wheel slot numbers
- **Shutter**: Select **Open**, **Closed**, or **Strobe** (the strobe rate is editable)
- **Iris** / **Frost** / **Focus**: Drag sliders as available for the fixture

### Raw DMX

At the bottom of the Test Console, switch to the **Raw** tab to see and edit raw DMX channels:
- Select a universe (0–63)
- See all 512 channel values
- Drag sliders to set channel values
- Click **Full** or **Zero** to set all channels on the page to 255 or 0

### Merging and Output

**Output to interfaces**: Check this box to send the Test Console data out through all enabled DMX interfaces (Art-Net, sACN, USB). This lets you drive physical lights.

By default, the Test Console **merges** with incoming DMX: if your console sends 50% on a channel and the Test Console is at 100%, DmxViz displays 100% (HTP: Highest Takes Precedence). If you want the Test Console to override incoming data, uncheck **Merge** (if exposed in the UI).

---

## Look and Performance

### Environment Panel

The **Environment** panel controls how your stage looks:

**Show grid**: Display a floor grid (default on).

**Volumetric beams**: Render haze inside beams (default on). Turning this off makes the scene faster but less realistic.

**Haze resolution**: **Half** (default, realistic) or **Quarter** (faster, softer). Use Quarter on slower GPUs.

**Automatic quality**: Enable to have DmxViz lower rendering quality if frames are slow, and raise it again when they're fast. Useful for maintaining 60 fps.

**Lens glow**: Glow effect when you look into a fixture (default on).

**Bloom**: Bright light blooms outward (default on).

**Beams stop at the floor**: Beams don't extend below Y = 0 (default on).

**Tone mapping**: **ACES, keeps colours saturated** (default; beams stay colourful) or **ACES per channel** (more contrast, but bright colours drift to white).

Click **Reset to defaults** to restore all settings.

### Per-Computer Settings

Environment settings are saved per computer, not in the project. So if you open a project on two machines, each can have different render settings (one optimized for presentation, one for performance).

---

## Projects

### New Project

Click **File** > **New** (or **Ctrl+N**) to start a new, empty project.

### Open Project

Click **File** > **Open** (**Ctrl+O**). Projects are saved as `.dmxviz` files (folders containing JSON, fixture definitions, and project data).

### Save Project

Click **File** > **Save** (**Ctrl+S**) to save. First time, it prompts for a filename and location.

Click **File** > **Save As** (**Ctrl+Shift+S**) to save to a different filename or location.

### What a Project Contains

A `.dmxviz` project folder holds:
- The stage geometry, groups, and object hierarchy
- Fixture instances (position, rotation, patch)
- Fixture type definitions (`.dmxviz-fixture.json` files)
- Camera presets
- Project metadata

It does **not** save:
- Render settings (Environment panel) – those are per-computer
- DMX interface configuration – you need to re-add and configure interfaces each time you open the project
- Incoming DMX data – only the Test Console data is saved

### Camera Presets

To save your current camera angle, go to **Add** > **Camera preset from view**. This saves the camera position, rotation, and zoom. Later, click one of the preset buttons (Front, Side, Top, Audience, Perspective) to jump back to that view.

---

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| **Ctrl+N** | New project |
| **Ctrl+O** | Open project |
| **Ctrl+S** | Save project |
| **Ctrl+Shift+S** | Save As |
| **Ctrl+Z** | Undo |
| **Ctrl+Y** or **Ctrl+Shift+Z** | Redo |
| **Ctrl+Q** | Quit |
| **Ctrl+D** | Duplicate selected object(s) |
| **Ctrl+G** | Group selected objects |
| **Ctrl+Shift+G** | Ungroup selected group |
| **W** | Switch to Move gizmo (in viewport) |
| **E** | Switch to Rotate gizmo (in viewport) |
| **R** | Switch to Scale gizmo (in viewport) |
| **F** | Frame selection (zoom to fit) |
| **F2** | Rename selected object (Outliner) |
| **Del** | Delete selected object(s) |
| **Escape** | Cancel rename or dialog |

### Camera Controls (In Viewport)

| Control | Action |
|---------|--------|
| **Middle drag** | Orbit camera around target |
| **Alt + Left drag** | Orbit |
| **Shift + Middle drag** | Pan (move target) |
| **Alt + Middle drag** | Pan |
| **Scroll wheel** | Dolly (zoom in/out) |
| **Alt + Right drag** | Dolly |
| **Right button + W/A/S/D/Q/E** | Fly mode (first-person movement) |
| **Shift** (in fly mode) | Fly faster |

### Outliner Right-Click Menu

| Option | Action |
|--------|--------|
| Rename (F2) | Rename the object |
| Duplicate (Ctrl+D) | Clone the object |
| Delete (Del) | Remove the object |
| Group (Ctrl+G) | Create a group containing selected objects |
| Ungroup (Ctrl+Shift+G) | Move group members to parent level |
| Frame (F) | Zoom camera to fit object |
| Show | Make object visible |
| Hide | Hide object |
| Lock | Prevent editing (greyed out in Outliner) |
| Unlock | Allow editing |

---

## Troubleshooting

### No Beams or Light Visible

1. Check the **Test Console**: select a fixture and set its **intensity** to **Full** (100%)
2. Check the **DMX Monitor**: is DMX data arriving? If not, check your console connection (see "Connecting a Lighting Console")
3. Check the fixture's **dimmer** or **shutter** in the Test Console. Some fixtures have a separate shutter channel that may be closed.
4. Zoom out in the viewport (scroll wheel) to see the whole stage. Beams may be there but out of view.

### No Art-Net or sACN Data Arriving

1. Check your **network cable** is plugged in
2. In the **DMX Interfaces** panel, make sure you selected the correct **network interface** (NIC). If unsure, click **Refresh** and try each one.
3. Check your **firewall** allows UDP on port 6454 (Art-Net) or 5568 (sACN). Run:
   ```bash
   sudo ufw allow 6454/udp  # Art-Net
   sudo ufw allow 5568/udp  # sACN
   ```
4. Check your **console** is sending on the right universe and IP address. Consult your console's manual.
5. Enable **verbose logging**: run `dmxviz --verbose` to see detailed DMX logs.

### USB Interface Not Recognized

1. Make sure the USB cable is **firmly plugged in**
2. On Linux, check permissions: `ls -l /dev/ttyUSB*`. If it shows `root:dialout`, add your user to the dialout group:
   ```bash
   sudo usermod -a -G dialout $(whoami)
   # Then log out and back in
   ```
3. Or run with `sudo` (not ideal but works for testing)
4. In the **DMX Interfaces** panel, click **Refresh** to rescan serial ports

### File Dialog Won't Open (Linux)

DmxViz uses **zenity** or **kdialog** to open file dialogs on Linux. Install one:
```bash
sudo apt install zenity      # For GNOME
sudo apt install kdialog     # For KDE/Plasma
```

### Low Frame Rate or Lag

1. Reduce **Haze resolution** in the **Environment** panel: switch from **Half** to **Quarter**
2. Enable **Automatic quality**: check the box in the **Environment** panel
3. Turn off **Volumetric beams** if you don't need them
4. Reduce the number of fixtures in your scene, or move distant fixtures out of view
5. Check your **GPU temperature** (it might be throttling due to heat)

### Crash on Startup

1. Run with `--test-pattern` flag to skip the demo show
2. Check the **Log** panel for error messages
3. Try `dmxviz --verbose` to see detailed logs in the terminal
4. If it crashes during file dialog, see "File Dialog Won't Open (Linux)" above

### Fixtures Look Wrong or Have Missing Data

1. Check the fixture definition in the **Fixture Library**. Click **Edit** to open the **Fixture Editor**
2. Go to the **Validation** section – red warnings indicate errors
3. Common issues:
   - **Channels out of order**: channels must be listed in address order in a mode
   - **Missing geometry**: pan/tilt need axes; beams need geometry
   - **Image not found**: wheel images must be embedded or referenced correctly
4. See [FIXTURE_FORMAT.md](FIXTURE_FORMAT.md) for the complete format reference

### "Conflict" Warnings in Patch

Two or more fixtures are patched to the same DMX address. This means the console can't control them independently.

Fix by:
1. Go to the **Patch** panel
2. Click a conflicting fixture
3. In the **Inspector**, change its **Address** to an unused number
4. Or click **Auto-patch selected** to find an unused address automatically

---

## Getting Help

- Read the [Requirements](REQUIREMENTS.md) and [Architecture](ARCHITECTURE.md) documents for technical details
- Check the [project roadmap](ROADMAP.md) for planned features
- See the [FIXTURE_FORMAT.md](FIXTURE_FORMAT.md) for fixture definition details
- Report bugs on the project's GitHub repository

