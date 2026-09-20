# openRuyi boot logo

`openRuyi.svg` is the original asset supplied for this firmware, from:

https://github.com/openRuyi-Project/homepage/blob/main/static/img/openRuyi.svg

The 261x192 artwork and its colors are preserved. `openRuyi.bmp` is a
24-bit BMP with the transparent background composited onto boot black.
It is embedded as an FV raw section, decoded with EDK2 BmpSupportLib,
and exposed through the standard Platform Logo protocol.

Regenerate the bitmap with ImageMagick:

```sh
convert -background black openRuyi.svg -alpha remove -alpha off \
  BMP3:openRuyi.bmp
```

Firmware builds consume the checked-in BMP and do not require an SVG
renderer. The artwork belongs to the openRuyi project; source-code SPDX
headers in this directory describe the driver code, not the artwork.
