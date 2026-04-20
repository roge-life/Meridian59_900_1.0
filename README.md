[![Test Build on Push](https://github.com/roge-life/Meridian59_900_1.0/actions/workflows/buildonpush.yml/badge.svg)](https://github.com/roge-life/Meridian59_900_1.0/actions/workflows/buildonpush.yml)  

Meridian 59 v2.0, February 2016
Andrew Kirmse and Chris Kirmse

Copyright 1994-2012 Andrew Kirmse and Chris Kirmse
All rights reserved.  Meridian is a registered trademark.


Server 900 (900.emfiftynine.info) Source Code Release
--------------
This repository contains the source code for Server 900, forked from 105_2.9. Some
rooms, graphics and audio files are not included and can be obtained from a
compatible client.

Note that a more up-to-date version, including git history, will be released
alongside the upcoming 3.0 expansion. The git history contains expansion content,
thus is not reproduced here.


Play Meridian 59
--------------
This repository is for the "Server 900" version of Meridian 59.
You can create an account for this server and download the client on
the [server 900 website] (http://900.emfiftynine.info/). Note that this
repository is for the "classic" version of the client, the Ogre client
repository is at https://github.com/cyberjunk/meridian59-dotnet.


Contribute to Meridian 59 development
--------------
This is a volunteer project under active development. New contributors are
always welcome.
No experience is required or assumed, and there are many different ways to
contribute (coding, art, 3D model creation, room building, documentation).


License
--------------
This project is distributed under a license that is described in the
LICENSE file.  The license does not cover the game content (artwork, audio),
which are not included.

Note that "Meridian" is a registered trademark and you may not use it
without the written permission of the owners.

The license requires that if you redistribute this code in any form,
you must make the source code available, including any changes you
make.  We would love it if you would contribute your changes back to
the original source so that everyone can benefit.


What's included and not included
--------------
The source to the client, server, game code, Blakod compiler, room
editor, and all associated tools are included.  The source code to
the irrKlang audio library is not included, and the graphics and music
for Meridian 59 must be downloaded with the game client.


Build Instructions
--------------
Build instructions can be found in the original documentation or on the OpenMeridian wiki.

0. Install [Microsoft Visual Studio 2015 Community Edition](https://www.visualstudio.com/en-us/downloads/download-visual-studio-vs.aspx).
During installation you will need to choose "Custom" installation and add the
C++ components to the installation, as these are not installed by default.
0. Download this source code, either with a git client or with the
"Download ZIP" option from your chosen repository.

### Visual Studio GUI build
0. If you prefer the Visual Studio graphical interface, open
Meridian59.sln from the root folder of the codebase. Click on the
BUILD menu and select Build Solution (or press CTRL+SHIFT+B) to build.

### Makefile build
0. Locate your Visual Studio install folder, usually something like
`"C:\Program Files (x86)\Microsoft Visual Studio 14.0"`.
Navigate to the Common folder, and then the Tools folder.
0. Create a shortcut to vsvars32.bat to setup your environment.
0. Open the Meridian Development Shell and navigate to the folder
containing the source code, then enter `nmake debug=1` to compile.

Getting Started: Server
--------------
0. After compilation completes, browse to the `.\run\server` folder,
and run `blakserv` to start the server.
0. Use the administrative interface to manage accounts and characters.
`create automated "username" "password" "email"` is a quick way to get started on Server 900.
0. Be sure to "save" from the server interface to persist changes.

Getting Started: Client
--------------
You will need to obtain the client graphics before you can run the
client locally.
Running `postbuild.bat` from the root directory of the repo will perform the copy function.

0. After compilation completes, the client is located at
`.\run\localclient`.
0. You can point your local client at your local server by running the
client `meridian.exe` with command line flags, like this:
`meridian.exe /U:username /W:password /H:localhost /P:5959`.

Note that any time you recompile KOD code, changes need to be loaded
into your local blakserv server by clicking the 'reload system' arrow
icon, or using the `reload` command.

Third-Party Code
--------------
Meridian uses the third party libraries zlib, libpng and jansson.
Each of these is built from source which is included in the appropriately-named
directories (libzlib, libpng and libjansson).

Contact Information
--------------
For further information please join the #Meridian59de channel on
irc.esper.net.

Forked from the [OpenMeridian codebase](https://github.com/OpenMeridian/Meridian59),
which was forked from the [original Meridian 59 codebase]
(https://github.com/Meridian59/Meridian59). Original codebase
README file included as README.old.

## Server 900 Specific Changes
* We've modified the build settings to increase spawns and loot drops
* Unbound was uncapped to 100000 for testing, it still drains
* Training Points per day are set to 500
* We've fixed various entrances and spawnpoints on maps
* New Explosion (spell) and Lightning Graphics
* Simplified Account creation flow added to login
* Accounts no longer restrict a user to 1 character at a time
* In game macro (bot) commands to allow for simple spellbotting without needing another tool
* Learning points increased by (now 20) to return to 1996 levels: 6/6 with 1 int.
* Added smallcave map to i6, outskirts of tos

*attempting merge to live
  
