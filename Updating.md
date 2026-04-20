# Meridian 59 Client Update Process

This document explains how to push updates (graphics, sounds, code) to players on Server 900.

## Overview
Updates are handled via the `AP_DOWNLOAD` protocol. The server maintains a list of "packages" that clients check against their own "Last Download Time".

## Configuration
The following settings in `blakserv.cfg` define the update environment:

```ini
[Update]
ClientMachine    = www.emfiftynine.info
ClientFilename   = /static/patches/club.arq
PackageMachine   = www.emfiftynine.info
PackagePath      = /static/patches/
```

- **ClientMachine/Filename**: Used for "Hard Updates" (updating the `.exe` or `.dll` via `club.exe`).
- **PackageMachine/Path**: Used for "Incremental Updates" (updating `.rsb`, `.rsc`, etc. while the client is running).

---

## 1. Incremental Updates (Resources, Rooms, Kod)
Use this for updating specific files like `rsc0000.rsb`.

### Step 1: Prepare the Archive
The client automatically extracts ZIP archives.
1.  Create a ZIP containing the updated file(s).
2.  Name it something descriptive (e.g., `rsc0000_patch.zip`).

### Step 2: Upload to Web Server
Upload the ZIP to the patch server:
- **Location**: `/opt/m59-account-api/static/patches/rsc0000_patch.zip`
- **Public URL**: `https://www.emfiftynine.info/static/patches/rsc0000_patch.zip`

### Step 3: Register on Game Server
The game server uses a file called `packages.txt` in its root directory to track available patches.

1.  Edit `/opt/meridian59/packages.txt`.
2.  Add a line in the format: `filename timestamp type size`
    - **filename**: The name of the ZIP file.
    - **timestamp**: Unix Epoch (e.g., `1776651600`). Must be newer than the client's last update.
    - **type**: Bitflags for location (see below).
    - **size**: Size in bytes.
3.  Example for a resource update:
    `rsc0000_patch.zip 1776651600 0 660991`

#### Download Types (Flags)
- `0`: Resource directory (`resource/`)
- `4`: Client root directory
- `16`: Help directory (`help/`)
- `20`: Mail directory (`mail/`)
- `1`: DELETE file (if added to the flags)

### Step 4: Reload Packages
On the game server administration console, run:
```
reload packages
```
New logins will now be prompted to download the patch.

---

## 3. Automated Patch Management
We use the `manage_patches.sh` script in the root `src/` directory to automate the steps above. This script tracks the state of every file and only packages/uploads changes.

### Synchronizing Changes
To scan your local source directories (`rsc/` and `rooms/`) for changes and prepare patches:
```bash
./manage_patches.sh sync
```

### Deploying Patches
To upload the prepared ZIPs to the web server and update the game server's registry:
```bash
./manage_patches.sh deploy
```

---

## 4. Hard Updates (Client Executable)
Use this when you change the client source code and need players to get a new `meridian.exe`.

### Step 1: Prepare `club.arq`
1.  Zip the new `meridian.exe` (and any new `.dll` files).
2.  Rename the zip to `club.arq`.
3.  Upload to `/opt/m59-account-api/static/patches/club.arq`.

### Step 2: Increment Revision
1.  On the game server, edit `blakserv.cfg`.
2.  Update `MinVersion` (e.g., if current is `736`, set to `737`).
3.  Gracefully restart the server.

When a player with an older version connects, their client will automatically close and launch `club.exe` to fetch the new archive.
