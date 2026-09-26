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

# CWSDPMI r7: the DPMI host DJGPP programs need under plain DOS (Loop A
# runs the HAL's DJGPP smoke test with it).
CWSDPMI_URL    := http://www.delorie.com/pub/djgpp/current/v2misc/csdpmi7b.zip
CWSDPMI_SHA256 := deacda0488e1cdd7c4a9f32fab45662b34c0ed6b2d7d4d13bc07041b62004a8c

# Matrox's public BIOS package (2003): genuine G200 (900-33), G400 (897-21)
# and G450 (935-20) video BIOSes for the emulated cards. Kept in the local
# 86Box ROM cache only, never committed.
MATROX_BIOS_URL    := https://ftp.matrox.com/pub/mga/archive/bios/2003/setup257.exe
MATROX_BIOS_SHA256 := f7c5662f5c809e5987c895229314d584b16b84ae04bdd3e3e5dd2df01f518329

# Loop B networking: mTCP (DHCP, HTGET, FTP on the bench PCs) and the Crynwr
# packet drivers (NE2000 for the 86Box dry run) from the FreeDOS 1.4 repo.
MTCP_URL       := https://www.brutman.com/mTCP/download/mTCP_2025-01-10.zip
MTCP_SHA256    := 31fdde92db0b2c390a84f17c04750eb8927afa79321e156f25afb50b4f0214a5
CRYNWR_URL     := https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/net/crynwr.zip
CRYNWR_SHA256  := b5761639a1bf4ad4fa93bfa4643ca62ae46d08412c78d45ae1b5e10d91d0b9c8
