$ProjectDir = $args[0]
$ExecDir = $args[1]
$lines = Get-Content "$ProjectDir\..\..\Misc\vq_pak\vq_pak_contents.txt"
$output_file = "$ProjectDir\..\..\Quake\vkquake.pak"

# Execute command
& "$ExecDir\mkpak.exe" $output_file "$ProjectDir\..\..\Misc\vq_pak" @lines

