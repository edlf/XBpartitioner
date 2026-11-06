@echo off
rem Regenerates the bitmap fonts compiled into the .xbe from the TrueType
rem fonts in fonts\, using the XDK's makefont.exe.
rem
rem Run from anywhere: tools\makefonts.bat
rem If makefont.exe is not on the PATH, set MAKEFONT to its full path first, e.g.
rem   set MAKEFONT=C:\Program Files\Microsoft Xbox SDK\xbox\bin\makefont.exe
rem
rem fontchars.txt lists the characters to include, one hex code per line
rem (makefont does not accept ranges). Sizes are in pixels, -a is the
rem antialiasing level (0, 2 or 4).

setlocal
if "%MAKEFONT%"=="" set MAKEFONT=makefont.exe

set ROOT=%~dp0..
set FONTS=%ROOT%\fonts
set CHARS=%~dp0fontchars.txt

rem -e 3F: draw '?' for characters that are not in the font

rem 480i/480p (640x480)
"%MAKEFONT%" -f "%FONTS%\DejaVuSans.ttf"          -h 18 -a 2 -e 3F -c font_text  "%CHARS%" "%ROOT%\font_text.h"  || goto :error
"%MAKEFONT%" -f "%FONTS%\DejaVuSans-Bold.ttf"     -h 32 -a 2 -e 3F -c font_title "%CHARS%" "%ROOT%\font_title.h" || goto :error
"%MAKEFONT%" -f "%FONTS%\DejaVuSansMono.ttf" -h 18 -a 2 -e 3F -c font_table "%CHARS%" "%ROOT%\font_table.h" || goto :error

rem 720p (1280x720): 1.5x the 480 line sizes, since 720 = 480 * 1.5
"%MAKEFONT%" -f "%FONTS%\DejaVuSans.ttf"          -h 27 -a 2 -e 3F -c font_text_720  "%CHARS%" "%ROOT%\font_text_720.h"  || goto :error
"%MAKEFONT%" -f "%FONTS%\DejaVuSans-Bold.ttf"     -h 48 -a 2 -e 3F -c font_title_720 "%CHARS%" "%ROOT%\font_title_720.h" || goto :error
"%MAKEFONT%" -f "%FONTS%\DejaVuSansMono.ttf" -h 27 -a 2 -e 3F -c font_table_720 "%CHARS%" "%ROOT%\font_table_720.h" || goto :error

echo Fonts generated.
exit /b 0

:error
echo Font generation failed.
exit /b 1
