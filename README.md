# DioWMenu
A Wayland application menu to quicky access your favorite applications.
To populate the menu you need to copy the .desktop file of your favorite application from /usr/share/applications to ~/.config/diowmenu/items.
After the .desktop file was copied, you need to open it in a text editor, find the Exec line and add the full path to the executable (e.g. Exec=thunar change to Exec=/usr/bin/thunar).
It was tested on Debian 12 on Wayfire.

# What you can do with DioWMenu
   1. Quickly access your favorite applications.
   2. Launch the application also there is the reboot and shutdown buttons.

   to build the project you need to install the following libs:

		make
		pkgconf
	 	librsvg2-dev
		libcairo2-dev
		libwayland-dev
		libc6-dev (spawn.h)

   on Debian run the following command:

		sudo apt install make pkgconf librsvg2-dev libcairo2-dev libwayland-dev libc6-dev

# Installation/Usage
  1. Open a terminal and run:
 
		 chmod +x ./configure
		 ./configure

  2. if all went well then run:

		 make
		 sudo make install
		 
		 (if you just want to test it then run: make run)
		
  3. Run the application:
  
		 diowmenu

# Configuration
The application creates the following configuration file

		~/.config/diowmenu/diowmenu.conf

	add the full path to your current icon theme directory, e.g.:

		icons_theme=/usr/share/icons/Lyra-blue-dark

	position dialog e.g.:

		posx=1300
		posy=-300

	the default reboot and shutdown commands (you can set up your own commands):

		reboot_command=/usr/bin/systemctl reboot
		poweroff_command=/usr/bin/systemctl poweroff

   any change in the configuration file requires application restart, right click on the panel will close the panel after that launch it again.

# Screenshots

![Alt text](https://raw.githubusercontent.com/DiogenesN/diowmenu/main/diowmenu.png)

That's it!

# Support

   My Libera IRC support channel: #linuxfriends

   Matrix: https://matrix.to/#/#linuxfriends2:matrix.org

   Email: nicolas.dio@protonmail.com
