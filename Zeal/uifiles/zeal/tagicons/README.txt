Zeal tag pictures
=================

A Zeal tag can show a picture in place of its built-in mark:

  ^I<name>^   shows <name>.png or I<name>.png (or .tga). Example: ^IEUR^ shows EUR.png
              or IEUR.png. For a guild code it replaces that guild's built-in icon.
              (Windows allows no file called CON.png, so use ICON.png for ^ICON^.)
  ^B<code>^   a guild's banner. A picture named B<code>.png replaces it.
              Example: BEUR.png replaces the banner ^BEUR^.

Type /tag guilds in game for the guild codes.


The three folders
-----------------

  tagicons\            Pictures that come with this Zeal build. An update may replace them,
                       so do not edit these.

  tagicons\custom\     YOUR pictures. Nothing ever installs into this folder, and a picture
                       here wins over one with the same name in tagicons\, so your changes
                       survive updates. Type /tag icons in game and Zeal creates the folder
                       if it is missing.

  tagicons\templates\  Every guild's built-in icon (I<code>.png) and banner (B<code>.png) as
                       a picture, 160 x 160 with a transparent background. Zeal does not read
                       this folder; the files are a starting point.


To change a guild's icon or banner
----------------------------------

  1. Copy its file from templates\ into custom\ (for example IEUR.png or BEUR.png).
  2. Edit it in any image editor that keeps transparency (PNG, or 32-bit TGA).
  3. In game, type /tag icons. Your picture is used from then on, and the list
     marks it "(yours)".

To add a new picture, put <name>.png in custom\ and tag with ^I<name>^.

To go back to the built-in mark, delete your file from custom\ and type /tag icons.


Rules for a picture
-------------------

  - The name is 1 to 6 letters or digits, then .png or .tga. Windows reserves a few names
    (CON, PRN, AUX, NUL, COM1 to COM9, LPT1 to LPT9) and no file can use them.
  - At most 512 x 512 pixels and 1 MB.
  - A transparent background, so it floats over the nameplate like the built-in marks.

Only you see your pictures. Everyone else sees their own picture with that name if they
have one, or the built-in mark.
