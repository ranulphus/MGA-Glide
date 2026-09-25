# Pinned external inputs. Changing any of these invalidates caches.

OW_RELEASE    := 2026-09-01-Build
OW_URL        := https://github.com/open-watcom/open-watcom-v2/releases/download/$(OW_RELEASE)/ow-snapshot.tar.xz
OW_SHA256     := bac354f3c75ffa49ff8d70a44e475de7e7c1823fff04b80c14787bd0792c9bdf

DJGPP_URL     := https://github.com/andrewwutw/build-djgpp/releases/download/v3.4/djgpp-linux64-gcc1220.tar.bz2
DJGPP_SHA256  := 8464f17017d6ab1b2bb2df4ed82357b5bf692e6e2b7fee37e315638f3d505f00

BOX86_REPO    := https://github.com/86Box/86Box.git
BOX86_COMMIT  := bcce80a8134108f44a86fbee0191955deea73e8d
ROMS_REPO     := https://github.com/86Box/roms.git
ROMS_COMMIT   := c761288ecddccb36d5664d33c0199e9d0b4e8454
