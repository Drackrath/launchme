#!/bin/bash
# A made-up Steam library for looking at the Linux launcher with covers, heroes and logos:
# app IDs nobody uses, generated pictures, a made-up account. Nothing touches a real Steam.
# Usage: wsl-fake-steam.sh [folder]  (default ~/lm/fakesteam); run launchme with STEAM_DIR=<folder>.
set -e
ROOT="${1:-$HOME/lm/fakesteam}"
rm -rf "$ROOT"
mkdir -p "$ROOT/steamapps/common" "$ROOT/config" "$ROOT/appcache/librarycache"
names=("Nebula Drift" "Iron Orchard" "Sunken Choir" "Pixel Tide" "Hollow Signal" "Crimson Atlas" "Glass Harbor" "Echo Runner"
       "Velvet Siege" "Quiet Comet" "Lantern Fields" "Neon Abyss Tactics")
colors=("#3a1c71:#ffaf7b" "#0f2027:#2c5364" "#614385:#516395" "#ff512f:#dd2476" "#1d976c:#93f9b9" "#cb2d3e:#ef473a"
        "#2193b0:#6dd5ed" "#ee0979:#ff6a00" "#7f00ff:#e100ff" "#141e30:#243b55" "#f7971e:#ffd200" "#00c6ff:#0072ff")
apps=""
for i in "${!names[@]}"; do
  id=$((9100001 + i)); name="${names[$i]}"; c="${colors[$i]}"; from="${c%%:*}"; to="${c##*:}"
  mkdir -p "$ROOT/steamapps/common/$name" "$ROOT/appcache/librarycache/$id"
  printf '"AppState"\n{\n\t"appid"\t\t"%s"\n\t"name"\t\t"%s"\n\t"StateFlags"\t\t"4"\n\t"installdir"\t\t"%s"\n\t"SizeOnDisk"\t\t"%s"\n\t"LastPlayed"\t\t"%s"\n\t"LastOwner"\t\t"76561197960265729"\n}\n' \
    "$id" "$name" "$name" "$((1000000000 + i * 37000000))" "$((1761000000 + i * 86400))" > "$ROOT/steamapps/appmanifest_$id.acf"
  apps="$apps\t\t\t\"$id\"\t\t\"$((1000000000 + i * 37000000))\"\n"
  folder="$ROOT/appcache/librarycache/$id"
  title="$(echo "$name" | sed 's/ /\\n/')"
  convert -size 600x900 "gradient:$from-$to" -gravity center -font DejaVu-Sans-Bold -pointsize 70 -fill white -annotate 0 "$title" \
    -quality 90 "$folder/library_600x900.jpg"
  convert -size 1920x620 "gradient:$to-$from" -rotate 90 -resize 1920x620! \( -size 1920x620 xc:none -fill "#ffffff22" -draw "circle 1400,300 1400,600" \) \
    -composite -quality 88 "$folder/library_hero.jpg"
  convert -size 460x215 "gradient:$from-$to" -gravity center -font DejaVu-Sans-Bold -pointsize 30 -fill white -annotate 0 "$name" "$folder/header.jpg"
  convert -size 900x260 xc:none -gravity center -font DejaVu-Sans-Bold -pointsize 96 -fill white -stroke "#00000088" -strokewidth 3 \
    -annotate 0 "$name" -trim +repage "$folder/logo.png"
done
printf '"libraryfolders"\n{\n\t"0"\n\t{\n\t\t"path"\t\t"%s"\n\t\t"apps"\n\t\t{\n%b\t\t}\n\t}\n}\n' "$ROOT" "$apps" > "$ROOT/steamapps/libraryfolders.vdf"
printf '"users"\n{\n\t"76561197960265729"\n\t{\n\t\t"AccountName"\t\t"tester"\n\t\t"PersonaName"\t\t"Tester"\n\t\t"MostRecent"\t\t"1"\n\t\t"AutoLogin"\t\t"1"\n\t\t"Timestamp"\t\t"1761600000"\n\t}\n}\n' > "$ROOT/config/loginusers.vdf"
echo "fake Steam: $ROOT (${#names[@]} games)"
